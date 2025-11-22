// // // // src/server.cpp
// // // #include "server.h"
// // // #include "threadpool.h"
// // // #include "utils.h"
// // // #include "http_conn.h"

// // // #include <arpa/inet.h>
// // // #include <fcntl.h>
// // // #include <sys/epoll.h>
// // // #include <sys/socket.h>
// // // #include <sys/sendfile.h>
// // // #include <unistd.h>
// // // #include <netinet/in.h>
// // // #include <signal.h>
// // // #include <string.h>

// // // #include <dirent.h>
// // // #include <sys/types.h>
// // // #include <sys/stat.h>

// // // #include <iostream>
// // // #include <unordered_map>
// // // #include <memory>
// // // #include <mutex>
// // // #include <vector>
// // // #include <chrono>
// // // #include <thread>

// // // using namespace std;

// // // // Simple HTTP response header helper
// // // static std::string make_response_header(int code, const std::string &status,
// // //                                         size_t content_length, bool keep_alive = false) {
// // //     std::string h = "HTTP/1.1 " + std::to_string(code) + " " + status + "\r\n";
// // //     h += "Server: hp-file-server/1.0\r\n";
// // //     h += "Content-Length: " + std::to_string(content_length) + "\r\n";
// // //     if (keep_alive) h += "Connection: keep-alive\r\n";
// // //     else h += "Connection: close\r\n";
// // //     h += "\r\n";
// // //     return h;
// // // }

// // // Server::Server(int port_, const string& root_, int threads)
// // //     : port(port_), root(root_), thread_count(threads) {}

// // // int Server::set_nonblocking(int fd) {
// // //     int flags = fcntl(fd, F_GETFL, 0);
// // //     if (flags == -1) return -1;
// // //     return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
// // // }

// // // int Server::create_and_bind() {
// // //     int fd = socket(AF_INET, SOCK_STREAM, 0);
// // //     if (fd == -1) { perror("socket"); return -1; }
// // //     int yes = 1;
// // //     setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

// // //     sockaddr_in addr{};
// // //     addr.sin_family = AF_INET;
// // //     addr.sin_addr.s_addr = INADDR_ANY;
// // //     addr.sin_port = htons(port);

// // //     if (bind(fd, (sockaddr*)&addr, sizeof(addr)) == -1) {
// // //         perror("bind");
// // //         close(fd);
// // //         return -1;
// // //     }
// // //     return fd;
// // // }

// // // void Server::run() {
// // //     signal(SIGPIPE, SIG_IGN);
// // //     // ensure root exists
// // //     mkdir(root.c_str(), 0755);

// // //     int sfd = create_and_bind();
// // //     if (sfd < 0) return;
// // //     if (set_nonblocking(sfd) == -1) { perror("set_nonblocking"); close(sfd); return; }
// // //     if (listen(sfd, SOMAXCONN) == -1) { perror("listen"); close(sfd); return; }

// // //     int epfd = epoll_create1(0);
// // //     if (epfd == -1) { perror("epoll_create1"); close(sfd); return; }
// // //     epoll_event ev{};
// // //     ev.events = EPOLLIN;
// // //     ev.data.fd = sfd;

// // //     if (epoll_ctl(epfd, EPOLL_CTL_ADD, sfd, &ev) == -1) { perror("epoll_ctl add sfd"); close(sfd); close(epfd); return; }

// // //     ThreadPool pool(thread_count);
// // //     unordered_map<int, shared_ptr<HttpConn>> conns;
// // //     mutex conns_mu;

// // //     const int MAX_EVENTS = 1024;
// // //     vector<epoll_event> events(MAX_EVENTS);

// // //     cout << "Listening on port " << port << " root=" << root << " threads=" << thread_count << "\n";

// // //     while (true) {
// // //         int n = epoll_wait(epfd, events.data(), MAX_EVENTS, 1000);
// // //         if (n < 0) {
// // //             if (errno == EINTR) continue;
// // //             perror("epoll_wait");
// // //             break;
// // //         }
// // //         for (int i = 0; i < n; ++i) {
// // //             if (events[i].data.fd == sfd) {
// // //                 // accept loop
// // //                 while (true) {
// // //                     sockaddr_in in_addr{};
// // //                     socklen_t in_len = sizeof(in_addr);
// // //                     int infd = accept(sfd, (sockaddr*)&in_addr, &in_len);
// // //                     if (infd == -1) {
// // //                         if (errno == EAGAIN || errno == EWOULDBLOCK) break;
// // //                         perror("accept");
// // //                         break;
// // //                     }
// // //                     if (set_nonblocking(infd) == -1) { perror("set_nonblocking conn"); close(infd); continue; }
// // //                     epoll_event e{};
// // //                     e.events = EPOLLIN | EPOLLET;
// // //                     e.data.fd = infd;
// // //                     if (epoll_ctl(epfd, EPOLL_CTL_ADD, infd, &e) == -1) { perror("epoll_ctl add conn"); close(infd); continue; }
// // //                     auto conn = make_shared<HttpConn>(infd);
// // //                     {
// // //                         lock_guard<mutex> lg(conns_mu);
// // //                         conns[infd] = conn;
// // //                     }
// // //                 }
// // //             } else {
// // //                 int fd = events[i].data.fd;
// // //                 shared_ptr<HttpConn> conn;
// // //                 {
// // //                     lock_guard<mutex> lg(conns_mu);
// // //                     auto it = conns.find(fd);
// // //                     if (it == conns.end()) continue;
// // //                     conn = it->second;
// // //                 }
// // //                 // schedule read/handle to thread pool
// // //                 pool.enqueue([conn, this](){
// // //                     char buf[8192];
// // //                     while (true) {
// // //                         ssize_t n = recv(conn->fd, buf, sizeof(buf), 0);
// // //                         if (n > 0) conn->inbuf.append(buf, n);
// // //                         else if (n == 0) { close(conn->fd); return; }
// // //                         else {
// // //                             if (errno == EAGAIN || errno == EWOULDBLOCK) break;
// // //                             // error
// // //                             close(conn->fd);
// // //                             return;
// // //                         }
// // //                     }

// // //                     // parse header if not parsed
// // //                     if (!conn->header_parsed) {
// // //                         size_t pos = conn->inbuf.find("\r\n\r\n");
// // //                         if (pos == string::npos) return; // wait for full header
// // //                         string header = conn->inbuf.substr(0, pos + 4);
// // //                         conn->inbuf.erase(0, pos + 4);

// // //                         size_t sp1 = header.find(' ');
// // //                         size_t sp2 = header.find(' ', sp1 + 1);
// // //                         if (sp1 == string::npos || sp2 == string::npos) return;
// // //                         conn->method = header.substr(0, sp1);
// // //                         conn->uri = header.substr(sp1 + 1, sp2 - sp1 - 1);

// // //                         size_t cl = header.find("Content-Length:");
// // //                         if (cl != string::npos) {
// // //                             size_t eol = header.find("\r", cl);
// // //                             if (eol != string::npos) {
// // //                                 string val = header.substr(cl + strlen("Content-Length:"), eol - (cl + strlen("Content-Length:")));
// // //                                 // trim
// // //                                 val.erase(0, val.find_first_not_of(" \t"));
// // //                                 val.erase(val.find_last_not_of(" \t") + 1);
// // //                                 try {
// // //                                     conn->content_length = stoul(val);
// // //                                 } catch(...) { conn->content_length = 0; }
// // //                             }
// // //                         }
// // //                         conn->header_parsed = true;
// // //                     }

// // //                     // GET handler
// // //                     if (conn->method == "GET") {
// // //                         // root directory listing when requesting "/"
// // //                         if (conn->uri == "/") {
// // //                           string index_path = root + "/index.html";

// // //                         if (!file_exists(index_path)) {
// // //                             string msg = "HTTP/1.1 404 Not Found\r\nContent-Length: 9\r\n\r\nNot Found";
// // //                             send(conn->fd, msg.c_str(), msg.size(), 0);
// // //                             close(conn->fd);
// // //                             return;
// // //                         }
// // //                                              int fd = open(index_path.c_str(), O_RDONLY);
// // //                         off_t size = file_size(index_path);
// // //                                              string hdr = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nContent-Length: " + to_string(size) + "\r\n\r\n";
// // //                         send(conn->fd, hdr.c_str(), hdr.size(), 0);
// // //                                              off_t off = 0;
// // //                         while (off < size) sendfile(conn->fd, fd, &off, size - off);
// // //                                              close(fd);
// // //                         close(conn->fd);
// // //                         return;}                       

// // //                         string path = sanitize_path(root, conn->uri);
// // //                         if (!file_exists(path)) {
// // //                             string body = "Not Found\n";
// // //                             string hdr = make_response_header(404, "Not Found", body.size(), false);
// // //                             send(conn->fd, hdr.c_str(), hdr.size(), 0);
// // //                             send(conn->fd, body.c_str(), body.size(), 0);
// // //                             close(conn->fd);
// // //                             return;
// // //                         }

// // //                         int infd = open(path.c_str(), O_RDONLY);
// // //                         if (infd < 0) {
// // //                             string body = "Internal Server Error\n";
// // //                             string hdr = make_response_header(500, "Internal Server Error", body.size(), false);
// // //                             send(conn->fd, hdr.c_str(), hdr.size(), 0);
// // //                             send(conn->fd, body.c_str(), body.size(), 0);
// // //                             close(conn->fd);
// // //                             return;
// // //                         }
// // //                         off_t fsize = file_size(path);
// // //                         if (fsize < 0) fsize = 0;
// // //                         string hdr = make_response_header(200, "OK", fsize, false);
// // //                         send(conn->fd, hdr.c_str(), hdr.size(), 0);

// // //                         off_t offset = 0;
// // //                         while (offset < fsize) {
// // //                             ssize_t s = sendfile(conn->fd, infd, &offset, (size_t)(fsize - offset));
// // //                             if (s <= 0) {
// // //                                 if (errno == EAGAIN || errno == EINTR) {
// // //                                     std::this_thread::sleep_for(std::chrono::milliseconds(1));
// // //                                     continue;
// // //                                 }
// // //                                 break;
// // //                             }
// // //                         }
// // //                         close(infd);
// // //                         close(conn->fd);
// // //                         return;
// // //                     }

// // //                     // POST handler (upload)
// // //                     if (conn->method == "POST") {
// // //                         if (conn->tmp_upload_path.empty()) {
// // //                             // create target path: allow using the URI last segment as filename
// // //                             string filename = conn->uri;
// // //                             // strip leading '/'
// // //                             if (!filename.empty() && filename.front() == '/') filename.erase(0,1);
// // //                             if (filename.empty()) {
// // //                                 // fallback: use generated name
// // //                                 filename = "upload_" + to_string(conn->fd) + "_" + to_string(time(nullptr));
// // //                             }
// // //                             conn->tmp_upload_path = this->root + "/" + filename;
// // //                             conn->file_fd = open(conn->tmp_upload_path.c_str(), O_CREAT | O_WRONLY | O_TRUNC, 0644);
// // //                             if (conn->file_fd < 0) {
// // //                                 string body = "Internal Server Error\n";
// // //                                 string hdr = make_response_header(500, "Internal Server Error", body.size(), false);
// // //                                 send(conn->fd, hdr.c_str(), hdr.size(), 0);
// // //                                 send(conn->fd, body.c_str(), body.size(), 0);
// // //                                 close(conn->fd);
// // //                                 return;
// // //                             }
// // //                         }

// // //                         size_t to_write = min(conn->content_length - conn->body_received, conn->inbuf.size());
// // //                         if (to_write > 0) {
// // //                             ssize_t w = write(conn->file_fd, conn->inbuf.data(), to_write);
// // //                             (void)w; // ignore return (we wrote but silence warning)
// // //                             conn->body_received += to_write;
// // //                             conn->inbuf.erase(0, to_write);
// // //                         }

// // //                         // read remaining body directly from socket
// // //                         while (conn->body_received < conn->content_length) {
// // //                             char tmp[8192];
// // //                             ssize_t r = recv(conn->fd, tmp, sizeof(tmp), 0);
// // //                             if (r > 0) {
// // //                                 ssize_t w = write(conn->file_fd, tmp, r);
// // //                                 (void)w;
// // //                                 conn->body_received += r;
// // //                             } else if (r == 0) {
// // //                                 break;
// // //                             } else {
// // //                                 if (errno == EAGAIN || errno == EWOULDBLOCK) break;
// // //                                 if (errno == EINTR) continue;
// // //                                 perror("recv");
// // //                                 close(conn->file_fd);
// // //                                 close(conn->fd);
// // //                                 return;
// // //                             }
// // //                         }

// // //                         if (conn->body_received >= conn->content_length) {
// // //                             close(conn->file_fd);
// // //                             conn->file_fd = -1;
// // //                             string body = "Uploaded\n";
// // //                             string hdr = make_response_header(201, "Created", body.size(), false);
// // //                             send(conn->fd, hdr.c_str(), hdr.size(), 0);
// // //                             send(conn->fd, body.c_str(), body.size(), 0);
// // //                             close(conn->fd);
// // //                             return;
// // //                         }
// // //                         // if not finished, simply return and wait for next epoll event to continue (edge-triggered)
// // //                     }
// // //                 }); // end enqueue
// // //             } // else non-sfd
// // //         } // for events
// // //         // optional: prune closed connections from conns map (omitted for brevity)
// // //     } // while true

// // //     // cleanup (never reached in this simple server)
// // //     close(sfd);
// // //     close(epfd);
// // // }
// // // src/server.cpp
// // // #include "server.h"
// // // #include "threadpool.h"
// // // #include "utils.h"
// // // #include "http_conn.h"

// // // #include <arpa/inet.h>
// // // #include <fcntl.h>
// // // #include <sys/epoll.h>
// // // #include <sys/socket.h>
// // // #include <sys/sendfile.h>
// // // #include <unistd.h>
// // // #include <netinet/in.h>
// // // #include <signal.h>
// // // #include <string.h>

// // // #include <dirent.h>
// // // #include <sys/types.h>
// // // #include <sys/stat.h>

// // // #include <iostream>
// // // #include <unordered_map>
// // // #include <memory>
// // // #include <mutex>
// // // #include <vector>
// // // #include <chrono>
// // // #include <thread>

// // // using namespace std;

// // // // Simple HTTP response header helper
// // // static std::string make_response_header(int code, const std::string &status,
// // //                                         size_t content_length, bool keep_alive = false,
// // //                                         const std::string &content_type = "text/plain") {
// // //     std::string h = "HTTP/1.1 " + std::to_string(code) + " " + status + "\r\n";
// // //     h += "Server: hp-file-server/1.0\r\n";
// // //     h += "Content-Type: " + content_type + "\r\n";
// // //     h += "Content-Length: " + std::to_string(content_length) + "\r\n";
// // //     if (keep_alive) h += "Connection: keep-alive\r\n";
// // //     else h += "Connection: close\r\n";
// // //     h += "\r\n";
// // //     return h;
// // // }

// // // Server::Server(int port_, const string& root_, int threads)
// // //     : port(port_), root(root_), thread_count(threads) {}

// // // int Server::set_nonblocking(int fd) {
// // //     int flags = fcntl(fd, F_GETFL, 0);
// // //     if (flags == -1) return -1;
// // //     return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
// // // }

// // // int Server::create_and_bind() {
// // //     int fd = socket(AF_INET, SOCK_STREAM, 0);
// // //     if (fd == -1) { perror("socket"); return -1; }
// // //     int yes = 1;
// // //     setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

// // //     sockaddr_in addr{};
// // //     addr.sin_family = AF_INET;
// // //     addr.sin_addr.s_addr = INADDR_ANY;
// // //     addr.sin_port = htons(port);

// // //     if (bind(fd, (sockaddr*)&addr, sizeof(addr)) == -1) {
// // //         perror("bind");
// // //         close(fd);
// // //         return -1;
// // //     }
// // //     return fd;
// // // }

// // // void Server::run() {
// // //     signal(SIGPIPE, SIG_IGN);
// // //     // ensure root exists
// // //     mkdir(root.c_str(), 0755);

// // //     int sfd = create_and_bind();
// // //     if (sfd < 0) return;
// // //     if (set_nonblocking(sfd) == -1) { perror("set_nonblocking"); close(sfd); return; }
// // //     if (listen(sfd, SOMAXCONN) == -1) { perror("listen"); close(sfd); return; }

// // //     int epfd = epoll_create1(0);
// // //     if (epfd == -1) { perror("epoll_create1"); close(sfd); return; }
// // //     epoll_event ev{};
// // //     ev.events = EPOLLIN;
// // //     ev.data.fd = sfd;

// // //     if (epoll_ctl(epfd, EPOLL_CTL_ADD, sfd, &ev) == -1) { perror("epoll_ctl add sfd"); close(sfd); close(epfd); return; }

// // //     ThreadPool pool(thread_count);
// // //     unordered_map<int, shared_ptr<HttpConn>> conns;
// // //     mutex conns_mu;

// // //     const int MAX_EVENTS = 1024;
// // //     vector<epoll_event> events(MAX_EVENTS);

// // //     cout << "Listening on port " << port << " root=" << root << " threads=" << thread_count << "\n";

// // //     while (true) {
// // //         int n = epoll_wait(epfd, events.data(), MAX_EVENTS, 1000);
// // //         if (n < 0) {
// // //             if (errno == EINTR) continue;
// // //             perror("epoll_wait");
// // //             break;
// // //         }
// // //         for (int i = 0; i < n; ++i) {
// // //             if (events[i].data.fd == sfd) {
// // //                 // accept loop
// // //                 while (true) {
// // //                     sockaddr_in in_addr{};
// // //                     socklen_t in_len = sizeof(in_addr);
// // //                     int infd = accept(sfd, (sockaddr*)&in_addr, &in_len);
// // //                     if (infd == -1) {
// // //                         if (errno == EAGAIN || errno == EWOULDBLOCK) break;
// // //                         perror("accept");
// // //                         break;
// // //                     }
// // //                     if (set_nonblocking(infd) == -1) { perror("set_nonblocking conn"); close(infd); continue; }
// // //                     epoll_event e{};
// // //                     e.events = EPOLLIN | EPOLLET;
// // //                     e.data.fd = infd;
// // //                     if (epoll_ctl(epfd, EPOLL_CTL_ADD, infd, &e) == -1) { perror("epoll_ctl add conn"); close(infd); continue; }
// // //                     auto conn = make_shared<HttpConn>(infd);
// // //                     {
// // //                         lock_guard<mutex> lg(conns_mu);
// // //                         conns[infd] = conn;
// // //                     }
// // //                 }
// // //             } else {
// // //                 int fd = events[i].data.fd;
// // //                 shared_ptr<HttpConn> conn;
// // //                 {
// // //                     lock_guard<mutex> lg(conns_mu);
// // //                     auto it = conns.find(fd);
// // //                     if (it == conns.end()) continue;
// // //                     conn = it->second;
// // //                 }
// // //                 // schedule read/handle to thread pool
// // //                 pool.enqueue([conn, this](){
// // //                     char buf[8192];
// // //                     while (true) {
// // //                         ssize_t n = recv(conn->fd, buf, sizeof(buf), 0);
// // //                         if (n > 0) conn->inbuf.append(buf, n);
// // //                         else if (n == 0) { close(conn->fd); return; }
// // //                         else {
// // //                             if (errno == EAGAIN || errno == EWOULDBLOCK) break;
// // //                             // error
// // //                             close(conn->fd);
// // //                             return;
// // //                         }
// // //                     }

// // //                     // parse header if not parsed
// // //                     string header_full; // keep local copy in order to inspect Accept and other headers
// // //                     if (!conn->header_parsed) {
// // //                         size_t pos = conn->inbuf.find("\r\n\r\n");
// // //                         if (pos == string::npos) return; // wait for full header
// // //                         header_full = conn->inbuf.substr(0, pos + 4);
// // //                         conn->inbuf.erase(0, pos + 4);

// // //                         size_t sp1 = header_full.find(' ');
// // //                         size_t sp2 = header_full.find(' ', sp1 + 1);
// // //                         if (sp1 == string::npos || sp2 == string::npos) return;
// // //                         conn->method = header_full.substr(0, sp1);
// // //                         conn->uri = header_full.substr(sp1 + 1, sp2 - sp1 - 1);

// // //                         size_t cl = header_full.find("Content-Length:");
// // //                         if (cl != string::npos) {
// // //                             size_t eol = header_full.find("\r", cl);
// // //                             if (eol != string::npos) {
// // //                                 string val = header_full.substr(cl + strlen("Content-Length:"), eol - (cl + strlen("Content-Length:")));
// // //                                 // trim
// // //                                 val.erase(0, val.find_first_not_of(" \t"));
// // //                                 val.erase(val.find_last_not_of(" \t") + 1);
// // //                                 try {
// // //                                     conn->content_length = stoul(val);
// // //                                 } catch(...) { conn->content_length = 0; }
// // //                             }
// // //                         }

// // //                         conn->header_parsed = true;
// // //                     }

// // //                     // For root "/" we may need to inspect Accept header; if header_full is empty (because header was parsed earlier), try to extract from inbuf? Usually header_full exists here.
// // //                     // But to be safe, try to re-find Accept in the original header portion stored earlier (we used header_full variable above)
// // //                     // Determine whether client prefers HTML
// // //                     bool client_accepts_html = false;
// // //                     if (!header_full.empty()) {
// // //                         size_t acc = header_full.find("Accept:");
// // //                         if (acc != string::npos) {
// // //                             size_t eol = header_full.find("\r", acc);
// // //                             string accval = header_full.substr(acc + 7, (eol==string::npos? string::npos : eol - (acc+7)));
// // //                             // lowercase check
// // //                             for (auto &c : accval) c = tolower((unsigned char)c);
// // //                             if (accval.find("text/html") != string::npos || accval.find("*/*") != string::npos) {
// // //                                 client_accepts_html = true;
// // //                             }
// // //                         }
// // //                     }

// // //                     // GET handler
// // //                     if (conn->method == "GET") {
// // //                         // root directory special behavior:
// // //                         if (conn->uri == "/") {
// // //                             // If the client accepts HTML, serve index.html so the browser shows UI.
// // //                             // If the client doesn't advertise Accept: text/html (like fetch from JS expecting file list),
// // //                             // return plain listing (one file per line) — so existing frontend that does fetch('/') keeps working.
// // //                             string index_path = root + "/index.html";
// // //                             if (client_accepts_html) {
// // //                                 if (!file_exists(index_path)) {
// // //                                     string msg = "HTTP/1.1 404 Not Found\r\nContent-Length: 9\r\n\r\nNot Found";
// // //                                     send(conn->fd, msg.c_str(), msg.size(), 0);
// // //                                     close(conn->fd);
// // //                                     return;
// // //                                 }
// // //                                 int fd = open(index_path.c_str(), O_RDONLY);
// // //                                 if (fd < 0) {
// // //                                     string body = "Internal Server Error\n";
// // //                                     string hdr = make_response_header(500, "Internal Server Error", body.size(), false, "text/plain");
// // //                                     send(conn->fd, hdr.c_str(), hdr.size(), 0);
// // //                                     send(conn->fd, body.c_str(), body.size(), 0);
// // //                                     close(conn->fd);
// // //                                     return;
// // //                                 }
// // //                                 off_t size = file_size(index_path);
// // //                                 string hdr = make_response_header(200, "OK", (size_t)size, false, "text/html");
// // //                                 send(conn->fd, hdr.c_str(), hdr.size(), 0);
// // //                                 off_t off = 0;
// // //                                 while (off < size) sendfile(conn->fd, fd, &off, size - off);
// // //                                 close(fd);
// // //                                 close(conn->fd);
// // //                                 return;
// // //                             } else {
// // //                                 // return simple plain-text listing (one filename per line) for programmatic clients
// // //                                 DIR *d = opendir(root.c_str());
// // //                                 if (!d) {
// // //                                     string body = "Internal Server Error\n";
// // //                                     string hdr = make_response_header(500, "Internal Server Error", body.size(), false, "text/plain");
// // //                                     send(conn->fd, hdr.c_str(), hdr.size(), 0);
// // //                                     send(conn->fd, body.c_str(), body.size(), 0);
// // //                                     close(conn->fd);
// // //                                     return;
// // //                                 }
// // //                                 string listing;
// // //                                 struct dirent *e;
// // //                                 while ((e = readdir(d)) != NULL) {
// // //                                     if (e->d_type == DT_REG) {
// // //                                         listing += e->d_name;
// // //                                         listing += "\n";
// // //                                     }
// // //                                 }
// // //                                 closedir(d);
// // //                                 string hdr = make_response_header(200, "OK", listing.size(), false, "text/plain");
// // //                                 send(conn->fd, hdr.c_str(), hdr.size(), 0);
// // //                                 send(conn->fd, listing.c_str(), listing.size(), 0);
// // //                                 close(conn->fd);
// // //                                 return;
// // //                             }
// // //                         }

// // //                         // for any other URI, treat as file download
// // //                         string path = sanitize_path(root, conn->uri);
// // //                         if (!file_exists(path)) {
// // //                             string body = "Not Found\n";
// // //                             string hdr = make_response_header(404, "Not Found", body.size(), false, "text/plain");
// // //                             send(conn->fd, hdr.c_str(), hdr.size(), 0);
// // //                             send(conn->fd, body.c_str(), body.size(), 0);
// // //                             close(conn->fd);
// // //                             return;
// // //                         }

// // //                         int infd = open(path.c_str(), O_RDONLY);
// // //                         if (infd < 0) {
// // //                             string body = "Internal Server Error\n";
// // //                             string hdr = make_response_header(500, "Internal Server Error", body.size(), false, "text/plain");
// // //                             send(conn->fd, hdr.c_str(), hdr.size(), 0);
// // //                             send(conn->fd, body.c_str(), body.size(), 0);
// // //                             close(conn->fd);
// // //                             return;
// // //                         }
// // //                         off_t fsize = file_size(path);
// // //                         if (fsize < 0) fsize = 0;

// // //                         // determine content-type by extension (simple)
// // //                         string content_type = "application/octet-stream";
// // //                         auto dot = path.find_last_of('.');
// // //                         if (dot != string::npos) {
// // //                             string ext = path.substr(dot+1);
// // //                             if (ext == "html" || ext == "htm") content_type = "text/html";
// // //                             else if (ext == "js") content_type = "application/javascript";
// // //                             else if (ext == "css") content_type = "text/css";
// // //                             else if (ext == "json") content_type = "application/json";
// // //                             else if (ext == "png") content_type = "image/png";
// // //                             else if (ext == "jpg" || ext == "jpeg") content_type = "image/jpeg";
// // //                             else if (ext == "pdf") content_type = "application/pdf";
// // //                             else if (ext == "txt") content_type = "text/plain";
// // //                         }

// // //                         string hdr = make_response_header(200, "OK", (size_t)fsize, false, content_type);
// // //                         send(conn->fd, hdr.c_str(), hdr.size(), 0);

// // //                         off_t offset = 0;
// // //                         while (offset < fsize) {
// // //                             ssize_t s = sendfile(conn->fd, infd, &offset, (size_t)(fsize - offset));
// // //                             if (s <= 0) {
// // //                                 if (errno == EAGAIN || errno == EINTR) {
// // //                                     this_thread::sleep_for(chrono::milliseconds(1));
// // //                                     continue;
// // //                                 }
// // //                                 break;
// // //                             }
// // //                         }
// // //                         close(infd);
// // //                         close(conn->fd);
// // //                         return;
// // //                     } // end GET

// // //                     // POST handler (upload)
// // //                     if (conn->method == "POST") {
// // //                         if (conn->tmp_upload_path.empty()) {
// // //                             // create target path: allow using the URI last segment as filename
// // //                             string filename = conn->uri;
// // //                             // strip leading '/'
// // //                             if (!filename.empty() && filename.front() == '/') filename.erase(0,1);
// // //                             if (filename.empty()) {
// // //                                 // fallback: use generated name
// // //                                 filename = "upload_" + to_string(conn->fd) + "_" + to_string(time(nullptr));
// // //                             }
// // //                             conn->tmp_upload_path = this->root + "/" + filename;
// // //                             conn->file_fd = open(conn->tmp_upload_path.c_str(), O_CREAT | O_WRONLY | O_TRUNC, 0644);
// // //                             if (conn->file_fd < 0) {
// // //                                 string body = "Internal Server Error\n";
// // //                                 string hdr = make_response_header(500, "Internal Server Error", body.size(), false, "text/plain");
// // //                                 send(conn->fd, hdr.c_str(), hdr.size(), 0);
// // //                                 send(conn->fd, body.c_str(), body.size(), 0);
// // //                                 close(conn->fd);
// // //                                 return;
// // //                             }
// // //                         }

// // //                         size_t to_write = min(conn->content_length - conn->body_received, conn->inbuf.size());
// // //                         if (to_write > 0) {
// // //                             ssize_t w = write(conn->file_fd, conn->inbuf.data(), to_write);
// // //                             (void)w; // ignore return (we wrote but silence warning)
// // //                             conn->body_received += to_write;
// // //                             conn->inbuf.erase(0, to_write);
// // //                         }

// // //                         // read remaining body directly from socket
// // //                         while (conn->body_received < conn->content_length) {
// // //                             char tmp[8192];
// // //                             ssize_t r = recv(conn->fd, tmp, sizeof(tmp), 0);
// // //                             if (r > 0) {
// // //                                 ssize_t w = write(conn->file_fd, tmp, r);
// // //                                 (void)w;
// // //                                 conn->body_received += r;
// // //                             } else if (r == 0) {
// // //                                 break;
// // //                             } else {
// // //                                 if (errno == EAGAIN || errno == EWOULDBLOCK) break;
// // //                                 if (errno == EINTR) continue;
// // //                                 perror("recv");
// // //                                 close(conn->file_fd);
// // //                                 close(conn->fd);
// // //                                 return;
// // //                             }
// // //                         }

// // //                         if (conn->body_received >= conn->content_length) {
// // //                             close(conn->file_fd);
// // //                             conn->file_fd = -1;
// // //                             string body = "Uploaded\n";
// // //                             string hdr = make_response_header(201, "Created", body.size(), false, "text/plain");
// // //                             send(conn->fd, hdr.c_str(), hdr.size(), 0);
// // //                             send(conn->fd, body.c_str(), body.size(), 0);
// // //                             close(conn->fd);
// // //                             return;
// // //                         }
// // //                         // if not finished, simply return and wait for next epoll event to continue (edge-triggered)
// // //                     } // end POST
// // //                 }); // end enqueue
// // //             } // else non-sfd
// // //         } // for events
// // //         // optional: prune closed connections from conns map (omitted for brevity)
// // //     } // while true

// // //     // cleanup (never reached in this simple server)
// // //     close(sfd);
// // //     close(epfd);
// // // }

// // // latest changes 
// // // src/server.cpp
// // #include "server.h"
// // #include "threadpool.h"
// // #include "utils.h"
// // #include "http_conn.h"

// // #include <arpa/inet.h>
// // #include <fcntl.h>
// // #include <sys/epoll.h>
// // #include <sys/socket.h>
// // #include <sys/sendfile.h>
// // #include <unistd.h>
// // #include <netinet/in.h>
// // #include <signal.h>
// // #include <string.h>

// // #include <dirent.h>
// // #include <sys/types.h>
// // #include <sys/stat.h>

// // #include <iostream>
// // #include <unordered_map>
// // #include <memory>
// // #include <mutex>
// // #include <vector>
// // #include <chrono>
// // #include <thread>
// // #include <algorithm>
// // #include <cctype>

// // using namespace std;

// // // Simple HTTP response header helper
// // static std::string make_response_header(int code, const std::string &status,
// //                                         size_t content_length, bool keep_alive = false,
// //                                         const std::string &content_type = "text/plain") {
// //     std::string h = "HTTP/1.1 " + std::to_string(code) + " " + status + "\r\n";
// //     h += "Server: hp-file-server/1.0\r\n";
// //     h += "Content-Type: " + content_type + "\r\n";
// //     h += "Content-Length: " + std::to_string(content_length) + "\r\n";
// //     if (keep_alive) h += "Connection: keep-alive\r\n";
// //     else h += "Connection: close\r\n";
// //     h += "\r\n";
// //     return h;
// // }

// // // Simple helper to map extension -> mime type
// // static string guess_mime(const string &path) {
// //     auto dot = path.find_last_of('.');
// //     if (dot == string::npos) return "application/octet-stream";
// //     string ext = path.substr(dot + 1);
// //     for (auto &c : ext) c = tolower((unsigned char)c);
// //     if (ext == "html" || ext == "htm") return "text/html";
// //     if (ext == "js") return "application/javascript";
// //     if (ext == "css") return "text/css";
// //     if (ext == "json") return "application/json";
// //     if (ext == "png") return "image/png";
// //     if (ext == "jpg" || ext == "jpeg") return "image/jpeg";
// //     if (ext == "gif") return "image/gif";
// //     if (ext == "pdf") return "application/pdf";
// //     if (ext == "txt") return "text/plain";
// //     return "application/octet-stream";
// // }

// // Server::Server(int port_, const string& root_, int threads)
// //     : port(port_), root(root_), thread_count(threads) {
// //     // normalize root path (remove trailing slash)
// //     if (!root.empty() && root.back() == '/') {
// //         root.pop_back();
// //     }
// // }

// // int Server::set_nonblocking(int fd) {
// //     int flags = fcntl(fd, F_GETFL, 0);
// //     if (flags == -1) return -1;
// //     return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
// // }

// // int Server::create_and_bind() {
// //     int fd = socket(AF_INET, SOCK_STREAM, 0);
// //     if (fd == -1) { perror("socket"); return -1; }
// //     int yes = 1;
// //     setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

// //     sockaddr_in addr{};
// //     addr.sin_family = AF_INET;
// //     addr.sin_addr.s_addr = INADDR_ANY;
// //     addr.sin_port = htons(port);

// //     if (bind(fd, (sockaddr*)&addr, sizeof(addr)) == -1) {
// //         perror("bind");
// //         close(fd);
// //         return -1;
// //     }
// //     return fd;
// // }

// // void Server::run() {
// //     // Ignore SIGPIPE - sockets may be closed by peers
// //     signal(SIGPIPE, SIG_IGN);

// //     // ensure root exists
// //     mkdir(root.c_str(), 0755);

// //     int sfd = create_and_bind();
// //     if (sfd < 0) return;

// //     if (set_nonblocking(sfd) == -1) { perror("set_nonblocking"); close(sfd); return; }
// //     if (listen(sfd, SOMAXCONN) == -1) { perror("listen"); close(sfd); return; }

// //     int epfd = epoll_create1(0);
// //     if (epfd == -1) { perror("epoll_create1"); close(sfd); return; }
// //     epoll_event ev{};
// //     ev.events = EPOLLIN;
// //     ev.data.fd = sfd;

// //     if (epoll_ctl(epfd, EPOLL_CTL_ADD, sfd, &ev) == -1) { perror("epoll_ctl add sfd"); close(sfd); close(epfd); return; }

// //     ThreadPool pool(thread_count);
// //     unordered_map<int, shared_ptr<HttpConn>> conns;
// //     mutex conns_mu;

// //     const int MAX_EVENTS = 1024;
// //     vector<epoll_event> events(MAX_EVENTS);

// //     cout << "Listening on port " << port << " root=" << root << " threads=" << thread_count << "\n";

// //     while (true) {
// //         int n = epoll_wait(epfd, events.data(), MAX_EVENTS, 1000);
// //         if (n < 0) {
// //             if (errno == EINTR) continue;
// //             perror("epoll_wait");
// //             break;
// //         }

// //         for (int i = 0; i < n; ++i) {
// //             if (events[i].data.fd == sfd) {
// //                 // accept loop (edge-triggered)
// //                 while (true) {
// //                     sockaddr_in in_addr{};
// //                     socklen_t in_len = sizeof(in_addr);
// //                     int infd = accept(sfd, (sockaddr*)&in_addr, &in_len);
// //                     if (infd == -1) {
// //                         if (errno == EAGAIN || errno == EWOULDBLOCK) break;
// //                         perror("accept");
// //                         break;
// //                     }
// //                     if (set_nonblocking(infd) == -1) { perror("set_nonblocking conn"); close(infd); continue; }
// //                     epoll_event e{};
// //                     e.events = EPOLLIN | EPOLLET;
// //                     e.data.fd = infd;
// //                     if (epoll_ctl(epfd, EPOLL_CTL_ADD, infd, &e) == -1) { perror("epoll_ctl add conn"); close(infd); continue; }

// //                     auto conn = make_shared<HttpConn>(infd);
// //                     lock_guard<mutex> lg(conns_mu);
// //                     conns[infd] = conn;
// //                 }
// //             } else {
// //                 int fd = events[i].data.fd;
// //                 shared_ptr<HttpConn> conn;
// //                 {
// //                     lock_guard<mutex> lg(conns_mu);
// //                     auto it = conns.find(fd);
// //                     if (it == conns.end()) continue;
// //                     conn = it->second;
// //                 }

// //                 // schedule read/handle to thread pool
// //                 pool.enqueue([conn, this](){
// //                     char buf[8192];

// //                     // read all available data (edge-triggered)
// //                     while (true) {
// //                         ssize_t n = recv(conn->fd, buf, sizeof(buf), 0);
// //                         if (n > 0) {
// //                             conn->inbuf.append(buf, n);
// //                         } else if (n == 0) {
// //                             // client closed
// //                             close(conn->fd);
// //                             return;
// //                         } else {
// //                             if (errno == EAGAIN || errno == EWOULDBLOCK) break;
// //                             // unrecoverable error
// //                             close(conn->fd);
// //                             return;
// //                         }
// //                     }

// //                     // parse header if not parsed
// //                     string header_full;
// //                     if (!conn->header_parsed) {
// //                         size_t pos = conn->inbuf.find("\r\n\r\n");
// //                         if (pos == string::npos) return; // wait for full header
// //                         header_full = conn->inbuf.substr(0, pos + 4);
// //                         conn->inbuf.erase(0, pos + 4);

// //                         // parse method and uri
// //                         size_t sp1 = header_full.find(' ');
// //                         size_t sp2 = header_full.find(' ', sp1 + 1);
// //                         if (sp1 == string::npos || sp2 == string::npos) return;
// //                         conn->method = header_full.substr(0, sp1);
// //                         conn->uri = header_full.substr(sp1 + 1, sp2 - sp1 - 1);

// //                         // parse Content-Length if present
// //                         size_t cl = header_full.find("Content-Length:");
// //                         if (cl != string::npos) {
// //                             size_t eol = header_full.find("\r", cl);
// //                             if (eol != string::npos) {
// //                                 string val = header_full.substr(cl + strlen("Content-Length:"), eol - (cl + strlen("Content-Length:")));
// //                                 // trim
// //                                 val.erase(0, val.find_first_not_of(" \t"));
// //                                 val.erase(val.find_last_not_of(" \t") + 1);
// //                                 try {
// //                                     conn->content_length = stoul(val);
// //                                 } catch(...) { conn->content_length = 0; }
// //                             }
// //                         }

// //                         conn->header_parsed = true;
// //                     }

// //                     // ---------------- GET handler ----------------
// //                     if (conn->method == "GET") {
// //                         // ALWAYS serve index.html for "/"
// //                         if (conn->uri == "/") {
// //                             string index_path = root + "/index.html";
// //                             if (!file_exists(index_path)) {
// //                                 string msg = "HTTP/1.1 404 Not Found\r\nContent-Length: 9\r\n\r\nNot Found";
// //                                 send(conn->fd, msg.c_str(), msg.size(), 0);
// //                                 close(conn->fd);
// //                                 return;
// //                             }

// //                             int fd = open(index_path.c_str(), O_RDONLY);
// //                             if (fd < 0) {
// //                                 string body = "Internal Server Error\n";
// //                                 string hdr = make_response_header(500, "Internal Server Error", body.size(), false, "text/plain");
// //                                 send(conn->fd, hdr.c_str(), hdr.size(), 0);
// //                                 send(conn->fd, body.c_str(), body.size(), 0);
// //                                 close(conn->fd);
// //                                 return;
// //                             }

// //                             off_t size = file_size(index_path);
// //                             string hdr =
// //                                 "HTTP/1.1 200 OK\r\n"
// //                                 "Content-Type: text/html\r\n"
// //                                 "Cache-Control: no-cache, no-store, must-revalidate\r\n"
// //                                 "Content-Length: " + to_string((size_t)size) + "\r\n\r\n";

// //                             send(conn->fd, hdr.c_str(), hdr.size(), 0);
// //                             off_t off = 0;
// //                             while (off < size) {
// //                                 ssize_t s = sendfile(conn->fd, fd, &off, (size_t)(size - off));
// //                                 if (s <= 0) {
// //                                     if (errno == EINTR) continue;
// //                                     break;
// //                                 }
// //                             }
// //                             close(fd);
// //                             close(conn->fd);
// //                             return;
// //                         }

// //                         // Serve static file for other URIs
// //                         string path = sanitize_path(root, conn->uri);
// //                         if (!file_exists(path)) {
// //                             string body = "Not Found\n";
// //                             string hdr = make_response_header(404, "Not Found", body.size(), false, "text/plain");
// //                             send(conn->fd, hdr.c_str(), hdr.size(), 0);
// //                             send(conn->fd, body.c_str(), body.size(), 0);
// //                             close(conn->fd);
// //                             return;
// //                         }

// //                         int infd = open(path.c_str(), O_RDONLY);
// //                         if (infd < 0) {
// //                             string body = "Internal Server Error\n";
// //                             string hdr = make_response_header(500, "Internal Server Error", body.size(), false, "text/plain");
// //                             send(conn->fd, hdr.c_str(), hdr.size(), 0);
// //                             send(conn->fd, body.c_str(), body.size(), 0);
// //                             close(conn->fd);
// //                             return;
// //                         }

// //                         off_t fsize = file_size(path);
// //                         if (fsize < 0) fsize = 0;
// //                         string content_type = guess_mime(path);

// //                         string hdr = make_response_header(200, "OK", (size_t)fsize, false, content_type);
// //                         send(conn->fd, hdr.c_str(), hdr.size(), 0);

// //                         off_t offset = 0;
// //                         while (offset < fsize) {
// //                             ssize_t s = sendfile(conn->fd, infd, &offset, (size_t)(fsize - offset));
// //                             if (s <= 0) {
// //                                 if (errno == EAGAIN || errno == EINTR) {
// //                                     this_thread::sleep_for(chrono::milliseconds(1));
// //                                     continue;
// //                                 }
// //                                 break;
// //                             }
// //                         }
// //                         close(infd);
// //                         close(conn->fd);
// //                         return;
// //                     } // end GET

// //                     // ---------------- POST handler (upload) ----------------
// //                     if (conn->method == "POST") {
// //                         // lazily create target file descriptor if not already
// //                         if (conn->tmp_upload_path.empty()) {
// //                             // derive filename from URI (strip leading '/')
// //                             string filename = conn->uri;
// //                             if (!filename.empty() && filename.front() == '/') filename.erase(0,1);
// //                             if (filename.empty()) {
// //                                 filename = "upload_" + to_string(conn->fd) + "_" + to_string(time(nullptr));
// //                             }
// //                             // sanitize filename (simple): replace slashes
// //                             replace(filename.begin(), filename.end(), '/', '_');
// //                             conn->tmp_upload_path = this->root + "/" + filename;

// //                             conn->file_fd = open(conn->tmp_upload_path.c_str(), O_CREAT | O_WRONLY | O_TRUNC, 0644);
// //                             if (conn->file_fd < 0) {
// //                                 string body = "Internal Server Error\n";
// //                                 string hdr = make_response_header(500, "Internal Server Error", body.size(), false, "text/plain");
// //                                 send(conn->fd, hdr.c_str(), hdr.size(), 0);
// //                                 send(conn->fd, body.c_str(), body.size(), 0);
// //                                 close(conn->fd);
// //                                 return;
// //                             }
// //                         }

// //                         // first flush any existing buffer (may contain part or whole body)
// //                         size_t to_write = min(conn->content_length - conn->body_received, conn->inbuf.size());
// //                         if (to_write > 0) {
// //                             ssize_t w = write(conn->file_fd, conn->inbuf.data(), to_write);
// //                             (void)w;
// //                             conn->body_received += to_write;
// //                             conn->inbuf.erase(0, to_write);
// //                         }

// //                         // then read remaining body from socket
// //                         while (conn->body_received < conn->content_length) {
// //                             char tmp[8192];
// //                             ssize_t r = recv(conn->fd, tmp, sizeof(tmp), 0);
// //                             if (r > 0) {
// //                                 ssize_t w = write(conn->file_fd, tmp, r);
// //                                 (void)w;
// //                                 conn->body_received += r;
// //                             } else if (r == 0) {
// //                                 break;
// //                             } else {
// //                                 if (errno == EAGAIN || errno == EWOULDBLOCK) break;
// //                                 if (errno == EINTR) continue;
// //                                 perror("recv");
// //                                 close(conn->file_fd);
// //                                 close(conn->fd);
// //                                 return;
// //                             }
// //                         }

// //                         if (conn->body_received >= conn->content_length) {
// //                             close(conn->file_fd);
// //                             conn->file_fd = -1;
// //                             string body = "Uploaded\n";
// //                             string hdr = make_response_header(201, "Created", body.size(), false, "text/plain");
// //                             send(conn->fd, hdr.c_str(), hdr.size(), 0);
// //                             send(conn->fd, body.c_str(), body.size(), 0);
// //                             close(conn->fd);
// //                             return;
// //                         }
// //                         // if not finished, return and wait for next epoll event
// //                     } // end POST
// //                 }); // end enqueue
// //             } // else non-sfd
// //         } // for events

// //         // optional: prune closed connections from conns map to avoid leak
// //         // (keep simple: remove any fd that appears closed)
// //         {
// //             lock_guard<mutex> lg(conns_mu);
// //             vector<int> to_erase;
// //             for (auto &p : conns) {
// //                 // if descriptor is -1 or closed, we would detect via some flag; omitted for brevity
// //                 // We'll skip aggressive pruning; OS will reclaim.
// //                 (void)p;
// //             }
// //         }
// //     } // while true

// //     // cleanup (never reached in normal operation)
// //     close(sfd);
// //     close(epfd);
// // }
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
// #include <algorithm>
// #include <cctype>

// using namespace std;

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

// static string guess_mime(const string &path) {
//     auto dot = path.find_last_of('.');
//     if (dot == string::npos) return "application/octet-stream";
//     string ext = path.substr(dot + 1);
//     for (auto &c : ext) c = tolower((unsigned char)c);
//     if (ext == "html" || ext == "htm") return "text/html";
//     if (ext == "js") return "application/javascript";
//     if (ext == "css") return "text/css";
//     if (ext == "json") return "application/json";
//     if (ext == "png") return "image/png";
//     if (ext == "jpg" || ext == "jpeg") return "image/jpeg";
//     if (ext == "gif") return "image/gif";
//     if (ext == "pdf") return "application/pdf";
//     if (ext == "txt") return "text/plain";
//     return "application/octet-stream";
// }

// Server::Server(int port_, const string& root_, int threads)
//     : port(port_), root(root_), thread_count(threads) {
//     if (!root.empty() && root.back() == '/') root.pop_back();
// }

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
//                     lock_guard<mutex> lg(conns_mu);
//                     conns[infd] = conn;
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

//                 pool.enqueue([conn, this](){
//                     char buf[8192];
//                     while (true) {
//                         ssize_t n = recv(conn->fd, buf, sizeof(buf), 0);
//                         if (n > 0) conn->inbuf.append(buf, n);
//                         else if (n == 0) { close(conn->fd); return; }
//                         else {
//                             if (errno == EAGAIN || errno == EWOULDBLOCK) break;
//                             close(conn->fd);
//                             return;
//                         }
//                     }

//                     string header_full;
//                     if (!conn->header_parsed) {
//                         size_t pos = conn->inbuf.find("\r\n\r\n");
//                         if (pos == string::npos) return;
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
//                                 val.erase(0, val.find_first_not_of(" \t"));
//                                 val.erase(val.find_last_not_of(" \t") + 1);
//                                 try { conn->content_length = stoul(val); } catch(...) { conn->content_length = 0; }
//                             }
//                         }
//                         conn->header_parsed = true;
//                     }

//                     // GET handler
//                     if (conn->method == "GET") {
//                         if (conn->uri == "/") {
//                             string index_path = root + "/index.html";
//                             if (!file_exists(index_path)) {
//                                 string msg = "HTTP/1.1 404 Not Found\r\nContent-Length: 9\r\n\r\nNot Found";
//                                 send(conn->fd, msg.c_str(), msg.size(), 0);
//                                 close(conn->fd);
//                                 return;
//                             }

//                             int fd = open(index_path.c_str(), O_RDONLY);
//                             if (fd < 0) {
//                                 string body = "Internal Server Error\n";
//                                 string hdr = make_response_header(500, "Internal Server Error", body.size(), false, "text/plain");
//                                 send(conn->fd, hdr.c_str(), hdr.size(), 0);
//                                 send(conn->fd, body.c_str(), body.size(), 0);
//                                 close(conn->fd);
//                                 return;
//                             }

//                             off_t size = file_size(index_path);
//                             string hdr =
//                                 "HTTP/1.1 200 OK\r\n"
//                                 "Content-Type: text/html\r\n"
//                                 "Cache-Control: no-cache, no-store, must-revalidate\r\n"
//                                 "Content-Length: " + to_string((size_t)size) + "\r\n\r\n";

//                             send(conn->fd, hdr.c_str(), hdr.size(), 0);
//                             off_t off = 0;
//                             while (off < size) {
//                                 ssize_t s = sendfile(conn->fd, fd, &off, (size_t)(size - off));
//                                 if (s <= 0) {
//                                     if (errno == EINTR) continue;
//                                     break;
//                                 }
//                             }
//                             close(fd);
//                             close(conn->fd);
//                             return;
//                         }

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
//                         string content_type = guess_mime(path);

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
//                     }

//                     // POST handler
//                     if (conn->method == "POST") {
//                         if (conn->tmp_upload_path.empty()) {
//                             string filename = conn->uri;
//                             if (!filename.empty() && filename.front() == '/') filename.erase(0,1);
//                             if (filename.empty()) filename = "upload_" + to_string(conn->fd) + "_" + to_string(time(nullptr));
//                             replace(filename.begin(), filename.end(), '/', '_');
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
//                             (void)w;
//                             conn->body_received += to_write;
//                             conn->inbuf.erase(0, to_write);
//                         }

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
//                     }
//                 }); // end enqueue
//             }
//         } // for events
//     } // while

//     close(sfd);
// }
// server.cpp
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

#include <iostream>
#include <unordered_map>
#include <memory>
#include <mutex>
#include <vector>
#include <chrono>
#include <thread>
#include <sstream>
#include <dirent.h>
#include <sys/stat.h>
#include <errno.h>

using namespace std;

static std::string make_response_header(int code, const std::string &status,
                                        size_t content_length, const std::string &content_type="text/plain",
                                        bool keep_alive=false) {
    std::string h = "HTTP/1.1 " + std::to_string(code) + " " + status + "\r\n";
    h += "Server: hp-file-server/1.0\r\n";
    h += "Content-Type: " + content_type + "\r\n";
    h += "Content-Length: " + std::to_string(content_length) + "\r\n";
    if (keep_alive) h += "Connection: keep-alive\r\n";
    else h += "Connection: close\r\n";
    h += "\r\n";
    return h;
}

Server::Server(int port_, const string& root_, int threads, const string& api_token_)
    : port(port_), root(root_), thread_count(threads), api_token(api_token_) {}

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

static string list_files_json(const string &root) {
    DIR *d = opendir(root.c_str());
    if (!d) return "[]";
    struct dirent *e;
    std::ostringstream oss;
    oss << "[";
    bool first = true;
    while ((e = readdir(d)) != nullptr) {
        if (e->d_type != DT_REG) continue;
        string name = e->d_name;
        string full = root + "/" + name;
        struct stat st;
        if (stat(full.c_str(), &st) == 0) {
            if (!first) oss << ",";
            first = false;
            oss << "{";
            oss << "\"name\":\"";
            // escape quotes in name rudimentary (names unlikely to contain double quotes)
            for (char c: name) { if (c=='\"') oss << "\\\""; else oss << c; }
            oss << "\",\"size\":" << st.st_size << ",\"mtime\":" << st.st_mtime;
            oss << "}";
        }
    }
    closedir(d);
    oss << "]";
    return oss.str();
}

static bool token_ok(const string &server_token, const unordered_map<string,string>& headers) {
    if (server_token.empty()) return true; // no auth required
    auto it = headers.find("x-api-key");
    if (it == headers.end()) return false;
    return it->second == server_token;
}


// parse headers from raw header block
static unordered_map<string,string> parse_headers(const string &hdr_block) {
    unordered_map<string,string> m;
    std::istringstream iss(hdr_block);
    string line;
    // first line is request-line, skip
    getline(iss, line);
    while (getline(iss, line)) {
        if (line.size()<=1) continue;
        auto pos = line.find(':');
        if (pos==string::npos) continue;
        string k = line.substr(0,pos);
        string v = line.substr(pos+1);
        // trim
        while (!v.empty() && (v.front()==' '||v.front()=='\t')) v.erase(0,1);
        while (!k.empty() && (k.back()==' '||k.back()=='\t')) k.pop_back();
        std::transform(k.begin(), k.end(), k.begin(), ::tolower);
        m[k] = v;
    }
    return m;
}

void Server::run() {
    signal(SIGPIPE, SIG_IGN);
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
                // accept
                while (true) {
                    sockaddr_in in_addr{};
                    socklen_t in_len = sizeof(in_addr);
                    int infd = accept(sfd, (sockaddr*)&in_addr, &in_len);
                    if (infd < 0) {
                        if (errno==EAGAIN || errno==EWOULDBLOCK) break;
                        perror("accept");
                        break;
                    }
                    set_nonblocking(infd);
                    epoll_event e{}; e.events = EPOLLIN | EPOLLET; e.data.fd = infd;
                    epoll_ctl(epfd, EPOLL_CTL_ADD, infd, &e);
                    lock_guard<mutex> lg(conns_mu);
                    conns[infd] = make_shared<HttpConn>(infd);
                }
            } else {
                int fd = events[i].data.fd;
                shared_ptr<HttpConn> conn;
                {
                    lock_guard<mutex> lg(conns_mu);
                    if (conns.count(fd)) conn = conns[fd];
                }
                if (!conn) continue;

                pool.enqueue([conn, this, &conns, &conns_mu] {
                    char buff[8192];
                    while (true) {
                        ssize_t n = recv(conn->fd, buff, sizeof(buff), 0);
                        if (n > 0) conn->inbuf.append(buff, n);
                        else if (n == 0) { close(conn->fd); return; }
                        else {
                            if (errno == EAGAIN || errno == EWOULDBLOCK) break;
                            close(conn->fd); return;
                        }
                    }

                    if (!conn->header_parsed) {
                        size_t pos = conn->inbuf.find("\r\n\r\n");
                        if (pos == string::npos) return;
                        string header = conn->inbuf.substr(0, pos + 4);
                        conn->inbuf.erase(0, pos + 4);

                        size_t sp1 = header.find(' ');
                        size_t sp2 = header.find(' ', sp1 + 1);
                        if (sp1 == string::npos || sp2 == string::npos) return;
                        conn->method = header.substr(0, sp1);
                        conn->uri = header.substr(sp1 + 1, sp2 - sp1 - 1);
                        conn->headers = parse_headers(header);

                        size_t cl = header.find("Content-Length:");
                        if (cl != string::npos) {
                            size_t eol = header.find("\r", cl);
                            if (eol != string::npos) {
                                string val = header.substr(cl + strlen("Content-Length:"), eol - (cl + strlen("Content-Length:")));
                                // trim
                                val.erase(0, val.find_first_not_of(" \t"));
                                val.erase(val.find_last_not_of(" \t") + 1);
                                try { conn->content_length = stoul(val); } catch(...) { conn->content_length = 0; }
                            }
                        }
                        conn->header_parsed = true;
                    }

                    // ROUTING
                    // GET /
                    if (conn->method == "GET") {
                        if (conn->uri == "/") {
                            // Always serve index.html
                            string index_path = root + "/index.html";
                            if (!file_exists(index_path)) {
                                string msg = "HTTP/1.1 404 Not Found\r\nContent-Length: 9\r\n\r\nNot Found";
                                send(conn->fd, msg.c_str(), msg.size(), 0);
                                close(conn->fd);
                                return;
                            }
                            int idxfd = open(index_path.c_str(), O_RDONLY);
                            off_t size = file_size(index_path);
                            string hdr = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nContent-Length: " + to_string(size) + "\r\n\r\n";
                            send(conn->fd, hdr.c_str(), hdr.size(), 0);
                            off_t off = 0;
                            while (off < size) sendfile(conn->fd, idxfd, &off, size - off);
                            close(idxfd);
                            close(conn->fd);
                            return;
                        }

                        // JSON listing endpoint
                        if (conn->uri == "/api/files") {
                            string json = list_files_json(root);
                            string hdr = make_response_header(200, "OK", json.size(), "application/json", false);
                            send(conn->fd, hdr.c_str(), hdr.size(), 0);
                            send(conn->fd, json.c_str(), json.size(), 0);
                            close(conn->fd);
                            return;
                        }

                        // download raw file at /files/<name> or direct /<name>
                        string path;
                        if (conn->uri.rfind("/files/",0)==0) path = sanitize_path(root, conn->uri.substr(7));
                        else path = sanitize_path(root, conn->uri);

                        if (!file_exists(path)) {
                            string body = "Not Found\n";
                            string hdr = make_response_header(404, "Not Found", body.size(), "text/plain", false);
                            send(conn->fd, hdr.c_str(), hdr.size(), 0);
                            send(conn->fd, body.c_str(), body.size(), 0);
                            close(conn->fd);
                            return;
                        }

                        int infd = open(path.c_str(), O_RDONLY);
                        off_t fsize = file_size(path);
                        // Handle Range header
                        off_t start = 0, end = fsize - 1;
                        auto itRange = conn->headers.find("range");
                        if (itRange != conn->headers.end()) {
                            string r = itRange->second;
                            // Example: bytes=0-1023
                            auto eq = r.find('=');
                            if (eq != string::npos) {
                                string rng = r.substr(eq+1);
                                auto dash = rng.find('-');
                                if (dash != string::npos) {
                                    string s1 = rng.substr(0,dash);
                                    string s2 = rng.substr(dash+1);
                                    if (!s1.empty()) start = stoll(s1);
                                    if (!s2.empty()) end = stoll(s2);
                                    if (start < 0) start = 0;
                                    if (end >= fsize) end = fsize - 1;
                                }
                            }
                        }
                        if (start > end || start >= fsize) {
                            string body = "Requested Range Not Satisfiable\n";
                            string hdr = "HTTP/1.1 416 Range Not Satisfiable\r\nContent-Length: " + to_string(body.size()) + "\r\n\r\n";
                            send(conn->fd, hdr.c_str(), hdr.size(), 0);
                            send(conn->fd, body.c_str(), body.size(), 0);
                            close(infd);
                            close(conn->fd);
                            return;
                        }

                        // send appropriate headers
                        if (start==0 && end==fsize-1) {
                            string hdr = "HTTP/1.1 200 OK\r\nAccept-Ranges: bytes\r\nContent-Length: " + to_string(fsize) + "\r\nContent-Type: application/octet-stream\r\n\r\n";
                            send(conn->fd, hdr.c_str(), hdr.size(), 0);
                            off_t offset = 0;
                            while (offset < fsize) {
                                ssize_t s = sendfile(conn->fd, infd, &offset, fsize - offset);
                                if (s <= 0) break;
                            }
                        } else {
                            // partial
                            // seek to start using lseek on infd via off_t off = start; sendfile supports offset pointer
                            off_t off = start;
                            string hdr = "HTTP/1.1 206 Partial Content\r\nAccept-Ranges: bytes\r\nContent-Range: bytes " + to_string(start) + "-" + to_string(end) + "/" + to_string(fsize) + "\r\nContent-Length: " + to_string(end - start + 1) + "\r\nContent-Type: application/octet-stream\r\n\r\n";
                            send(conn->fd, hdr.c_str(), hdr.size(), 0);
                            while (off <= end) {
                                ssize_t s = sendfile(conn->fd, infd, &off, (size_t)(end - off + 1));
                                if (s <= 0) break;
                            }
                        }
                        close(infd);
                        close(conn->fd);
                        return;
                    } // GET end

                    // PUT to upload (resumable using Content-Range or simple full PUT)
                    if (conn->method == "PUT") {
                        // auth check
                        if (!token_ok(this->api_token, conn->headers)) {
                            string body = "Unauthorized\n";
                            string hdr = make_response_header(401, "Unauthorized", body.size(), "text/plain", false);
                            send(conn->fd, hdr.c_str(), hdr.size(), 0);
                            send(conn->fd, body.c_str(), body.size(), 0);
                            close(conn->fd);
                            return;
                        }
                        // URI expected /upload/<name> or /<name>
                        string name;
                        if (conn->uri.rfind("/upload/",0)==0) name = conn->uri.substr(8);
                        else if (conn->uri.size()>1) name = conn->uri.substr(1);
                        else {
                            string body = "Bad Request\n";
                            string hdr = make_response_header(400, "Bad Request", body.size(), "text/plain", false);
                            send(conn->fd, hdr.c_str(), hdr.size(), 0);
                            send(conn->fd, body.c_str(), body.size(), 0);
                            close(conn->fd);
                            return;
                        }

                        // decode name (very simple)
                        // build target paths
                        string target = sanitize_path(root, "/" + name);
                        string tmp = target + ".part";

                        // Content-Range header: bytes X-Y/total
                        off_t write_offset = 0;
                        auto itCR = conn->headers.find("content-range");
                        if (itCR != conn->headers.end()) {
                            // parse
                            // bytes 0-1023/2048
                            string v = itCR->second;
                            auto sp = v.find(' ');
                            if (sp != string::npos) {
                                string rng = v.substr(sp+1);
                                auto slash = rng.find('/');
                                string range = (slash==string::npos)?rng:rng.substr(0,slash);
                                auto dash = range.find('-');
                                if (dash != string::npos) {
                                    string s1 = range.substr(0,dash);
                                    // string s2 = range.substr(dash+1);
                                    write_offset = stoll(s1);
                                }
                            }
                        }

                        // append/seek based on write_offset
                        int fdout;
                        if (write_offset==0) {
                            fdout = open(tmp.c_str(), O_CREAT | O_WRONLY | O_TRUNC, 0644);
                        } else {
                            fdout = open(tmp.c_str(), O_CREAT | O_WRONLY, 0644);
                        }
                        if (fdout < 0) {
                            string body = "Internal Server Error\n";
                            string hdr = make_response_header(500, "Internal Server Error", body.size(), "text/plain", false);
                            send(conn->fd, hdr.c_str(), hdr.size(), 0);
                            send(conn->fd, body.c_str(), body.size(), 0);
                            close(conn->fd);
                            return;
                        }
                        if (write_offset) lseek(fdout, write_offset, SEEK_SET);

                        // write already-buffered body content first
                        size_t to_write = min(conn->content_length - conn->body_received, conn->inbuf.size());
                        if (to_write > 0) {
                            ssize_t w = write(fdout, conn->inbuf.data(), to_write);
                            (void)w; conn->body_received += to_write; conn->inbuf.erase(0, to_write);
                        }

                        // read remainder from socket until content-length satisfied
                        while (conn->body_received < conn->content_length) {
                            char tmpbuf[8192];
                            ssize_t r = recv(conn->fd, tmpbuf, sizeof(tmpbuf), 0);
                            if (r > 0) {
                                ssize_t w = write(fdout, tmpbuf, r);
                                (void)w;
                                conn->body_received += r;
                            } else if (r == 0) break;
                            else { if (errno==EAGAIN||errno==EWOULDBLOCK) break; if (errno==EINTR) continue; perror("recv"); close(fdout); close(conn->fd); return; }
                        }
                        close(fdout);

                        // if Content-Range included with total size, and we have complete file -> rename
                        auto itTotal = conn->headers.find("content-range");
                        bool maybeComplete = false;
                        if (itTotal != conn->headers.end()) {
                            // parse total
                            string v = itTotal->second;
                            auto slash = v.find('/');
                            if (slash != string::npos) {
                                string total = v.substr(slash+1);
                                try {
                                    off_t t = stoll(total);
                                    off_t cur = file_size(tmp);
                                    if (cur >= t) maybeComplete = true;
                                } catch(...) {}
                            }
                        } else {
                            // no Content-Range: if we've received content_length bytes -> complete
                            off_t cur = file_size(tmp);
                            if (cur >= (off_t)conn->content_length) maybeComplete = true;
                        }

                        if (maybeComplete) {
                            rename(tmp.c_str(), target.c_str()); // overwrite
                            string body = "Created\n";
                            string hdr = make_response_header(201, "Created", body.size(), "text/plain", false);
                            send(conn->fd, hdr.c_str(), hdr.size(), 0);
                            send(conn->fd, body.c_str(), body.size(), 0);
                            close(conn->fd);
                            return;
                        } else {
                            // partial accepted
                            string body = "Partial\r\n";
                            string hdr = make_response_header(200, "OK", body.size(), "text/plain", false);
                            send(conn->fd, hdr.c_str(), hdr.size(), 0);
                            send(conn->fd, body.c_str(), body.size(), 0);
                            close(conn->fd);
                            return;
                        }
                    } // PUT end

                    // simple multipart POST /upload (limited; for browsers)
                    if (conn->method == "POST") {
                        // if /api/files or other endpoints, skip (we handle only /upload)
                        if (conn->uri == "/upload") {
                            // naive parse: look for boundary and raw file content - for production use a multipart parser
                            string hdrs;
                            // we already parsed headers earlier
                            auto itCT = conn->headers.find("content-type");
                            if (itCT == conn->headers.end()) {
                                string body = "Bad Request\n";
                                string hdr = make_response_header(400, "Bad Request", body.size(), "text/plain", false);
                                send(conn->fd, hdr.c_str(), hdr.size(), 0);
                                send(conn->fd, body.c_str(), body.size(), 0);
                                close(conn->fd);
                                return;
                            }
                            string ct = itCT->second;
                            // fallback: if we have the entire body in conn->inbuf, write as single file with generated name
                            string genname = "upload_" + to_string(conn->fd) + "_" + to_string(time(nullptr));
                            string target = root + "/" + genname;
                            int fdout = open(target.c_str(), O_CREAT | O_WRONLY | O_TRUNC, 0644);
                            if (fdout < 0) { string body="Internal\n"; string hdr = make_response_header(500,"Internal Server Error",body.size(),"text/plain",false); send(conn->fd,hdr.c_str(),hdr.size(),0); send(conn->fd,body.c_str(),body.size(),0); close(conn->fd); return;}
                            // write whatever buffered
                            if (!conn->inbuf.empty()) write(fdout, conn->inbuf.data(), conn->inbuf.size());
                            // then read remainder
                            while (conn->body_received < conn->content_length) {
                                char tmpbuf[8192];
                                ssize_t r = recv(conn->fd, tmpbuf, sizeof(tmpbuf), 0);
                                if (r > 0) { write(fdout, tmpbuf, r); conn->body_received += r; }
                                else if (r==0) break;
                                else { if (errno==EAGAIN||errno==EWOULDBLOCK) break; if (errno==EINTR) continue; perror("recv"); break; }
                            }
                            close(fdout);
                            string body = "Created\n";
                            string hdr = make_response_header(201,"Created",body.size(),"text/plain",false);
                            send(conn->fd,hdr.c_str(),hdr.size(),0);
                            send(conn->fd,body.c_str(),body.size(),0);
                            close(conn->fd);
                            return;
                        }
                    } // POST

                    // DELETE
                    if (conn->method == "DELETE") {
                        // auth
                        if (!token_ok(this->api_token, conn->headers)) {
                            string body = "Unauthorized\n";
                            string hdr = make_response_header(401, "Unauthorized", body.size(), "text/plain", false);
                            send(conn->fd, hdr.c_str(), hdr.size(), 0);
                            send(conn->fd, body.c_str(), body.size(), 0);
                            close(conn->fd);
                            return;
                        }
                        // uri /files/<name> or /<name>
                        string name;
                        if (conn->uri.rfind("/files/",0)==0) name = conn->uri.substr(7);
                        else if (conn->uri.size()>1) name = conn->uri.substr(1);
                        else { string body="Bad Request\n"; string hdr = make_response_header(400,"Bad Request",body.size(),"text/plain",false); send(conn->fd,hdr.c_str(),hdr.size(),0); send(conn->fd,body.c_str(),body.size(),0); close(conn->fd); return; }
                        string path = sanitize_path(root, "/" + name);
                        if (!file_exists(path)) {
                            string body = "Not Found\n"; string hdr = make_response_header(404,"Not Found",body.size(),"text/plain",false); send(conn->fd,hdr.c_str(),hdr.size(),0); send(conn->fd,body.c_str(),body.size(),0); close(conn->fd); return;
                        }
                        if (unlink(path.c_str()) == 0) {
                            string body = "Deleted\n"; string hdr = make_response_header(200,"OK",body.size(),"text/plain",false); send(conn->fd,hdr.c_str(),hdr.size(),0); send(conn->fd,body.c_str(),body.size(),0); close(conn->fd); return;
                        } else {
                            string body = "Internal\n"; string hdr = make_response_header(500,"Internal Server Error",body.size(),"text/plain",false); send(conn->fd,hdr.c_str(),hdr.size(),0); send(conn->fd,body.c_str(),body.size(),0); close(conn->fd); return;
                        }
                    } // DELETE

                    // fallback: unrecognized
                    string body = "Not Implemented\n";
                    string hdr = make_response_header(501, "Not Implemented", body.size(), "text/plain", false);
                    send(conn->fd, hdr.c_str(), hdr.size(), 0);
                    send(conn->fd, body.c_str(), body.size(), 0);
                    close(conn->fd);
                }); // enqueue
            } // else
        } // for events
    } // while

    close(sfd);
    close(epfd);
}
