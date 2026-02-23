#include <arpa/inet.h>
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <sstream>
#include <string>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

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

bool send_all(int fd, const char* buf, size_t len) {
    size_t total = 0;
    while (total < len) {
        ssize_t n = send(fd, buf + total, len - total, 0);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }
        total += static_cast<size_t>(n);
    }
    return true;
}

std::vector<std::string> split_args(const std::string& command) {
    std::istringstream iss(command);
    std::vector<std::string> args;
    std::string tok;
    while (iss >> tok) {
        args.push_back(tok);
    }
    return args;
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

    std::cout << "[server] listening on 0.0.0.0:" << kPort << " (Step 6: exec + waitpid)\n";

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
            std::snprintf(client_ip, sizeof(client_ip), "%s", "?");
        }
        std::cout << "[server] accepted fd=" << conn_fd << " from " << client_ip << ":" << ntohs(client_addr.sin_port) << "\n";

        char recv_buf[4096];
        ssize_t n = recv(conn_fd, recv_buf, sizeof(recv_buf) - 1, 0);
        if (n <= 0) {
            if (n < 0) {
                std::perror("recv");
            } else {
                std::cout << "[server] recv returned 0 (peer closed)\n";
            }
            close(conn_fd);
            continue;
        }

        recv_buf[n] = '\0';
        std::string command(recv_buf);
        while (!command.empty() && (command.back() == '\n' || command.back() == '\r' || command.back() == '\0')) {
            command.pop_back();
        }

        std::cout << "[server] command=\"" << command << "\"\n";

        std::vector<std::string> args = split_args(command);
        if (args.empty()) {
            const char* msg = "empty command\n";
            send_all(conn_fd, msg, std::strlen(msg));
            close(conn_fd);
            continue;
        }

        std::vector<char*> argv;
        argv.reserve(args.size() + 1);
        for (std::string& s : args) {
            argv.push_back(const_cast<char*>(s.c_str()));
        }
        argv.push_back(nullptr);

        int pipefd[2];
        if (pipe(pipefd) < 0) {
            std::perror("pipe");
            close(conn_fd);
            continue;
        }

        pid_t pid = fork();
        if (pid < 0) {
            std::perror("fork");
            close(pipefd[0]);
            close(pipefd[1]);
            close(conn_fd);
            continue;
        }

        if (pid == 0) {
            close(pipefd[0]);
            if (dup2(pipefd[1], STDOUT_FILENO) < 0) {
                std::perror("dup2 stdout");
                _exit(127);
            }
            if (dup2(pipefd[1], STDERR_FILENO) < 0) {
                std::perror("dup2 stderr");
                _exit(127);
            }
            close(pipefd[1]);

            execvp(argv[0], argv.data());
            std::perror("execvp");
            _exit(127);
        }

        close(pipefd[1]);

        char out[4096];
        while (true) {
            ssize_t r = read(pipefd[0], out, sizeof(out));
            if (r < 0) {
                if (errno == EINTR) {
                    continue;
                }
                std::perror("read(pipe)");
                break;
            }
            if (r == 0) {
                break;
            }
            if (!send_all(conn_fd, out, static_cast<size_t>(r))) {
                std::perror("send");
                break;
            }
        }

        close(pipefd[0]);

        int status = 0;
        if (waitpid(pid, &status, 0) < 0) {
            std::perror("waitpid");
        } else {
            if (WIFEXITED(status)) {
                std::cout << "[server] child pid=" << pid << " exit=" << WEXITSTATUS(status) << "\n";
            } else if (WIFSIGNALED(status)) {
                std::cout << "[server] child pid=" << pid << " signaled=" << WTERMSIG(status) << "\n";
            }
        }

        close(conn_fd);
    }

    close(g_listen_fd);
    g_listen_fd = -1;
    return 0;
}
