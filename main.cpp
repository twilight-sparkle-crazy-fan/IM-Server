#include "chatserver.h"
#include "mysql/sqlConnectionPool.h"
#include <iostream>
#include <csignal>

// 全局指针，方便信号处理函数访问
ChatServer* g_server = nullptr;

// 处理 Ctrl+C 等信号
void handleSignal(int sig) {
    if (g_server) {
        std::cout << "\n[System] Signal (" << sig << ") received. Shutting down server..." << std::endl;

        delete g_server; 
        g_server = nullptr;
    }
    SqlConnPool::getInstance().closePool();
    exit(0);
}

int main(int argc, char* argv[]) {

    signal(SIGPIPE, SIG_IGN);
    signal(SIGINT, handleSignal);
    signal(SIGTERM, handleSignal);

    int port = 8888;
    int threadNum = 8;

    if (argc >= 2) port = std::stoi(argv[1]);
    if (argc >= 3) threadNum = std::stoi(argv[2]);

    try {
        std::cout << "========================================" << std::endl;
        std::cout << "   IM Server starting on port: " << port << std::endl;
        std::cout << "   Worker Threads: " << threadNum << std::endl;
        std::cout << "========================================" << std::endl;

        // 初始化 MySQL 连接池
        SqlConnPool::getInstance().init(
            "127.0.0.1",   // host
            3306,           // port
            "root",         // user        — 按实际环境修改
            "",       // password    — 按实际环境修改
            "im_server",    // database
            8               // pool size
        );

        g_server = new ChatServer(port, threadNum);
        g_server->start();

    } catch (const std::exception& e) {
        std::cerr << "[Critical] Server error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}