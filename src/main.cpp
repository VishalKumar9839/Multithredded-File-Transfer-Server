// #include <iostream>
// #include <string>
// #include "server.h"

// int main(int argc, char** argv) {
//     std::string root = "./storage";
//     int port = 8080;
//     int threads = 8;

//     if (argc > 1) port = std::stoi(argv[1]);
//     if (argc > 2) root = argv[2];
//     if (argc > 3) threads = std::stoi(argv[3]);

//     Server server(port, root, threads);
//     server.run();

//     return 0;
// }
#include "server.h"
#include <iostream>

int main(int argc, char** argv) {

    int port = 8080;
    std::string root = "./storage";
    int threads = 8;

    if (argc >= 2) port = atoi(argv[1]);
    if (argc >= 3) root = argv[2];
    if (argc >= 4) threads = atoi(argv[3]);

    std::string api_token = "";  // keep empty unless adding real authentication

    std::cout << "Starting server on port " << port
              << " root=" << root
              << " threads=" << threads << "\n";

    Server server(port, root, threads, api_token);

    server.run();
    return 0;
}
