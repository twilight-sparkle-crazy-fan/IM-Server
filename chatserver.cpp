#include "chatserver.h"
#include "chat/usermanager.h"

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#include <stdexcept>
#include <chrono>
#include <vector>
#include <cassert>

// 辅助函数：将 fd 设置为非阻塞
static int setNonBlocking(int fd)
{
    int flags = ::fcntl(fd, F_GETFL, 0);
    if (flags == -1) return -1;
    return ::fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

// 构造 / 析构
ChatServer::ChatServer(int port, int threadNum)
    : port_(port), listenFd_(-1), epollFd_(-1), running_(false)
{
    initSocket();
    initEpoll();

    // 将 Epoll 修改回调注入到 ChatSession（static 成员）
    // 当 Session 内部需要切换监听事件（如切换到 EPOLLOUT）时调用
    ChatSession::ModEpollCallback = [this](int fd, uint32_t events) {
        epollMod(fd, events);
    };

    threadpool_ = std::make_unique<Threadpool>(threadNum);
}

ChatServer::~ChatServer()
{
    running_ = false;
    if (timerThread_.joinable())
        timerThread_.join();

    if (listenFd_ >= 0) ::close(listenFd_);
    if (epollFd_  >= 0) ::close(epollFd_);
}

// 1. 底层网络驱动 —— Socket & Epoll 初始化

void ChatServer::initSocket()
{
    listenFd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (listenFd_ < 0)
        throw std::runtime_error("socket() failed: " + std::string(strerror(errno)));

    // 地址重用，快速重启服务时不阻塞
    int opt = 1;
    ::setsockopt(listenFd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons(port_);

    if (::bind(listenFd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0)
        throw std::runtime_error("bind() failed: " + std::string(strerror(errno)));

    if (::listen(listenFd_, SOMAXCONN) < 0)
        throw std::runtime_error("listen() failed: " + std::string(strerror(errno)));

    setNonBlocking(listenFd_);
}

void ChatServer::initEpoll()
{
    epollFd_ = ::epoll_create1(0);
    if (epollFd_ < 0)
        throw std::runtime_error("epoll_create1() failed: " + std::string(strerror(errno)));

    // 监听 fd 只需要 EPOLLIN，无需 EPOLLONESHOT
    epollAdd(listenFd_, EPOLLIN | EPOLLET);
}

int ChatServer::epollAdd(int fd, uint32_t events)
{
    epoll_event ev{};
    ev.data.fd = fd;
    ev.events  = events;
    return ::epoll_ctl(epollFd_, EPOLL_CTL_ADD, fd, &ev);
}

int ChatServer::epollMod(int fd, uint32_t events)
{
    epoll_event ev{};
    ev.data.fd = fd;
    ev.events  = events;
    return ::epoll_ctl(epollFd_, EPOLL_CTL_MOD, fd, &ev);
}

int ChatServer::epollDel(int fd)
{
    return ::epoll_ctl(epollFd_, EPOLL_CTL_DEL, fd, nullptr);
}

// ────────────────────────────────────────────────────────────────────────────
// 主事件循环
// ────────────────────────────────────────────────────────────────────────────
void ChatServer::start()
{
    running_ = true;

    // 启动定时器线程（组件 4）
    timerThread_ = std::thread(&ChatServer::timerLoop, this);

    epoll_event events[MAX_EVENTS];

    while (running_)
    {
        int n = ::epoll_wait(epollFd_, events, MAX_EVENTS, -1);
        if (n < 0)
        {
            if (errno == EINTR) continue; // 被信号中断，属正常情况
            break;
        }

        for (int i = 0; i < n; ++i)
        {
            int        fd  = events[i].data.fd;
            uint32_t   ev  = events[i].events;

            if (fd == listenFd_)
            {
                // ── 新连接到来 ──
                handleNewConnection();
            }
            else if (ev & (EPOLLRDHUP | EPOLLHUP | EPOLLERR))
            {
                // ── 连接异常或对端关闭 ──
                handleClose(fd);
            }
            else if (ev & EPOLLIN)
            {
                // ── 有数据可读 → 投递到线程池（组件 3）──
                handleRead(fd);
            }
            else if (ev & EPOLLOUT)
            {
                // ── 可写（通常由 Session 内部触发）──
                handleWrite(fd);
            }
        }
    }
}

// ────────────────────────────────────────────────────────────────────────────
// 2. 连接生命周期管理
// ────────────────────────────────────────────────────────────────────────────

void ChatServer::handleNewConnection()
{
    // 在 ET 模式下需要循环 accept 直到 EAGAIN
    while (true)
    {
        sockaddr_in clientAddr{};
        socklen_t   addrLen = sizeof(clientAddr);

        int connFd = ::accept(listenFd_,
                              reinterpret_cast<sockaddr*>(&clientAddr),
                              &addrLen);
        if (connFd < 0)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK) break; // 已无待处理连接
            break;
        }

        setNonBlocking(connFd);

        auto session = std::make_shared<ChatSession>(connFd);

        {
            std::lock_guard<std::mutex> lock(sessionsMutex_);
            sessions_[connFd] = session;
        }

        // EPOLLONESHOT：同一 fd 的事件每次只触发一次，读完后由 Worker 重新 arm
        // EPOLLRDHUP  ：内核探测到对端关闭，提前通知主线程
        epollAdd(connFd, EPOLLIN | EPOLLET | EPOLLONESHOT | EPOLLRDHUP);
    }
}

void ChatServer::handleClose(int fd)
{
    std::shared_ptr<ChatSession> session;

    {
        std::lock_guard<std::mutex> lock(sessionsMutex_);
        auto it = sessions_.find(fd);
        if (it == sessions_.end()) return; // 已经被处理过，幂等保护
        session = it->second;
        sessions_.erase(it);
    }

    // 通知 UserManager 用户下线
    if (session->getLogin())
        UserManager::getInstance().removeSession(session->getUserId());

    // 从 Epoll 中移除（fd 关闭后内核也会自动移除，此处显式处理更稳健）
    epollDel(fd);

    // 关闭 Session（ChatSession::close() 内部有 isClosed 幂等保护）
    session->close();
}


// 3. 任务分发 —— 与 ThreadPool 联动

void ChatServer::handleRead(int fd)
{
    std::shared_ptr<ChatSession> session;
    {
        std::lock_guard<std::mutex> lock(sessionsMutex_);
        auto it = sessions_.find(fd);
        if (it == sessions_.end()) return;
        session = it->second; // 增加引用计数，Worker 持有期间 session 不会析构
    }

    threadpool_->enqueue([this, fd, session]() {
        session->processRead();

        // 尝试重新 arm Epoll：
        //   若 processRead 内部已关闭 fd（ChatSession::close()），
        //   epoll_ctl 会返回 -1 / EBADF，此时触发 handleClose 清理。
        int ret = epollMod(fd, EPOLLIN | EPOLLET | EPOLLONESHOT | EPOLLRDHUP);
        if (ret < 0)
            handleClose(fd);
    });
}

void ChatServer::handleWrite(int fd)
{
    std::shared_ptr<ChatSession> session;
    {
        std::lock_guard<std::mutex> lock(sessionsMutex_);
        auto it = sessions_.find(fd);
        if (it == sessions_.end()) return;
        session = it->second;
    }

    threadpool_->enqueue([this, fd, session]() {
        session->processWrite();

        // 写完后恢复监听读事件
        int ret = epollMod(fd, EPOLLIN | EPOLLET | EPOLLONESHOT | EPOLLRDHUP);
        if (ret < 0)
            handleClose(fd);
    });
}


// 4. 定时器任务 —— 扫描心跳超时连接

void ChatServer::timerLoop()
{
    while (running_)
    {
        std::this_thread::sleep_for(std::chrono::seconds(TIMER_INTERVAL));

        // 先收集超时的 fd，再在锁外逐一关闭（避免 handleClose 重新获锁时死锁）
        std::vector<int> toClose;
        {
            std::lock_guard<std::mutex> lock(sessionsMutex_);
            for (auto& [fd, session] : sessions_)
            {
                if (session->checkTimeout(HEARTBEAT_TIMEOUT))
                    toClose.push_back(fd);
            }
        }

        for (int fd : toClose)
            handleClose(fd);
    }
}
