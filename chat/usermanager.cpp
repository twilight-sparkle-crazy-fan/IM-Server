#include "usermanager.h"
#include <iostream>

void UserManager::addSession(int userId, std::shared_ptr<ChatSession> session) {
 
    // 写锁，独占访问
    std::unique_lock<std::shared_mutex> lock(m_mutex);
    auto it = m_users.find(userId);
    if (it != m_users.end()) {
        // 可以选择在这里给旧 Session 发送一个“被顶下线”的消息
    }  
    m_users[userId] = session;
    // 可以在这里打印日志
    // std::cout << "User " << userId << " logged in. Total: " << m_users.size() << std::endl;
}

void UserManager::removeSession(int userId) {
    // 写锁，独占访问
    std::unique_lock<std::shared_mutex> lock(m_mutex);
    auto it = m_users.find(userId);
    if (it != m_users.end()) {
        m_users.erase(it);
        // std::cout << "User " << userId << " logged out. Total: " << m_users.size() << std::endl;
    }
}

bool UserManager::sendTo(int toUserId, const json& msg) {
    std::shared_ptr<ChatSession> target = nullptr;

    {
        std::shared_lock<std::shared_mutex> lock(m_mutex);
        auto it = m_users.find(toUserId);
        if (it != m_users.end()) {
            target = it->second; // 增加引用计数，确保发送时对象活着
        }
    } // 尽早释放锁

    if (target){
        try {
            target->send(msg);
            return true; // 发送成功
        } catch (...) {
            // 发送失败，可能连接已断开
            // 可以选择在这里记录日志
            // std::cout << "Failed to send message to user " << toUserId << std::endl;
        }
    }

    // 用户不在线或发送失败
    return false; // 提示调用者可以处理离线逻辑
}

size_t UserManager::getOnlineCount() const {
    // 读锁
    std::shared_lock<std::shared_mutex> lock(m_mutex);
    return m_users.size();
}

std::vector<int> UserManager::getOnlineUsers() const {
    // 读锁
    std::shared_lock<std::shared_mutex> lock(m_mutex);
    std::vector<int> users;
    users.reserve(m_users.size());
    for (const auto& pair : m_users) {
        users.push_back(pair.first);
    }
    return users;
}

void UserManager::broadcast(const json& msg) {
    // 读锁
    std::shared_lock<std::shared_mutex> lock(m_mutex);
    for (const auto& pair : m_users) {
        if (pair.second != nullptr) {
             try {
                pair.second->send(msg);
            } catch (...) {
                // 忽略单个发送失败，避免影响广播给其他人
                // 也可以记录日志
            }
        }
    }
}
