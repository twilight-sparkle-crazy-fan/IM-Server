#pragma once

#include <sys/epoll.h>
#include <unordered_map>
#include <memory>
#include <thread>
#include <mutex>
#include <atomic>
#include "chat/chat.h"
#include "chat/usermanager.h"
#include "threadpool/threadpool.h"

static constexpr int MAX_EVENTS        = 1024;
static constexpr int HEARTBEAT_TIMEOUT = 30;   // 心跳超时阈值（秒）
static constexpr int TIMER_INTERVAL    = 1;    // 定时器扫描间隔（秒）

class ChatServer {
public:
    ChatServer(int port, int threadNum = 4);
    ~ChatServer();

    // 启动主事件循环（阻塞）
    void start();

private:
    // ─── 1. 底层网络驱动 ─────────────────────────────────────────
    void initSocket();                    // 创建监听 Socket
    void initEpoll();                     // 创建 Epoll 实例，注册 listenFd_

    int  epollAdd(int fd, uint32_t events);
    int  epollMod(int fd, uint32_t events);
    int  epollDel(int fd);

    // ─── 2. 连接生命周期管理 ──────────────────────────────────────
    void handleNewConnection();           // accept 新连接，建立 Session 并注册到 Epoll
    void handleClose(int fd);             // 断开连接：清理 sessions_、Epoll、UserManager

    // ─── 3. 任务分发（ThreadPool 联动）───────────────────────────
    void handleRead (int fd);             // 从 Epoll 读事件 → 投递 processRead  到线程池
    void handleWrite(int fd);             // 从 Epoll 写事件 → 投递 processWrite 到线程池

    // ─── 4. 定时器任务 ────────────────────────────────────────────
    void timerLoop();                     // 独立线程：每 TIMER_INTERVAL 秒扫描超时连接

private:
    int  port_;
    int  listenFd_;
    int  epollFd_;
    std::atomic<bool> running_;

    // fd → ChatSession 映射表（需 sessionsMutex_ 保护）
    std::unordered_map<int, std::shared_ptr<ChatSession>> sessions_;
    std::mutex sessionsMutex_;

    std::unique_ptr<Threadpool> threadpool_;
    std::thread timerThread_;
};
