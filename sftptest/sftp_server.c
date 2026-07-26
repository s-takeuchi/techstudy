#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <libssh/libssh.h>
#include <libssh/server.h>
#include <libssh/sftp.h>

#define DEFAULT_PORT 11111
#define DEFAULT_HOST_KEY "/home/takeuchi/.ssh/ssh_host_rsa_key"
#define DEFAULT_UPLOAD_DIR "./uploads"
#define MAX_LOCAL_PATH 4096

typedef struct upload_handle {
    int fd;
    char *remote_path;
    char *local_path;
    uint64_t bytes_written;
} upload_handle;

typedef struct config {
    int port;
    const char *host_key;
    const char *upload_dir;
    unsigned int delay_init;
    unsigned int delay_open;
    unsigned int delay_write;
    unsigned int delay_close;
    int fail_open;
    int fail_write;
    int fail_close;
    unsigned long disconnect_after_write;
    int accept_forever;
} config;

static volatile sig_atomic_t g_stop = 0;

static void signal_handler(int signo)
{
    (void)signo;
    g_stop = 1;
}

static unsigned long env_ulong(const char *name, unsigned long default_value)
{
    const char *text = getenv(name);
    char *end = NULL;
    unsigned long value;

    if (text == NULL || *text == '\0') {
        return default_value;
    }

    errno = 0;
    value = strtoul(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0') {
        fprintf(stderr, "[CONFIG] invalid %s='%s'; using %lu\n",
                name, text, default_value);
        return default_value;
    }
    return value;
}

static int env_bool(const char *name, int default_value)
{
    const char *text = getenv(name);

    if (text == NULL || *text == '\0') {
        return default_value;
    }
    if (strcmp(text, "1") == 0 || strcasecmp(text, "true") == 0 ||
        strcasecmp(text, "yes") == 0 || strcasecmp(text, "on") == 0) {
        return 1;
    }
    if (strcmp(text, "0") == 0 || strcasecmp(text, "false") == 0 ||
        strcasecmp(text, "no") == 0 || strcasecmp(text, "off") == 0) {
        return 0;
    }
    fprintf(stderr, "[CONFIG] invalid %s='%s'; using %d\n",
            name, text, default_value);
    return default_value;
}

static void load_config(config *cfg)
{
    const char *text;

    memset(cfg, 0, sizeof(*cfg));
    cfg->port = (int)env_ulong("SFTP_BIND_PORT", DEFAULT_PORT);

    text = getenv("SFTP_HOST_KEY");
    cfg->host_key = (text != NULL && *text != '\0') ? text : DEFAULT_HOST_KEY;

    text = getenv("SFTP_UPLOAD_DIR");
    cfg->upload_dir = (text != NULL && *text != '\0') ? text : DEFAULT_UPLOAD_DIR;

    cfg->delay_init = (unsigned int)env_ulong("SFTP_DELAY_INIT", 0);
    cfg->delay_open = (unsigned int)env_ulong("SFTP_DELAY_OPEN", 0);
    cfg->delay_write = (unsigned int)env_ulong("SFTP_DELAY_WRITE", 0);
    cfg->delay_close = (unsigned int)env_ulong("SFTP_DELAY_CLOSE", 0);
    cfg->fail_open = env_bool("SFTP_FAIL_OPEN", 0);
    cfg->fail_write = env_bool("SFTP_FAIL_WRITE", 0);
    cfg->fail_close = env_bool("SFTP_FAIL_CLOSE", 0);
    cfg->disconnect_after_write = env_ulong("SFTP_DISCONNECT_AFTER_WRITE", 0);
    cfg->accept_forever = env_bool("SFTP_ACCEPT_FOREVER", 1);
}

static void print_config(const config *cfg)
{
    printf("[CONFIG] port=%d\n", cfg->port);
    printf("[CONFIG] host_key=%s\n", cfg->host_key);
    printf("[CONFIG] upload_dir=%s\n", cfg->upload_dir);
    printf("[CONFIG] delays init/open/write/close=%u/%u/%u/%u sec\n",
           cfg->delay_init, cfg->delay_open,
           cfg->delay_write, cfg->delay_close);
    printf("[CONFIG] failures open/write/close=%d/%d/%d\n",
           cfg->fail_open, cfg->fail_write, cfg->fail_close);
    printf("[CONFIG] disconnect_after_write=%lu\n",
           cfg->disconnect_after_write);
    printf("[CONFIG] accept_forever=%d\n", cfg->accept_forever);
}

static void delay_stage(const char *stage, unsigned int seconds)
{
    if (seconds == 0) {
        return;
    }
    printf("[FAULT] %s: sleep %u second(s)\n", stage, seconds);
    fflush(stdout);
    sleep(seconds);
}

static int ensure_directory(const char *path)
{
    struct stat st;

    if (stat(path, &st) == 0) {
        if (S_ISDIR(st.st_mode)) {
            return 0;
        }
        fprintf(stderr, "[FILE] %s is not a directory\n", path);
        return -1;
    }
    if (errno != ENOENT) {
        perror("[FILE] stat");
        return -1;
    }
    if (mkdir(path, 0755) != 0) {
        perror("[FILE] mkdir");
        return -1;
    }
    return 0;
}

static int build_local_path(const char *upload_dir,
                            const char *remote_path,
                            char *output,
                            size_t output_size)
{
    const char *base;
    int n;

    if (remote_path == NULL || *remote_path == '\0') {
        return -1;
    }
    base = strrchr(remote_path, '/');
    base = (base != NULL) ? base + 1 : remote_path;
    if (*base == '\0' || strcmp(base, ".") == 0 || strcmp(base, "..") == 0 ||
        strchr(base, '\\') != NULL) {
        return -1;
    }
    n = snprintf(output, output_size, "%s/%s", upload_dir, base);
    return (n >= 0 && (size_t)n < output_size) ? 0 : -1;
}

static void free_upload(upload_handle *h)
{
    if (h == NULL) {
        return;
    }
    if (h->fd >= 0) {
        close(h->fd);
    }
    free(h->remote_path);
    free(h->local_path);
    free(h);
}

static int authenticate_client(ssh_session session)
{
    ssh_message message;

    printf("[AUTH] waiting for public-key authentication\n");
    while (!g_stop) {
        message = ssh_message_get(session);
        if (message == NULL) {
            fprintf(stderr, "[AUTH] ssh_message_get: %s\n",
                    ssh_get_error(session));
            return -1;
        }

        if (ssh_message_type(message) == SSH_REQUEST_AUTH &&
            ssh_message_subtype(message) == SSH_AUTH_METHOD_PUBLICKEY &&
            ssh_message_auth_pubkey(message) != NULL) {
            printf("[AUTH] accepted user='%s' (test server: any key)\n",
                   ssh_message_auth_user(message));
            ssh_message_auth_reply_success(message, 0);
            ssh_message_free(message);
            return 0;
        }

        ssh_message_reply_default(message);
        ssh_message_free(message);
    }
    return -1;
}

static int wait_sftp_channel(ssh_session session, ssh_channel *out_channel)
{
    ssh_message message;
    ssh_channel channel = NULL;

    *out_channel = NULL;
    while (!g_stop) {
        message = ssh_message_get(session);
        if (message == NULL) {
            return -1;
        }

        if (ssh_message_type(message) == SSH_REQUEST_CHANNEL_OPEN &&
            ssh_message_subtype(message) == SSH_CHANNEL_SESSION) {
            channel = ssh_message_channel_request_open_reply_accept(message);
            ssh_message_free(message);
            if (channel == NULL) {
                return -1;
            }
            printf("[SERVER] session channel opened\n");
            continue;
        }

        if (ssh_message_type(message) == SSH_REQUEST_CHANNEL &&
            ssh_message_subtype(message) == SSH_CHANNEL_REQUEST_SUBSYSTEM) {
            const char *name = ssh_message_channel_request_subsystem(message);
            if (channel != NULL && name != NULL && strcmp(name, "sftp") == 0) {
                int rc = ssh_message_channel_request_reply_success(message);
                ssh_message_free(message);
                if (rc != SSH_OK) {
                    return -1;
                }
                *out_channel = channel;
                printf("[SERVER] SFTP subsystem accepted\n");
                return 0;
            }
        }

        ssh_message_reply_default(message);
        ssh_message_free(message);
    }
    return -1;
}

static int handle_realpath(sftp_client_message msg)
{
    const char *path = sftp_client_message_get_filename(msg);
    if (path == NULL || *path == '\0' || strcmp(path, ".") == 0) {
        path = "/";
    }
    printf("[SFTP] REALPATH '%s'\n", path);
    return sftp_reply_name(msg, path, NULL);
}

static int handle_open(sftp_session sftp,
                       sftp_client_message msg,
                       const config *cfg)
{
    const char *remote = sftp_client_message_get_filename(msg);
    char local[MAX_LOCAL_PATH];
    upload_handle *h;
    ssh_string token;

    delay_stage("OPEN", cfg->delay_open);
    if (cfg->fail_open) {
        return sftp_reply_status(msg, SSH_FX_FAILURE,
                                 "Injected OPEN failure");
    }
    if (build_local_path(cfg->upload_dir, remote, local, sizeof(local)) != 0) {
        return sftp_reply_status(msg, SSH_FX_PERMISSION_DENIED,
                                 "Invalid upload path");
    }

    h = calloc(1, sizeof(*h));
    if (h == NULL) {
        return sftp_reply_status(msg, SSH_FX_FAILURE, "Out of memory");
    }
    h->fd = -1;
    h->remote_path = strdup(remote != NULL ? remote : "");
    h->local_path = strdup(local);
    if (h->remote_path == NULL || h->local_path == NULL) {
        free_upload(h);
        return sftp_reply_status(msg, SSH_FX_FAILURE, "Out of memory");
    }

    h->fd = open(local, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (h->fd < 0) {
        fprintf(stderr, "[FILE] open('%s'): %s\n", local, strerror(errno));
        free_upload(h);
        return sftp_reply_status(msg, SSH_FX_FAILURE,
                                 "Cannot create destination file");
    }

    token = sftp_handle_alloc(sftp, h);
    if (token == NULL) {
        free_upload(h);
        return sftp_reply_status(msg, SSH_FX_FAILURE,
                                 "No SFTP handle available");
    }

    printf("[SFTP] OPEN remote='%s' local='%s'\n", remote, local);
    if (sftp_reply_handle(msg, token) != SSH_OK) {
        sftp_handle_remove(sftp, h);
        ssh_string_free(token);
        free_upload(h);
        return SSH_ERROR;
    }
    ssh_string_free(token);
    return SSH_OK;
}

static int handle_write(sftp_session sftp,
                        sftp_client_message msg,
                        const config *cfg,
                        unsigned long *write_count)
{
    upload_handle *h;
    const void *data;
    size_t len;
    uint64_t offset;
    ssize_t n;

    delay_stage("WRITE", cfg->delay_write);
    ++(*write_count);

    if (cfg->disconnect_after_write > 0 &&
        *write_count >= cfg->disconnect_after_write) {
        printf("[FAULT] WRITE #%lu: disconnect without STATUS reply\n",
               *write_count);
        ssh_disconnect(sftp->session);
        return SSH_ERROR;
    }
    if (cfg->fail_write) {
        return sftp_reply_status(msg, SSH_FX_FAILURE,
                                 "Injected WRITE failure");
    }

    h = (upload_handle *)sftp_handle(sftp, msg->handle);
    if (h == NULL || h->fd < 0) {
        return sftp_reply_status(msg, SSH_FX_INVALID_HANDLE,
                                 "Invalid file handle");
    }

    data = ssh_string_data(msg->data);
    len = ssh_string_len(msg->data);
    offset = msg->offset;
    if (len > 0 && data == NULL) {
        return sftp_reply_status(msg, SSH_FX_BAD_MESSAGE,
                                 "Missing WRITE data");
    }

    n = pwrite(h->fd, data, len, (off_t)offset);
    if (n < 0 || (size_t)n != len) {
        fprintf(stderr,
                "[FILE] pwrite('%s', offset=%" PRIu64 ", len=%zu): %s\n",
                h->local_path, offset, len, strerror(errno));
        return sftp_reply_status(msg, SSH_FX_FAILURE, "Local write failed");
    }

    h->bytes_written += (uint64_t)n;
    printf("[SFTP] WRITE file='%s' offset=%" PRIu64
           " len=%zu request=%lu\n",
           h->local_path, offset, len, *write_count);
    return sftp_reply_status(msg, SSH_FX_OK, "WRITE completed");
}

static int handle_close(sftp_session sftp,
                        sftp_client_message msg,
                        const config *cfg)
{
    upload_handle *h;

    delay_stage("CLOSE", cfg->delay_close);
    h = (upload_handle *)sftp_handle(sftp, msg->handle);
    if (h == NULL) {
        return sftp_reply_status(msg, SSH_FX_INVALID_HANDLE,
                                 "Invalid file handle");
    }

    printf("[SFTP] CLOSE file='%s' bytes=%" PRIu64 "\n",
           h->local_path, h->bytes_written);
    sftp_handle_remove(sftp, h);
    if (h->fd >= 0) {
        (void)fsync(h->fd);
        close(h->fd);
        h->fd = -1;
    }
    free_upload(h);

    if (cfg->fail_close) {
        return sftp_reply_status(msg, SSH_FX_FAILURE,
                                 "Injected CLOSE failure");
    }
    return sftp_reply_status(msg, SSH_FX_OK, "CLOSE completed");
}

static int handle_sftp_session(ssh_session session,
                               ssh_channel channel,
                               const config *cfg)
{
    sftp_session sftp;
    sftp_client_message msg = NULL;
    unsigned long write_count = 0;

    sftp = sftp_server_new(session, channel);
    if (sftp == NULL) {
        fprintf(stderr, "[SFTP] sftp_server_new: %s\n",
                ssh_get_error(session));
        return -1;
    }

    delay_stage("INIT", cfg->delay_init);
    if (sftp_server_init(sftp) != SSH_OK) {
        fprintf(stderr, "[SFTP] sftp_server_init: %s\n",
                ssh_get_error(session));
        sftp_free(sftp);
        return -1;
    }

    printf("[SFTP] initialized; multiple PUT requests are accepted\n");
    while (!g_stop && ssh_is_connected(session)) {
        uint8_t type;
        int rc;

        msg = sftp_get_client_message(sftp);
        if (msg == NULL) {
            printf("[SFTP] client session ended: %s\n",
                   ssh_get_error(session));
            break;
        }

        type = sftp_client_message_get_type(msg);
        switch (type) {
        case SSH_FXP_REALPATH:
            rc = handle_realpath(msg);
            break;
        case SSH_FXP_OPEN:
            rc = handle_open(sftp, msg, cfg);
            break;
        case SSH_FXP_WRITE:
            rc = handle_write(sftp, msg, cfg, &write_count);
            break;
        case SSH_FXP_CLOSE:
            rc = handle_close(sftp, msg, cfg);
            break;
        case SSH_FXP_LSTAT:
        case SSH_FXP_STAT:
            rc = sftp_reply_status(msg, SSH_FX_NO_SUCH_FILE,
                                   "File does not exist");
            break;
        default:
            printf("[SFTP] unsupported request type=%u\n", type);
            rc = sftp_reply_status(msg, SSH_FX_OP_UNSUPPORTED,
                                   "Unsupported by test server");
            break;
        }

        sftp_client_message_free(msg);
        msg = NULL;
        if (rc != SSH_OK) {
            break;
        }
    }

    if (msg != NULL) {
        sftp_client_message_free(msg);
    }
    sftp_server_free(sftp);
    printf("[SFTP] session finished\n");
    return 0;
}

static int serve_one_client(ssh_bind bind, const config *cfg)
{
    ssh_session session = NULL;
    ssh_channel channel = NULL;
    int rc = -1;

    session = ssh_new();
    if (session == NULL) {
        return -1;
    }

    printf("[SERVER] waiting for client\n");
    if (ssh_bind_accept(bind, session) == SSH_ERROR) {
        fprintf(stderr, "[SERVER] accept: %s\n", ssh_get_error(bind));
        goto cleanup;
    }
    if (ssh_handle_key_exchange(session) != SSH_OK) {
        fprintf(stderr, "[SERVER] key exchange: %s\n",
                ssh_get_error(session));
        goto cleanup;
    }
    if (authenticate_client(session) != 0) {
        goto cleanup;
    }
    if (wait_sftp_channel(session, &channel) != 0) {
        goto cleanup;
    }

    rc = handle_sftp_session(session, channel, cfg);

cleanup:
    if (channel != NULL) {
        if (ssh_channel_is_open(channel)) {
            (void)ssh_channel_send_eof(channel);
            (void)ssh_channel_close(channel);
        }
        ssh_channel_free(channel);
    }
    if (session != NULL) {
        if (ssh_is_connected(session)) {
            ssh_disconnect(session);
        }
        ssh_free(session);
    }
    return rc;
}

int main(void)
{
    config cfg;
    ssh_bind bind = NULL;
    int exit_code = EXIT_FAILURE;

    load_config(&cfg);
    print_config(&cfg);
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    if (ensure_directory(cfg.upload_dir) != 0) {
        return EXIT_FAILURE;
    }
    if (ssh_init() < 0) {
        fprintf(stderr, "[INIT] ssh_init failed\n");
        return EXIT_FAILURE;
    }

    bind = ssh_bind_new();
    if (bind == NULL) {
        goto cleanup;
    }
    if (ssh_bind_options_set(bind, SSH_BIND_OPTIONS_BINDPORT, &cfg.port) != SSH_OK ||
        ssh_bind_options_set(bind, SSH_BIND_OPTIONS_HOSTKEY, cfg.host_key) != SSH_OK) {
        fprintf(stderr, "[INIT] options: %s\n", ssh_get_error(bind));
        goto cleanup;
    }
    if (ssh_bind_listen(bind) < 0) {
        fprintf(stderr, "[INIT] listen: %s\n", ssh_get_error(bind));
        goto cleanup;
    }

    printf("[INIT] listening on port %d\n", cfg.port);
    do {
        (void)serve_one_client(bind, &cfg);
    } while (!g_stop && cfg.accept_forever);

    exit_code = EXIT_SUCCESS;

cleanup:
    if (bind != NULL) {
        ssh_bind_free(bind);
    }
    ssh_finalize();
    printf("[SERVER] stopped\n");
    return exit_code;
}
