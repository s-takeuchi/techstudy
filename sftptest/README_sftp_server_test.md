# sftp_server_test

A minimal SFTP server implemented with **libssh** for testing SFTP client
behavior, timeout handling, and fault injection.

This server is intended for **testing only** and is **not** suitable for
production use.

## Features

- Supports SFTP **PUT (upload)**.
- Supports SFTP **GET (download)**.
- Supports multiple file transfers within a single SFTP session.
- Accepts multiple client connections sequentially.
- Configurable delay injection for protocol testing.
- Configurable failure injection.
- Configurable forced disconnects.
- Stores uploaded files under a configurable directory.
- Serves downloaded files from the same directory.

---

# Supported Operations

| Operation | Supported |
|-----------|-----------|
| INIT | Yes |
| REALPATH | Yes |
| STAT | Yes |
| LSTAT | Yes |
| OPEN | Yes |
| READ | Yes |
| WRITE | Yes |
| CLOSE | Yes |

The following operations are intentionally **not implemented**.

- REMOVE
- RENAME
- MKDIR
- RMDIR
- READDIR
- SYMLINK
- HARDLINK
- SETSTAT
- FSETSTAT

---

# Build

```bash
gcc -Wall -Wextra -O2 \
    sftp_server_test.c \
    $(pkg-config --cflags --libs libssh) \
    -o sftp_server_test
```

or

```bash
gcc -Wall -Wextra -O2 \
    sftp_server_test.c \
    -lssh \
    -o sftp_server_test
```

---

# Default Configuration

| Item | Default |
|------|---------|
| Port | 11111 |
| Upload/Download directory | ./uploads |
| Host key | /home/takeuchi/.ssh/ssh_host_rsa_key |

---

# Upload (PUT)

Example:

```bash
sftp> put local.txt remote.txt
```

The uploaded file is stored as

```
./uploads/remote.txt
```

The directory portion of the remote path is ignored.

Example

```
put local.txt /tmp/remote.txt
```

is stored as

```
./uploads/remote.txt
```

---

# Download (GET)

Files are served from the upload directory.

Suppose

```
./uploads/test.txt
```

exists on the server.

Then

```bash
sftp> get test.txt
```

or

```bash
sftp> get /home/user/test.txt
```

downloads

```
./uploads/test.txt
```

Again, only the basename of the requested path is used.

---

# Delay Injection

The server can intentionally delay protocol responses.

| Environment Variable | Description |
|----------------------|-------------|
| SFTP_DELAY_INIT | Delay INIT reply |
| SFTP_DELAY_REALPATH | Delay REALPATH reply |
| SFTP_DELAY_OPEN | Delay OPEN reply |
| SFTP_DELAY_READ | Delay READ reply |
| SFTP_DELAY_WRITE | Delay WRITE reply |
| SFTP_DELAY_CLOSE | Delay CLOSE reply |

Example

```bash
SFTP_DELAY_READ=20 ./sftp_server_test
```

delays every READ response by 20 seconds.

---

# Failure Injection

| Environment Variable | Description |
|----------------------|-------------|
| SFTP_FAIL_OPEN | Fail OPEN |
| SFTP_FAIL_READ | Fail READ |
| SFTP_FAIL_WRITE | Fail WRITE |
| SFTP_FAIL_CLOSE | Fail CLOSE |

Example

```bash
SFTP_FAIL_READ=1 ./sftp_server_test
```

returns an error for every READ request.

---

# Disconnect Injection

| Environment Variable | Description |
|----------------------|-------------|
| SFTP_DISCONNECT_AFTER_READ | Disconnect after N READ requests |
| SFTP_DISCONNECT_AFTER_WRITE | Disconnect after N WRITE requests |

Example

```bash
SFTP_DISCONNECT_AFTER_WRITE=3 ./sftp_server_test
```

disconnects immediately after receiving the third WRITE request.

---

# Protocol Flow

## PUT

```
OPEN
 ↓
WRITE
 ↓
WRITE
 ↓
...
 ↓
CLOSE
```

## GET

```
STAT
 ↓
OPEN
 ↓
READ
 ↓
READ
 ↓
...
 ↓
EOF
 ↓
CLOSE
```

---

# File Mapping

The server intentionally ignores directory names.

Example

```
/home/user/a.txt
/tmp/a.txt
a.txt
```

all refer to

```
./uploads/a.txt
```

This simplifies testing and avoids exposing arbitrary filesystem paths.

---

# Intended Usage

This server is useful for testing

- libcurl
- libssh
- libssh2
- OpenSSH sftp
- WinSCP
- FileZilla
- Paramiko
- JSch

especially for

- timeout testing
- retry testing
- disconnect handling
- protocol debugging
- fault injection

---

# Notes

This is a lightweight test server.

It intentionally

- accepts any public key (test only)
- handles one client at a time
- ignores directory hierarchy
- does not implement filesystem management commands

Do **not** use this server in production.