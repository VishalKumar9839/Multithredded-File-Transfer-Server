
#pragma once
#include <string>
#include <vector>
#include <sys/types.h>


struct HttpConn {
int fd;
std::string inbuf;
std::string outbuf;
bool keep_alive = false;
bool header_parsed = false;
std::string method;
std::string uri;
std::string http_version;
size_t content_length = 0;
size_t body_received = 0;
int file_fd = -1; // for uploads
off_t file_offset = 0; // for downloads
size_t file_size = 0;
std::string tmp_upload_path;
HttpConn(int fd_): fd(fd_){}
};