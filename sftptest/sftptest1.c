#include <stdio.h>
#include <string.h>
#include <curl/curl.h>

typedef struct {
	const char* readptr;
	size_t size_left;
} UploadObject;

size_t read_callback(char* dest, size_t size, size_t nmemb, void* userp)
{
	UploadObject* upload = (UploadObject*)userp;
	size_t max_buffer = size * nmemb;
	if (upload->size_left == 0) {
		return 0;
	}
	size_t copy_size = upload->size_left;
	if (copy_size > max_buffer) {
		copy_size = max_buffer;
	}
	memcpy(dest, upload->readptr, copy_size);
	upload->readptr += copy_size;
	upload->size_left -= copy_size;
	return copy_size;
}

int exec_curl()
{
	curl_global_init(CURL_GLOBAL_ALL);
	CURL* curl = curl_easy_init();
	if (curl) {
		const char* data_to_send = "Hello, SFTP Server! This is a test upload.\n";
		UploadObject upload_data;
		upload_data.readptr = data_to_send;
		upload_data.size_left = strlen(data_to_send);
		
		curl_easy_setopt(curl, CURLOPT_URL, "sftp://takeuchi@172.21.177.24:11111/home/takeuchi/test.txt");
		//curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
		curl_easy_setopt(curl, CURLOPT_UPLOAD, 1L);
		curl_easy_setopt(curl, CURLOPT_READFUNCTION, read_callback);
		curl_easy_setopt(curl, CURLOPT_READDATA, &upload_data);
		curl_easy_setopt(curl, CURLOPT_INFILESIZE_LARGE, (curl_off_t)upload_data.size_left);
		curl_easy_setopt(curl, CURLOPT_SSH_AUTH_TYPES, CURLSSH_AUTH_PUBLICKEY);
		curl_easy_setopt(curl, CURLOPT_SSH_PRIVATE_KEYFILE, "/home/takeuchi/.ssh/id_ed25519");
		curl_easy_setopt(curl, CURLOPT_FORBID_REUSE, 1L);
		curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
		CURLcode res = curl_easy_perform(curl);
		if (res == CURLE_OK) {
			printf("Upload success\n");
		} else {
			printf("Upload failure %s\n", curl_easy_strerror(res));
		}
		curl_easy_cleanup(curl);
	}
	curl_global_cleanup();
	
	return 0;
}

int main(int argc, char* argv[])
{
	exec_curl();
	return 0;
}

