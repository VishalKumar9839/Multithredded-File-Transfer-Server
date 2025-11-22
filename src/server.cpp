// // src/server.cpp
// #include "server.h"
// #include "threadpool.h"
// #include "utils.h"
// #include "http_conn.h"

// #include <arpa/inet.h>
// #include <fcntl.h>
// #include <sys/epoll.h>
// #include <sys/socket.h>
// #include <sys/sendfile.h>
// #include <unistd.h>
// #include <netinet/in.h>
// #include <signal.h>
// #include <string.h>

// #include <dirent.h>
// #include <sys/types.h>
// #include <sys/stat.h>

// #include <iostream>
// #include <unordered_map>
// #include <memory>
// #include <mutex>
// #include <vector>
// #include <chrono>
// #include <thread>

// using namespace std;

// // Simple HTTP response header helper
// static std::string make_response_header(int code, const std::string &status,
//                                         size_t content_length, bool keep_alive = false) {
//     std::string h = "HTTP/1.1 " + std::to_string(code) + " " + status + "\r\n";
//     h += "Server: hp-file-server/1.0\r\n";
//     h += "Content-Length: " + std::to_string(content_length) + "\r\n";
//     if (keep_alive) h += "Connection: keep-alive\r\n";
//     else h += "Connection: close\r\n";
//     h += "\r\n";
//     return h;
// }

// Server::Server(int port_, const string& root_, int threads)
//     : port(port_), root(root_), thread_count(threads) {}

// int Server::set_nonblocking(int fd) {
//     int flags = fcntl(fd, F_GETFL, 0);
//     if (flags == -1) return -1;
//     return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
// }

// int Server::create_and_bind() {
//     int fd = socket(AF_INET, SOCK_STREAM, 0);
//     if (fd == -1) { perror("socket"); return -1; }
//     int yes = 1;
//     setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

//     sockaddr_in addr{};
//     addr.sin_family = AF_INET;
//     addr.sin_addr.s_addr = INADDR_ANY;
//     addr.sin_port = htons(port);

//     if (bind(fd, (sockaddr*)&addr, sizeof(addr)) == -1) {
//         perror("bind");
//         close(fd);
//         return -1;
//     }
//     return fd;
// }

// void Server::run() {
//     signal(SIGPIPE, SIG_IGN);
//     // ensure root exists
//     mkdir(root.c_str(), 0755);

//     int sfd = create_and_bind();
//     if (sfd < 0) return;
//     if (set_nonblocking(sfd) == -1) { perror("set_nonblocking"); close(sfd); return; }
//     if (listen(sfd, SOMAXCONN) == -1) { perror("listen"); close(sfd); return; }

//     int epfd = epoll_create1(0);
//     if (epfd == -1) { perror("epoll_create1"); close(sfd); return; }
//     epoll_event ev{};
//     ev.events = EPOLLIN;
//     ev.data.fd = sfd;

//     if (epoll_ctl(epfd, EPOLL_CTL_ADD, sfd, &ev) == -1) { perror("epoll_ctl add sfd"); close(sfd); close(epfd); return; }

//     ThreadPool pool(thread_count);
//     unordered_map<int, shared_ptr<HttpConn>> conns;
//     mutex conns_mu;

//     const int MAX_EVENTS = 1024;
//     vector<epoll_event> events(MAX_EVENTS);

//     cout << "Listening on port " << port << " root=" << root << " threads=" << thread_count << "\n";

//     while (true) {
//         int n = epoll_wait(epfd, events.data(), MAX_EVENTS, 1000);
//         if (n < 0) {
//             if (errno == EINTR) continue;
//             perror("epoll_wait");
//             break;
//         }
//         for (int i = 0; i < n; ++i) {
//             if (events[i].data.fd == sfd) {
//                 // accept loop
//                 while (true) {
//                     sockaddr_in in_addr{};
//                     socklen_t in_len = sizeof(in_addr);
//                     int infd = accept(sfd, (sockaddr*)&in_addr, &in_len);
//                     if (infd == -1) {
//                         if (errno == EAGAIN || errno == EWOULDBLOCK) break;
//                         perror("accept");
//                         break;
//                     }
//                     if (set_nonblocking(infd) == -1) { perror("set_nonblocking conn"); close(infd); continue; }
//                     epoll_event e{};
//                     e.events = EPOLLIN | EPOLLET;
//                     e.data.fd = infd;
//                     if (epoll_ctl(epfd, EPOLL_CTL_ADD, infd, &e) == -1) { perror("epoll_ctl add conn"); close(infd); continue; }
//                     auto conn = make_shared<HttpConn>(infd);
//                     {
//                         lock_guard<mutex> lg(conns_mu);
//                         conns[infd] = conn;
//                     }
//                 }
//             } else {
//                 int fd = events[i].data.fd;
//                 shared_ptr<HttpConn> conn;
//                 {
//                     lock_guard<mutex> lg(conns_mu);
//                     auto it = conns.find(fd);
//                     if (it == conns.end()) continue;
//                     conn = it->second;
//                 }
//                 // schedule read/handle to thread pool
//                 pool.enqueue([conn, this](){
//                     char buf[8192];
//                     while (true) {
//                         ssize_t n = recv(conn->fd, buf, sizeof(buf), 0);
//                         if (n > 0) conn->inbuf.append(buf, n);
//                         else if (n == 0) { close(conn->fd); return; }
//                         else {
//                             if (errno == EAGAIN || errno == EWOULDBLOCK) break;
//                             // error
//                             close(conn->fd);
//                             return;
//                         }
//                     }

//                     // parse header if not parsed
//                     if (!conn->header_parsed) {
//                         size_t pos = conn->inbuf.find("\r\n\r\n");
//                         if (pos == string::npos) return; // wait for full header
//                         string header = conn->inbuf.substr(0, pos + 4);
//                         conn->inbuf.erase(0, pos + 4);

//                         size_t sp1 = header.find(' ');
//                         size_t sp2 = header.find(' ', sp1 + 1);
//                         if (sp1 == string::npos || sp2 == string::npos) return;
//                         conn->method = header.substr(0, sp1);
//                         conn->uri = header.substr(sp1 + 1, sp2 - sp1 - 1);

//                         size_t cl = header.find("Content-Length:");
//                         if (cl != string::npos) {
//                             size_t eol = header.find("\r", cl);
//                             if (eol != string::npos) {
//                                 string val = header.substr(cl + strlen("Content-Length:"), eol - (cl + strlen("Content-Length:")));
//                                 // trim
//                                 val.erase(0, val.find_first_not_of(" \t"));
//                                 val.erase(val.find_last_not_of(" \t") + 1);
//                                 try {
//                                     conn->content_length = stoul(val);
//                                 } catch(...) { conn->content_length = 0; }
//                             }
//                         }
//                         conn->header_parsed = true;
//                     }

//                     // GET handler
//                     if (conn->method == "GET") {
//                         // root directory listing when requesting "/"
//                         if (conn->uri == "/") {
//                           string index_path = root + "/index.html";

//                         if (!file_exists(index_path)) {
//                             string msg = "HTTP/1.1 404 Not Found\r\nContent-Length: 9\r\n\r\nNot Found";
//                             send(conn->fd, msg.c_str(), msg.size(), 0);
//                             close(conn->fd);
//                             return;
//                         }
//                                              int fd = open(index_path.c_str(), O_RDONLY);
//                         off_t size = file_size(index_path);
//                                              string hdr = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nContent-Length: " + to_string(size) + "\r\n\r\n";
//                         send(conn->fd, hdr.c_str(), hdr.size(), 0);
//                                              off_t off = 0;
//                         while (off < size) sendfile(conn->fd, fd, &off, size - off);
//                                              close(fd);
//                         close(conn->fd);
//                         return;}                       

//                         string path = sanitize_path(root, conn->uri);
//                         if (!file_exists(path)) {
//                             string body = "Not Found\n";
//                             string hdr = make_response_header(404, "Not Found", body.size(), false);
//                             send(conn->fd, hdr.c_str(), hdr.size(), 0);
//                             send(conn->fd, body.c_str(), body.size(), 0);
//                             close(conn->fd);
//                             return;
//                         }

//                         int infd = open(path.c_str(), O_RDONLY);
//                         if (infd < 0) {
//                             string body = "Internal Server Error\n";
//                             string hdr = make_response_header(500, "Internal Server Error", body.size(), false);
//                             send(conn->fd, hdr.c_str(), hdr.size(), 0);
//                             send(conn->fd, body.c_str(), body.size(), 0);
//                             close(conn->fd);
//                             return;
//                         }
//                         off_t fsize = file_size(path);
//                         if (fsize < 0) fsize = 0;
//                         string hdr = make_response_header(200, "OK", fsize, false);
//                         send(conn->fd, hdr.c_str(), hdr.size(), 0);

//                         off_t offset = 0;
//                         while (offset < fsize) {
//                             ssize_t s = sendfile(conn->fd, infd, &offset, (size_t)(fsize - offset));
//                             if (s <= 0) {
//                                 if (errno == EAGAIN || errno == EINTR) {
//                                     std::this_thread::sleep_for(std::chrono::milliseconds(1));
//                                     continue;
//                                 }
//                                 break;
//                             }
//                         }
//                         close(infd);
//                         close(conn->fd);
//                         return;
//                     }

//                     // POST handler (upload)
//                     if (conn->method == "POST") {
//                         if (conn->tmp_upload_path.empty()) {
//                             // create target path: allow using the URI last segment as filename
//                             string filename = conn->uri;
//                             // strip leading '/'
//                             if (!filename.empty() && filename.front() == '/') filename.erase(0,1);
//                             if (filename.empty()) {
//                                 // fallback: use generated name
//                                 filename = "upload_" + to_string(conn->fd) + "_" + to_string(time(nullptr));
//                             }
//                             conn->tmp_upload_path = this->root + "/" + filename;
//                             conn->file_fd = open(conn->tmp_upload_path.c_str(), O_CREAT | O_WRONLY | O_TRUNC, 0644);
//                             if (conn->file_fd < 0) {
//                                 string body = "Internal Server Error\n";
//                                 string hdr = make_response_header(500, "Internal Server Error", body.size(), false);
//                                 send(conn->fd, hdr.c_str(), hdr.size(), 0);
//                                 send(conn->fd, body.c_str(), body.size(), 0);
//                                 close(conn->fd);
//                                 return;
//                             }
//                         }

//                         size_t to_write = min(conn->content_length - conn->body_received, conn->inbuf.size());
//                         if (to_write > 0) {
//                             ssize_t w = write(conn->file_fd, conn->inbuf.data(), to_write);
//                             (void)w; // ignore return (we wrote but silence warning)
//                             conn->body_received += to_write;
//                             conn->inbuf.erase(0, to_write);
//                         }

//                         // read remaining body directly from socket
//                         while (conn->body_received < conn->content_length) {
//                             char tmp[8192];
//                             ssize_t r = recv(conn->fd, tmp, sizeof(tmp), 0);
//                             if (r > 0) {
//                                 ssize_t w = write(conn->file_fd, tmp, r);
//                                 (void)w;
//                                 conn->body_received += r;
//                             } else if (r == 0) {
//                                 break;
//                             } else {
//                                 if (errno == EAGAIN || errno == EWOULDBLOCK) break;
//                                 if (errno == EINTR) continue;
//                                 perror("recv");
//                                 close(conn->file_fd);
//                                 close(conn->fd);
//                                 return;
//                             }
//                         }

//                         if (conn->body_received >= conn->content_length) {
//                             close(conn->file_fd);
//                             conn->file_fd = -1;
//                             string body = "Uploaded\n";
//                             string hdr = make_response_header(201, "Created", body.size(), false);
//                             send(conn->fd, hdr.c_str(), hdr.size(), 0);
//                             send(conn->fd, body.c_str(), body.size(), 0);
//                             close(conn->fd);
//                             return;
//                         }
//                         // if not finished, simply return and wait for next epoll event to continue (edge-triggered)
//                     }
//                 }); // end enqueue
//             } // else non-sfd
//         } // for events
//         // optional: prune closed connections from conns map (omitted for brevity)
//     } // while true

//     // cleanup (never reached in this simple server)
//     close(sfd);
//     close(epfd);
// }
// src/server.cpp
// #include "server.h"
// #include "threadpool.h"
// #include "utils.h"
// #include "http_conn.h"

// #include <arpa/inet.h>
// #include <fcntl.h>
// #include <sys/epoll.h>
// #include <sys/socket.h>
// #include <sys/sendfile.h>
// #include <unistd.h>
// #include <netinet/in.h>
// #include <signal.h>
// #include <string.h>

// #include <dirent.h>
// #include <sys/types.h>
// #include <sys/stat.h>

// #include <iostream>
// #include <unordered_map>
// #include <memory>
// #include <mutex>
// #include <vector>
// #include <chrono>
// #include <thread>

// using namespace std;

// // Simple HTTP response header helper
// static std::string make_response_header(int code, const std::string &status,
//                                         size_t content_length, bool keep_alive = false,
//                                         const std::string &content_type = "text/plain") {
//     std::string h = "HTTP/1.1 " + std::to_string(code) + " " + status + "\r\n";
//     h += "Server: hp-file-server/1.0\r\n";
//     h += "Content-Type: " + content_type + "\r\n";
//     h += "Content-Length: " + std::to_string(content_length) + "\r\n";
//     if (keep_alive) h += "Connection: keep-alive\r\n";
//     else h += "Connection: close\r\n";
//     h += "\r\n";
//     return h;
// }

// Server::Server(int port_, const string& root_, int threads)
//     : port(port_), root(root_), thread_count(threads) {}

// int Server::set_nonblocking(int fd) {
//     int flags = fcntl(fd, F_GETFL, 0);
//     if (flags == -1) return -1;
//     return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
// }

// int Server::create_and_bind() {
//     int fd = socket(AF_INET, SOCK_STREAM, 0);
//     if (fd == -1) { perror("socket"); return -1; }
//     int yes = 1;
//     setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

//     sockaddr_in addr{};
//     addr.sin_family = AF_INET;
//     addr.sin_addr.s_addr = INADDR_ANY;
//     addr.sin_port = htons(port);

//     if (bind(fd, (sockaddr*)&addr, sizeof(addr)) == -1) {
//         perror("bind");
//         close(fd);
//         return -1;
//     }
//     return fd;
// }

// void Server::run() {
//     signal(SIGPIPE, SIG_IGN);
//     // ensure root exists
//     mkdir(root.c_str(), 0755);

//     int sfd = create_and_bind();
//     if (sfd < 0) return;
//     if (set_nonblocking(sfd) == -1) { perror("set_nonblocking"); close(sfd); return; }
//     if (listen(sfd, SOMAXCONN) == -1) { perror("listen"); close(sfd); return; }

//     int epfd = epoll_create1(0);
//     if (epfd == -1) { perror("epoll_create1"); close(sfd); return; }
//     epoll_event ev{};
//     ev.events = EPOLLIN;
//     ev.data.fd = sfd;

//     if (epoll_ctl(epfd, EPOLL_CTL_ADD, sfd, &ev) == -1) { perror("epoll_ctl add sfd"); close(sfd); close(epfd); return; }

//     ThreadPool pool(thread_count);
//     unordered_map<int, shared_ptr<HttpConn>> conns;
//     mutex conns_mu;

//     const int MAX_EVENTS = 1024;
//     vector<epoll_event> events(MAX_EVENTS);

//     cout << "Listening on port " << port << " root=" << root << " threads=" << thread_count << "\n";

//     while (true) {
//         int n = epoll_wait(epfd, events.data(), MAX_EVENTS, 1000);
//         if (n < 0) {
//             if (errno == EINTR) continue;
//             perror("epoll_wait");
//             break;
//         }
//         for (int i = 0; i < n; ++i) {
//             if (events[i].data.fd == sfd) {
//                 // accept loop
//                 while (true) {
//                     sockaddr_in in_addr{};
//                     socklen_t in_len = sizeof(in_addr);
//                     int infd = accept(sfd, (sockaddr*)&in_addr, &in_len);
//                     if (infd == -1) {
//                         if (errno == EAGAIN || errno == EWOULDBLOCK) break;
//                         perror("accept");
//                         break;
//                     }
//                     if (set_nonblocking(infd) == -1) { perror("set_nonblocking conn"); close(infd); continue; }
//                     epoll_event e{};
//                     e.events = EPOLLIN | EPOLLET;
//                     e.data.fd = infd;
//                     if (epoll_ctl(epfd, EPOLL_CTL_ADD, infd, &e) == -1) { perror("epoll_ctl add conn"); close(infd); continue; }
//                     auto conn = make_shared<HttpConn>(infd);
//                     {
//                         lock_guard<mutex> lg(conns_mu);
//                         conns[infd] = conn;
//                     }
//                 }
//             } else {
//                 int fd = events[i].data.fd;
//                 shared_ptr<HttpConn> conn;
//                 {
//                     lock_guard<mutex> lg(conns_mu);
//                     auto it = conns.find(fd);
//                     if (it == conns.end()) continue;
//                     conn = it->second;
//                 }
//                 // schedule read/handle to thread pool
//                 pool.enqueue([conn, this](){
//                     char buf[8192];
//                     while (true) {
//                         ssize_t n = recv(conn->fd, buf, sizeof(buf), 0);
//                         if (n > 0) conn->inbuf.append(buf, n);
//                         else if (n == 0) { close(conn->fd); return; }
//                         else {
//                             if (errno == EAGAIN || errno == EWOULDBLOCK) break;
//                             // error
//                             close(conn->fd);
//                             return;
//                         }
//                     }

//                     // parse header if not parsed
//                     string header_full; // keep local copy in order to inspect Accept and other headers
//                     if (!conn->header_parsed) {
//                         size_t pos = conn->inbuf.find("\r\n\r\n");
//                         if (pos == string::npos) return; // wait for full header
//                         header_full = conn->inbuf.substr(0, pos + 4);
//                         conn->inbuf.erase(0, pos + 4);

//                         size_t sp1 = header_full.find(' ');
//                         size_t sp2 = header_full.find(' ', sp1 + 1);
//                         if (sp1 == string::npos || sp2 == string::npos) return;
//                         conn->method = header_full.substr(0, sp1);
//                         conn->uri = header_full.substr(sp1 + 1, sp2 - sp1 - 1);

//                         size_t cl = header_full.find("Content-Length:");
//                         if (cl != string::npos) {
//                             size_t eol = header_full.find("\r", cl);
//                             if (eol != string::npos) {
//                                 string val = header_full.substr(cl + strlen("Content-Length:"), eol - (cl + strlen("Content-Length:")));
//                                 // trim
//                                 val.erase(0, val.find_first_not_of(" \t"));
//                                 val.erase(val.find_last_not_of(" \t") + 1);
//                                 try {
//                                     conn->content_length = stoul(val);
//                                 } catch(...) { conn->content_length = 0; }
//                             }
//                         }

//                         conn->header_parsed = true;
//                     }

//                     // For root "/" we may need to inspect Accept header; if header_full is empty (because header was parsed earlier), try to extract from inbuf? Usually header_full exists here.
//                     // But to be safe, try to re-find Accept in the original header portion stored earlier (we used header_full variable above)
//                     // Determine whether client prefers HTML
//                     bool client_accepts_html = false;
//                     if (!header_full.empty()) {
//                         size_t acc = header_full.find("Accept:");
//                         if (acc != string::npos) {
//                             size_t eol = header_full.find("\r", acc);
//                             string accval = header_full.substr(acc + 7, (eol==string::npos? string::npos : eol - (acc+7)));
//                             // lowercase check
//                             for (auto &c : accval) c = tolower((unsigned char)c);
//                             if (accval.find("text/html") != string::npos || accval.find("*/*") != string::npos) {
//                                 client_accepts_html = true;
//                             }
//                         }
//                     }

//                     // GET handler
//                     if (conn->method == "GET") {
//                         // root directory special behavior:
//                         if (conn->uri == "/") {
//                             // If the client accepts HTML, serve index.html so the browser shows UI.
//                             // If the client doesn't advertise Accept: text/html (like fetch from JS expecting file list),
//                             // return plain listing (one file per line) — so existing frontend that does fetch('/') keeps working.
//                             string index_path = root + "/index.html";
//                             if (client_accepts_html) {
//                                 if (!file_exists(index_path)) {
//                                     string msg = "HTTP/1.1 404 Not Found\r\nContent-Length: 9\r\n\r\nNot Found";
//                                     send(conn->fd, msg.c_str(), msg.size(), 0);
//                                     close(conn->fd);
//                                     return;
//                                 }
//                                 int fd = open(index_path.c_str(), O_RDONLY);
//                                 if (fd < 0) {
//                                     string body = "Internal Server Error\n";
//                                     string hdr = make_response_header(500, "Internal Server Error", body.size(), false, "text/plain");
//                                     send(conn->fd, hdr.c_str(), hdr.size(), 0);
//                                     send(conn->fd, body.c_str(), body.size(), 0);
//                                     close(conn->fd);
//                                     return;
//                                 }
//                                 off_t size = file_size(index_path);
//                                 string hdr = make_response_header(200, "OK", (size_t)size, false, "text/html");
//                                 send(conn->fd, hdr.c_str(), hdr.size(), 0);
//                                 off_t off = 0;
//                                 while (off < size) sendfile(conn->fd, fd, &off, size - off);
//                                 close(fd);
//                                 close(conn->fd);
//                                 return;
//                             } else {
//                                 // return simple plain-text listing (one filename per line) for programmatic clients
//                                 DIR *d = opendir(root.c_str());
//                                 if (!d) {
//                                     string body = "Internal Server Error\n";
//                                     string hdr = make_response_header(500, "Internal Server Error", body.size(), false, "text/plain");
//                                     send(conn->fd, hdr.c_str(), hdr.size(), 0);
//                                     send(conn->fd, body.c_str(), body.size(), 0);
//                                     close(conn->fd);
//                                     return;
//                                 }
//                                 string listing;
//                                 struct dirent *e;
//                                 while ((e = readdir(d)) != NULL) {
//                                     if (e->d_type == DT_REG) {
//                                         listing += e->d_name;
//                                         listing += "\n";
//                                     }
//                                 }
//                                 closedir(d);
//                                 string hdr = make_response_header(200, "OK", listing.size(), false, "text/plain");
//                                 send(conn->fd, hdr.c_str(), hdr.size(), 0);
//                                 send(conn->fd, listing.c_str(), listing.size(), 0);
//                                 close(conn->fd);
//                                 return;
//                             }
//                         }

//                         // for any other URI, treat as file download
//                         string path = sanitize_path(root, conn->uri);
//                         if (!file_exists(path)) {
//                             string body = "Not Found\n";
//                             string hdr = make_response_header(404, "Not Found", body.size(), false, "text/plain");
//                             send(conn->fd, hdr.c_str(), hdr.size(), 0);
//                             send(conn->fd, body.c_str(), body.size(), 0);
//                             close(conn->fd);
//                             return;
//                         }

//                         int infd = open(path.c_str(), O_RDONLY);
//                         if (infd < 0) {
//                             string body = "Internal Server Error\n";
//                             string hdr = make_response_header(500, "Internal Server Error", body.size(), false, "text/plain");
//                             send(conn->fd, hdr.c_str(), hdr.size(), 0);
//                             send(conn->fd, body.c_str(), body.size(), 0);
//                             close(conn->fd);
//                             return;
//                         }
//                         off_t fsize = file_size(path);
//                         if (fsize < 0) fsize = 0;

//                         // determine content-type by extension (simple)
//                         string content_type = "application/octet-stream";
//                         auto dot = path.find_last_of('.');
//                         if (dot != string::npos) {
//                             string ext = path.substr(dot+1);
//                             if (ext == "html" || ext == "htm") content_type = "text/html";
//                             else if (ext == "js") content_type = "application/javascript";
//                             else if (ext == "css") content_type = "text/css";
//                             else if (ext == "json") content_type = "application/json";
//                             else if (ext == "png") content_type = "image/png";
//                             else if (ext == "jpg" || ext == "jpeg") content_type = "image/jpeg";
//                             else if (ext == "pdf") content_type = "application/pdf";
//                             else if (ext == "txt") content_type = "text/plain";
//                         }

//                         string hdr = make_response_header(200, "OK", (size_t)fsize, false, content_type);
//                         send(conn->fd, hdr.c_str(), hdr.size(), 0);

//                         off_t offset = 0;
//                         while (offset < fsize) {
//                             ssize_t s = sendfile(conn->fd, infd, &offset, (size_t)(fsize - offset));
//                             if (s <= 0) {
//                                 if (errno == EAGAIN || errno == EINTR) {
//                                     this_thread::sleep_for(chrono::milliseconds(1));
//                                     continue;
//                                 }
//                                 break;
//                             }
//                         }
//                         close(infd);
//                         close(conn->fd);
//                         return;
//                     } // end GET

//                     // POST handler (upload)
//                     if (conn->method == "POST") {
//                         if (conn->tmp_upload_path.empty()) {
//                             // create target path: allow using the URI last segment as filename
//                             string filename = conn->uri;
//                             // strip leading '/'
//                             if (!filename.empty() && filename.front() == '/') filename.erase(0,1);
//                             if (filename.empty()) {
//                                 // fallback: use generated name
//                                 filename = "upload_" + to_string(conn->fd) + "_" + to_string(time(nullptr));
//                             }
//                             conn->tmp_upload_path = this->root + "/" + filename;
//                             conn->file_fd = open(conn->tmp_upload_path.c_str(), O_CREAT | O_WRONLY | O_TRUNC, 0644);
//                             if (conn->file_fd < 0) {
//                                 string body = "Internal Server Error\n";
//                                 string hdr = make_response_header(500, "Internal Server Error", body.size(), false, "text/plain");
//                                 send(conn->fd, hdr.c_str(), hdr.size(), 0);
//                                 send(conn->fd, body.c_str(), body.size(), 0);
//                                 close(conn->fd);
//                                 return;
//                             }
//                         }

//                         size_t to_write = min(conn->content_length - conn->body_received, conn->inbuf.size());
//                         if (to_write > 0) {
//                             ssize_t w = write(conn->file_fd, conn->inbuf.data(), to_write);
//                             (void)w; // ignore return (we wrote but silence warning)
//                             conn->body_received += to_write;
//                             conn->inbuf.erase(0, to_write);
//                         }

//                         // read remaining body directly from socket
//                         while (conn->body_received < conn->content_length) {
//                             char tmp[8192];
//                             ssize_t r = recv(conn->fd, tmp, sizeof(tmp), 0);
//                             if (r > 0) {
//                                 ssize_t w = write(conn->file_fd, tmp, r);
//                                 (void)w;
//                                 conn->body_received += r;
//                             } else if (r == 0) {
//                                 break;
//                             } else {
//                                 if (errno == EAGAIN || errno == EWOULDBLOCK) break;
//                                 if (errno == EINTR) continue;
//                                 perror("recv");
//                                 close(conn->file_fd);
//                                 close(conn->fd);
//                                 return;
//                             }
//                         }

//                         if (conn->body_received >= conn->content_length) {
//                             close(conn->file_fd);
//                             conn->file_fd = -1;
//                             string body = "Uploaded\n";
//                             string hdr = make_response_header(201, "Created", body.size(), false, "text/plain");
//                             send(conn->fd, hdr.c_str(), hdr.size(), 0);
//                             send(conn->fd, body.c_str(), body.size(), 0);
//                             close(conn->fd);
//                             return;
//                         }
//                         // if not finished, simply return and wait for next epoll event to continue (edge-triggered)
//                     } // end POST
//                 }); // end enqueue
//             } // else non-sfd
//         } // for events
//         // optional: prune closed connections from conns map (omitted for brevity)
//     } // while true

//     // cleanup (never reached in this simple server)
//     close(sfd);
//     close(epfd);
// }

// latest changes 
// src/server.cpp
#include "server.h"
#include "threadpool.h"
#include "utils.h"
#include "http_conn.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/sendfile.h>
#include <unistd.h>
#include <netinet/in.h>
#include <signal.h>
#include <string.h>

#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <iostream>
#include <unordered_map>
#include <memory>
#include <mutex>
#include <vector>
#include <chrono>
#include <thread>
#include <algorithm>
#include <cctype>

using namespace std;

// Simple HTTP response header helper
static std::string make_response_header(int code, const std::string &status,
                                        size_t content_length, bool keep_alive = false,
                                        const std::string &content_type = "text/plain") {
    std::string h = "HTTP/1.1 " + std::to_string(code) + " " + status + "\r\n";
    h += "Server: hp-file-server/1.0\r\n";
    h += "Content-Type: " + content_type + "\r\n";
    h += "Content-Length: " + std::to_string(content_length) + "\r\n";
    if (keep_alive) h += "Connection: keep-alive\r\n";
    else h += "Connection: close\r\n";
    h += "\r\n";
    return h;
}

// Simple helper to map extension -> mime type
static string guess_mime(const string &path) {
    auto dot = path.find_last_of('.');
    if (dot == string::npos) return "application/octet-stream";
    string ext = path.substr(dot + 1);
    for (auto &c : ext) c = tolower((unsigned char)c);
    if (ext == "html" || ext == "htm") return "text/html";
    if (ext == "js") return "application/javascript";
    if (ext == "css") return "text/css";
    if (ext == "json") return "application/json";
    if (ext == "png") return "image/png";
    if (ext == "jpg" || ext == "jpeg") return "image/jpeg";
    if (ext == "gif") return "image/gif";
    if (ext == "pdf") return "application/pdf";
    if (ext == "txt") return "text/plain";
    return "application/octet-stream";
}

Server::Server(int port_, const string& root_, int threads)
    : port(port_), root(root_), thread_count(threads) {
    // normalize root path (remove trailing slash)
    if (!root.empty() && root.back() == '/') {
        root.pop_back();
    }
}

int Server::set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

int Server::create_and_bind() {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd == -1) { perror("socket"); return -1; }
    int yes = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if (bind(fd, (sockaddr*)&addr, sizeof(addr)) == -1) {
        perror("bind");
        close(fd);
        return -1;
    }
    return fd;
}

void Server::run() {
    // Ignore SIGPIPE - sockets may be closed by peers
    signal(SIGPIPE, SIG_IGN);

    // ensure root exists
    mkdir(root.c_str(), 0755);

    int sfd = create_and_bind();
    if (sfd < 0) return;

    if (set_nonblocking(sfd) == -1) { perror("set_nonblocking"); close(sfd); return; }
    if (listen(sfd, SOMAXCONN) == -1) { perror("listen"); close(sfd); return; }

    int epfd = epoll_create1(0);
    if (epfd == -1) { perror("epoll_create1"); close(sfd); return; }
    epoll_event ev{};
    ev.events = EPOLLIN;
    ev.data.fd = sfd;

    if (epoll_ctl(epfd, EPOLL_CTL_ADD, sfd, &ev) == -1) { perror("epoll_ctl add sfd"); close(sfd); close(epfd); return; }

    ThreadPool pool(thread_count);
    unordered_map<int, shared_ptr<HttpConn>> conns;
    mutex conns_mu;

    const int MAX_EVENTS = 1024;
    vector<epoll_event> events(MAX_EVENTS);

    cout << "Listening on port " << port << " root=" << root << " threads=" << thread_count << "\n";

    while (true) {
        int n = epoll_wait(epfd, events.data(), MAX_EVENTS, 1000);
        if (n < 0) {
            if (errno == EINTR) continue;
            perror("epoll_wait");
            break;
        }

        for (int i = 0; i < n; ++i) {
            if (events[i].data.fd == sfd) {
                // accept loop (edge-triggered)
                while (true) {
                    sockaddr_in in_addr{};
                    socklen_t in_len = sizeof(in_addr);
                    int infd = accept(sfd, (sockaddr*)&in_addr, &in_len);
                    if (infd == -1) {
                        if (errno == EAGAIN || errno == EWOULDBLOCK) break;
                        perror("accept");
                        break;
                    }
                    if (set_nonblocking(infd) == -1) { perror("set_nonblocking conn"); close(infd); continue; }
                    epoll_event e{};
                    e.events = EPOLLIN | EPOLLET;
                    e.data.fd = infd;
                    if (epoll_ctl(epfd, EPOLL_CTL_ADD, infd, &e) == -1) { perror("epoll_ctl add conn"); close(infd); continue; }

                    auto conn = make_shared<HttpConn>(infd);
                    lock_guard<mutex> lg(conns_mu);
                    conns[infd] = conn;
                }
            } else {
                int fd = events[i].data.fd;
                shared_ptr<HttpConn> conn;
                {
                    lock_guard<mutex> lg(conns_mu);
                    auto it = conns.find(fd);
                    if (it == conns.end()) continue;
                    conn = it->second;
                }

                // schedule read/handle to thread pool
                pool.enqueue([conn, this](){
                    char buf[8192];

                    // read all available data (edge-triggered)
                    while (true) {
                        ssize_t n = recv(conn->fd, buf, sizeof(buf), 0);
                        if (n > 0) {
                            conn->inbuf.append(buf, n);
                        } else if (n == 0) {
                            // client closed
                            close(conn->fd);
                            return;
                        } else {
                            if (errno == EAGAIN || errno == EWOULDBLOCK) break;
                            // unrecoverable error
                            close(conn->fd);
                            return;
                        }
                    }

                    // parse header if not parsed
                    string header_full;
                    if (!conn->header_parsed) {
                        size_t pos = conn->inbuf.find("\r\n\r\n");
                        if (pos == string::npos) return; // wait for full header
                        header_full = conn->inbuf.substr(0, pos + 4);
                        conn->inbuf.erase(0, pos + 4);

                        // parse method and uri
                        size_t sp1 = header_full.find(' ');
                        size_t sp2 = header_full.find(' ', sp1 + 1);
                        if (sp1 == string::npos || sp2 == string::npos) return;
                        conn->method = header_full.substr(0, sp1);
                        conn->uri = header_full.substr(sp1 + 1, sp2 - sp1 - 1);

                        // parse Content-Length if present
                        size_t cl = header_full.find("Content-Length:");
                        if (cl != string::npos) {
                            size_t eol = header_full.find("\r", cl);
                            if (eol != string::npos) {
                                string val = header_full.substr(cl + strlen("Content-Length:"), eol - (cl + strlen("Content-Length:")));
                                // trim
                                val.erase(0, val.find_first_not_of(" \t"));
                                val.erase(val.find_last_not_of(" \t") + 1);
                                try {
                                    conn->content_length = stoul(val);
                                } catch(...) { conn->content_length = 0; }
                            }
                        }

                        conn->header_parsed = true;
                    }

                    // ---------------- GET handler ----------------
                    if (conn->method == "GET") {
                        // ALWAYS serve index.html for "/"
                        if (conn->uri == "/") {
                            string index_path = root + "/index.html";
                            if (!file_exists(index_path)) {
                                string msg = "HTTP/1.1 404 Not Found\r\nContent-Length: 9\r\n\r\nNot Found";
                                send(conn->fd, msg.c_str(), msg.size(), 0);
                                close(conn->fd);
                                return;
                            }

                            int fd = open(index_path.c_str(), O_RDONLY);
                            if (fd < 0) {
                                string body = "Internal Server Error\n";
                                string hdr = make_response_header(500, "Internal Server Error", body.size(), false, "text/plain");
                                send(conn->fd, hdr.c_str(), hdr.size(), 0);
                                send(conn->fd, body.c_str(), body.size(), 0);
                                close(conn->fd);
                                return;
                            }

                            off_t size = file_size(index_path);
                            string hdr =
                                "HTTP/1.1 200 OK\r\n"
                                "Content-Type: text/html\r\n"
                                "Cache-Control: no-cache, no-store, must-revalidate\r\n"
                                "Content-Length: " + to_string((size_t)size) + "\r\n\r\n";

                            send(conn->fd, hdr.c_str(), hdr.size(), 0);
                            off_t off = 0;
                            while (off < size) {
                                ssize_t s = sendfile(conn->fd, fd, &off, (size_t)(size - off));
                                if (s <= 0) {
                                    if (errno == EINTR) continue;
                                    break;
                                }
                            }
                            close(fd);
                            close(conn->fd);
                            return;
                        }

                        // Serve static file for other URIs
                        string path = sanitize_path(root, conn->uri);
                        if (!file_exists(path)) {
                            string body = "Not Found\n";
                            string hdr = make_response_header(404, "Not Found", body.size(), false, "text/plain");
                            send(conn->fd, hdr.c_str(), hdr.size(), 0);
                            send(conn->fd, body.c_str(), body.size(), 0);
                            close(conn->fd);
                            return;
                        }

                        int infd = open(path.c_str(), O_RDONLY);
                        if (infd < 0) {
                            string body = "Internal Server Error\n";
                            string hdr = make_response_header(500, "Internal Server Error", body.size(), false, "text/plain");
                            send(conn->fd, hdr.c_str(), hdr.size(), 0);
                            send(conn->fd, body.c_str(), body.size(), 0);
                            close(conn->fd);
                            return;
                        }

                        off_t fsize = file_size(path);
                        if (fsize < 0) fsize = 0;
                        string content_type = guess_mime(path);

                        string hdr = make_response_header(200, "OK", (size_t)fsize, false, content_type);
                        send(conn->fd, hdr.c_str(), hdr.size(), 0);

                        off_t offset = 0;
                        while (offset < fsize) {
                            ssize_t s = sendfile(conn->fd, infd, &offset, (size_t)(fsize - offset));
                            if (s <= 0) {
                                if (errno == EAGAIN || errno == EINTR) {
                                    this_thread::sleep_for(chrono::milliseconds(1));
                                    continue;
                                }
                                break;
                            }
                        }
                        close(infd);
                        close(conn->fd);
                        return;
                    } // end GET

                    // ---------------- POST handler (upload) ----------------
                    if (conn->method == "POST") {
                        // lazily create target file descriptor if not already
                        if (conn->tmp_upload_path.empty()) {
                            // derive filename from URI (strip leading '/')
                            string filename = conn->uri;
                            if (!filename.empty() && filename.front() == '/') filename.erase(0,1);
                            if (filename.empty()) {
                                filename = "upload_" + to_string(conn->fd) + "_" + to_string(time(nullptr));
                            }
                            // sanitize filename (simple): replace slashes
                            replace(filename.begin(), filename.end(), '/', '_');
                            conn->tmp_upload_path = this->root + "/" + filename;

                            conn->file_fd = open(conn->tmp_upload_path.c_str(), O_CREAT | O_WRONLY | O_TRUNC, 0644);
                            if (conn->file_fd < 0) {
                                string body = "Internal Server Error\n";
                                string hdr = make_response_header(500, "Internal Server Error", body.size(), false, "text/plain");
                                send(conn->fd, hdr.c_str(), hdr.size(), 0);
                                send(conn->fd, body.c_str(), body.size(), 0);
                                close(conn->fd);
                                return;
                            }
                        }

                        // first flush any existing buffer (may contain part or whole body)
                        size_t to_write = min(conn->content_length - conn->body_received, conn->inbuf.size());
                        if (to_write > 0) {
                            ssize_t w = write(conn->file_fd, conn->inbuf.data(), to_write);
                            (void)w;
                            conn->body_received += to_write;
                            conn->inbuf.erase(0, to_write);
                        }

                        // then read remaining body from socket
                        while (conn->body_received < conn->content_length) {
                            char tmp[8192];
                            ssize_t r = recv(conn->fd, tmp, sizeof(tmp), 0);
                            if (r > 0) {
                                ssize_t w = write(conn->file_fd, tmp, r);
                                (void)w;
                                conn->body_received += r;
                            } else if (r == 0) {
                                break;
                            } else {
                                if (errno == EAGAIN || errno == EWOULDBLOCK) break;
                                if (errno == EINTR) continue;
                                perror("recv");
                                close(conn->file_fd);
                                close(conn->fd);
                                return;
                            }
                        }

                        if (conn->body_received >= conn->content_length) {
                            close(conn->file_fd);
                            conn->file_fd = -1;
                            string body = "Uploaded\n";
                            string hdr = make_response_header(201, "Created", body.size(), false, "text/plain");
                            send(conn->fd, hdr.c_str(), hdr.size(), 0);
                            send(conn->fd, body.c_str(), body.size(), 0);
                            close(conn->fd);
                            return;
                        }
                        // if not finished, return and wait for next epoll event
                    } // end POST
                }); // end enqueue
            } // else non-sfd
        } // for events

        // optional: prune closed connections from conns map to avoid leak
        // (keep simple: remove any fd that appears closed)
        {
            lock_guard<mutex> lg(conns_mu);
            vector<int> to_erase;
            for (auto &p : conns) {
                // if descriptor is -1 or closed, we would detect via some flag; omitted for brevity
                // We'll skip aggressive pruning; OS will reclaim.
                (void)p;
            }
        }
    } // while true

    // cleanup (never reached in normal operation)
    close(sfd);
    close(epfd);
}
