#include <arpa/inet.h>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <string>
#include <sys/socket.h>
#include <unistd.h>

namespace {
constexpr int kPort = 5000;
}

int main(int argc, char** argv) {
    std::string command = (argc > 1) ? argv[1] : "whoami";

    int sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_fd < 0) {
        std::perror("socket");
        return 1;
    }

    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(kPort);
    if (inet_pton(AF_INET, "127.0.0.1", &server_addr.sin_addr) != 1) {
        std::cerr << "inet_pton failed\n";
        close(sock_fd);
        return 1;
    }

    if (connect(sock_fd, reinterpret_cast<sockaddr*>(&server_addr), sizeof(server_addr)) < 0) {
        std::perror("connect");
        close(sock_fd);
        return 1;
    }

    std::cout << "[client] connected, sending command: " << command << "\n";
    ssize_t sent = send(sock_fd, command.c_str(), command.size(), 0);
    if (sent < 0) {
        std::perror("send");
        close(sock_fd);
        return 1;
    }

    std::cout << "[client] sent bytes=" << sent << "\n";
    close(sock_fd);
    return 0;
}
