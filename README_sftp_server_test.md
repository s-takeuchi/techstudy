# sftp_server_test.c

libcurl/libsshのPUT・タイムアウト試験用の最小SFTPサーバです。

## 対応内容

- 同一SFTP接続内で複数回のPUTを処理
- クライアント切断後も次の接続を受付
- ファイルごとに実ハンドルを管理
- WRITEのoffsetを使ってpwrite
- リモートファイル名ごとにuploads配下へ保存
- INIT/OPEN/WRITE/CLOSEの遅延注入
- OPEN/WRITE/CLOSEの失敗注入
- N回目のWRITEで応答せず切断

## ビルド

```bash
gcc -Wall -Wextra -O2 \
  sftp_server_test.c \
  $(pkg-config --cflags --libs libssh) \
  -o sftp_server_test
```

または:

```bash
gcc -Wall -Wextra -O2 sftp_server_test.c -lssh -o sftp_server_test
```

## 通常起動

```bash
mkdir -p uploads
./sftp_server_test
```

## 主な環境変数

```text
SFTP_BIND_PORT=11111
SFTP_HOST_KEY=/home/takeuchi/.ssh/ssh_host_rsa_key
SFTP_UPLOAD_DIR=./uploads
SFTP_DELAY_INIT=0
SFTP_DELAY_OPEN=0
SFTP_DELAY_WRITE=0
SFTP_DELAY_CLOSE=0
SFTP_FAIL_OPEN=0
SFTP_FAIL_WRITE=0
SFTP_FAIL_CLOSE=0
SFTP_DISCONNECT_AFTER_WRITE=0
SFTP_ACCEPT_FOREVER=1
```

WRITE応答を20秒遅らせる例:

```bash
SFTP_DELAY_WRITE=20 ./sftp_server_test
```

3回目のWRITEでSTATUS応答を返さず切断する例:

```bash
SFTP_DISCONNECT_AFTER_WRITE=3 ./sftp_server_test
```

## 注意

これは障害再現用です。任意の公開鍵を受理し、downloadやディレクトリ操作は実装していません。本番サーバとしては使用しないでください。
