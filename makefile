CC ?= gcc
CFLAGS ?= -std=c11 -O2 -g -Wall -Wextra -Wpedantic
LDLIBS ?= -lssh

all: sftp_server sftptest1 sftptest2 sftptest3

sftp_server: sftp_server.c
	$(CC) $(CFLAGS) -o $@ $< $(LDLIBS)

sftptest1:sftptest1.c
	$(CC) $(CFLAGS) -o sftptest1 sftptest1.c /usr/lib64/libcurl.so

sftptest2:sftptest2.c
	$(CC) $(CFLAGS) -o sftptest2 sftptest2.c /usr/lib64/libcurl.so

sftptest3:sftptest3.c
	$(CC) $(CFLAGS) -o sftptest3 sftptest3.c /usr/lib64/libcurl.so

clean:
	rm -f sftp_server
	rm -f sftptest1
	rm -f sftptest2
	rm -f sftptest3
	yes|rm -r uploads

