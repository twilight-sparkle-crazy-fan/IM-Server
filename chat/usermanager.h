#ifndef USER_MANAGER_H
#define USER_MANAGER_H


#include <unordered_map>
#include <shared_mutex>
#include <mutex>
#include "chat.h"
#include "nlohmann/json.hpp"
#include <vector>

using json = nlohmann::json;

class UserManager {
public:
    static UserManager& getInstance() {
        static UserManager instance;
        return instance;
    }

    // 删除拷贝构造和赋值运算符
    UserManager(const UserManager&) = delete;
    UserManager& operator=(const UserManager&) = delete;

    // 1. 在线映射管理 & 2. 线程安全的注册与注销
    void addSession(int userId, ChatSession* session);
    void removeSession(int userId);

    // 3. 消息转发中转
    bool sendTo(int toUserId, const json& msg);

    // 4. 统计与监控
    size_t getOnlineCount() const;
    std::vector<int> getOnlineUsers() const;

    // 5. 广播功能
    void broadcast(const json& msg);

private:
    UserManager() = default;
    ~UserManager() = default;

    std::unordered_map<int, ChatSession*> m_users; // UserID -> ChatSession*
    mutable std::shared_mutex m_mutex; // 读写锁
};


#endif