#include <iostream>
#include <string>
#include "server.h"

int main(int argc, char** argv) {
    std::string root = "./storage";
    int port = 8080;
    int threads = 8;

    if (argc > 1) port = std::stoi(argv[1]);
    if (argc > 2) root = argv[2];
    if (argc > 3) threads = std::stoi(argv[3]);

    Server server(port, root, threads);
    server.run();

    return 0;
}
