#ifndef SQL_CONNECTION_POOL_H
#define SQL_CONNECTION_POOL_H

#include <mysql/mysql.h>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <string>
#include <memory>
#include <atomic>

/**
 * SqlConnPool — MySQL 连接池（单例）
 *
 * 设计目标：
 *   - 预创建固定数量的 MySQL 长连接，避免每次业务请求都走"建连 → 查询 → 断连"。
 *   - 通过 RAII (shared_ptr + custom deleter) 向 Worker 线程提供连接，
 *     使用完毕后自动归还，杜绝忘记归还的风险。
 *   - 所有接口线程安全，可在 ThreadPool 的多个 Worker 中并发使用。
 */
class SqlConnPool {
public:
    static SqlConnPool& getInstance() {
        static SqlConnPool instance;
        return instance;
    }

    // 禁止拷贝和赋值
    SqlConnPool(const SqlConnPool&) = delete;
    SqlConnPool& operator=(const SqlConnPool&) = delete;

    /**
     * 初始化连接池
     * @param host     MySQL 主机地址
     * @param port     MySQL 端口
     * @param user     用户名
     * @param pwd      密码
     * @param dbName   数据库名
     * @param connSize 连接池大小（默认 8）
     */
    void init(const std::string& host, unsigned int port,
              const std::string& user, const std::string& pwd,
              const std::string& dbName, int connSize = 8);

    /**
     * 获取一个数据库连接（RAII 方式）
     * 返回 shared_ptr<MYSQL>，析构时自动归还连接到池中。
     * 如果池中无空闲连接则阻塞等待。
     */
    std::shared_ptr<MYSQL> getConn();

    /**
     * 获取当前空闲连接数
     */
    int getFreeCount();

    /**
     * 关闭连接池，释放所有连接
     */
    void closePool();

private:
    SqlConnPool() = default;
    ~SqlConnPool();

    // 将连接归还到池中（由 shared_ptr 的自定义删除器调用）
    void freeConn(MYSQL* conn);

private:
    std::queue<MYSQL*>      connQueue_;     // 空闲连接队列
    std::mutex              mutex_;         // 保护队列
    std::condition_variable cond_;          // 等待空闲连接
    std::atomic<bool>       closed_{true};  // 连接池是否已关闭
    int                     maxConn_{0};    // 最大连接数
};

#endif
