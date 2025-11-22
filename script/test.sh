#!/bin/bash

PORT=8080
ROOT=./storage

mkdir -p $ROOT

./hp-file-server $PORT $ROOT 4 &
PID=$!
sleep 1

echo "Testing upload..."
curl -X POST --data-binary @examples/sample_file.bin \
     -H "Content-Type: application/octet-stream" \
     http://localhost:$PORT/upload.bin

echo "Testing download..."
curl http://localhost:$PORT/upload.bin -o downloaded.bin

cmp downloaded.bin examples/sample_file.bin && echo "SUCCESS"

kill $PID
