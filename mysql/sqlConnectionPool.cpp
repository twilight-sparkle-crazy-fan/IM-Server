#include "sqlConnectionPool.h"
#include <iostream>
#include <cassert>

// ────────────────────────────────────────────────────────────────────────────
// 初始化：预创建 connSize 条 MySQL 长连接
// ────────────────────────────────────────────────────────────────────────────
void SqlConnPool::init(const std::string& host, unsigned int port,
                       const std::string& user, const std::string& pwd,
                       const std::string& dbName, int connSize)
{
    assert(connSize > 0);

    for (int i = 0; i < connSize; ++i) {
        MYSQL* conn = mysql_init(nullptr);
        if (!conn) {
            std::cerr << "[SqlConnPool] mysql_init() failed!" << std::endl;
            continue;
        }

        // 设置连接超时 & 自动重连
        unsigned int timeout = 10;
        bool reconnect = true;
        mysql_options(conn, MYSQL_OPT_CONNECT_TIMEOUT, &timeout);
        mysql_options(conn, MYSQL_OPT_RECONNECT, &reconnect);

        // 设置字符集为 utf8mb4
        mysql_options(conn, MYSQL_SET_CHARSET_NAME, "utf8mb4");

        conn = mysql_real_connect(conn,
                                  host.c_str(), user.c_str(), pwd.c_str(),
                                  dbName.c_str(), port, nullptr, 0);
        if (!conn) {
            std::cerr << "[SqlConnPool] mysql_real_connect() failed: "
                      << mysql_error(conn) << std::endl;
            continue;
        }

        connQueue_.push(conn);
    }

    maxConn_ = static_cast<int>(connQueue_.size());
    closed_  = false;

    std::cout << "[SqlConnPool] Initialized with " << maxConn_
              << " connections (requested " << connSize << ")" << std::endl;

    if (maxConn_ == 0) {
        std::cerr << "[SqlConnPool] WARNING: No valid connections created!" << std::endl;
    }
}

// ────────────────────────────────────────────────────────────────────────────
// 获取连接 — RAII（shared_ptr + 自定义删除器）
// ────────────────────────────────────────────────────────────────────────────
std::shared_ptr<MYSQL> SqlConnPool::getConn()
{
    if (closed_) {
        return nullptr;
    }

    MYSQL* conn = nullptr;
    {
        std::unique_lock<std::mutex> lock(mutex_);
        cond_.wait(lock, [this] { return !connQueue_.empty() || closed_; });

        if (closed_ || connQueue_.empty()) {
            return nullptr;
        }

        conn = connQueue_.front();
        connQueue_.pop();
    }

    // 检查连接是否还活着，断了就重连
    if (mysql_ping(conn) != 0) {
        std::cerr << "[SqlConnPool] Connection lost, reconnecting..." << std::endl;
        // mysql_ping 在开启 MYSQL_OPT_RECONNECT 后会自动重连
    }

    // 返回 shared_ptr，自定义删除器在析构时归还连接
    return std::shared_ptr<MYSQL>(conn, [this](MYSQL* c) {
        this->freeConn(c);
    });
}

// ────────────────────────────────────────────────────────────────────────────
// 归还连接
// ────────────────────────────────────────────────────────────────────────────
void SqlConnPool::freeConn(MYSQL* conn)
{
    if (!conn) return;

    if (closed_) {
        // 池已关闭，直接释放
        mysql_close(conn);
        return;
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        connQueue_.push(conn);
    }
    cond_.notify_one();
}

// ────────────────────────────────────────────────────────────────────────────
// 获取空闲数
// ────────────────────────────────────────────────────────────────────────────
int SqlConnPool::getFreeCount()
{
    std::lock_guard<std::mutex> lock(mutex_);
    return static_cast<int>(connQueue_.size());
}

// ────────────────────────────────────────────────────────────────────────────
// 关闭连接池
// ────────────────────────────────────────────────────────────────────────────
void SqlConnPool::closePool()
{
    if (closed_.exchange(true)) return; // 幂等

    cond_.notify_all(); // 唤醒所有等待者

    std::lock_guard<std::mutex> lock(mutex_);
    while (!connQueue_.empty()) {
        MYSQL* conn = connQueue_.front();
        connQueue_.pop();
        mysql_close(conn);
    }

    std::cout << "[SqlConnPool] All connections closed." << std::endl;
}

SqlConnPool::~SqlConnPool()
{
    closePool();
}
