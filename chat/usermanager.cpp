#include "usermanager.h"
#include <iostream>

void UserManager::addSession(int userId, ChatSession* session) {
    if (session == nullptr) {
        return;
    }
    
    // 写锁，独占访问
    std::unique_lock<std::shared_mutex> lock(m_mutex);
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
    // 读锁，共享访问
    std::shared_lock<std::shared_mutex> lock(m_mutex);
    auto it = m_users.find(toUserId);
    if (it != m_users.end() && it->second != nullptr) {
        // 找到目标用户，发送消息
        try {
            it->second->send(msg);
            return true;
        } catch (const std::exception& e) {
            std::cerr << "Error sending message to user " << toUserId << ": " << e.what() << std::endl;
            // 可能需要在这里移除失效的 session，但考虑到锁的复杂性，
            // 通常由 ChatSession 自身的错误处理机制触发 removeSession
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
