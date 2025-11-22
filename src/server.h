#pragma once
#include <string>

class Server {
public:
    Server(int port, const std::string& root, int threads);
    void run();

private:
    int port;
    std::string root;
    int thread_count;

    int create_and_bind();
    int set_nonblocking(int fd);
};
