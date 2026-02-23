#include <arpa/inet.h>
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <sys/socket.h>
#include <unistd.h>

namespace {
constexpr int kPort = 5000;
int g_listen_fd = -1;

void handle_signal(int) {
    if (g_listen_fd >= 0) {
        close(g_listen_fd);
        g_listen_fd = -1;
    }
    _exit(0);
}
}

int main() {
    std::signal(SIGINT, handle_signal);
    std::signal(SIGTERM, handle_signal);

    g_listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (g_listen_fd < 0) {
        std::perror("socket");
        return 1;
    }

    int opt = 1;
    if (setsockopt(g_listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        std::perror("setsockopt");
        close(g_listen_fd);
        return 1;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(kPort);

    if (bind(g_listen_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::perror("bind");
        close(g_listen_fd);
        return 1;
    }

    if (listen(g_listen_fd, 1) < 0) {
        std::perror("listen");
        close(g_listen_fd);
        return 1;
    }

    std::cout << "[server] listening on 0.0.0.0:" << kPort << " (Step 4: accept + recv)\n";

    while (true) {
        sockaddr_in client_addr{};
        socklen_t client_len = sizeof(client_addr);

        int conn_fd = accept(g_listen_fd, reinterpret_cast<sockaddr*>(&client_addr), &client_len);
        if (conn_fd < 0) {
            if (errno == EINTR) {
                continue;
            }
            std::perror("accept");
            break;
        }

        char client_ip[INET_ADDRSTRLEN] = {};
        if (inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip)) == nullptr) {
            std::snprintf(client_ip, sizeof(client_ip), "%s", "<ntop_fail>");
        }

        std::cout << "[server] accepted fd=" << conn_fd << " from "
                  << client_ip << ":" << ntohs(client_addr.sin_port) << "\n";

        char recv_buf[4096];
        ssize_t n = recv(conn_fd, recv_buf, sizeof(recv_buf) - 1, 0);
        if (n < 0) {
            std::perror("recv");
            close(conn_fd);
            continue;
        }

        if (n == 0) {
            std::cout << "[server] recv returned 0 (peer closed)\n";
            close(conn_fd);
            continue;
        }

        recv_buf[n] = '\0';
        std::cout << "[server] recv bytes=" << n << " data=\"" << recv_buf << "\"\n";

        close(conn_fd);
    }

    close(g_listen_fd);
    g_listen_fd = -1;
    return 0;
}
