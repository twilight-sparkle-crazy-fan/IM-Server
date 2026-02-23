#ifndef CHAT_SESSION_H
#define CHAT_SESSION_H

#include "buffer.h"
#include "nlohmann/json.hpp"
#include <atomic>
#include <ctime>
#include <unordered_map>
#include <exception>
#include <functional>
#include <memory>
#include <unistd.h>
#include <mutex>
#include <string>

using json = nlohmann::json;

class ChatSession : public std::enable_shared_from_this<ChatSession> {
public:
    ChatSession(int fd);
    ~ChatSession();

    //核心功能组
    void processRead();
    void processWrite();
    void close();
    
    //发送接口，非阻塞
    void send(const json &message);

    //获取成员组
    bool getLogin() const {return isLogin;}
    int  getUserId() const {return userId;}
    int  getSocketFd() const {return socketFd;};

    //心跳检测组
    time_t getLastActiveTime() const {return lastActiveTime;}
    bool checkTimeout (int timeoutSeconds);

public:
    static std::function<void(int,uint32_t)> ModEpollCallback;

private:
    //尝试解包
    void handlePacket();

    //业务逻辑组
    void initHandlers();// 初始化业务处理函数映射表 
    void dispatch(const json& msgObj);// 路由分发

    //细分业务组
    void handleLogin(const json& msg);
    void handleRegister(const json& msg);
    void handleChat(const json& msg);
    void handleHeartbeat(const json& msg);
    void handleAddFriend(const json& msg);
    void handleGetFriends(const json& msg);

    // 离线消息辅助
    void pullOfflineMessages();
    void storeOfflineMessage(int toId, int fromId, const std::string& content);

private:
    int socketFd;
    int userId;
    std::string username_; // 登录后记录用户名
    bool isLogin;
    std::atomic_bool isClosed;
    time_t lastActiveTime;

    
    Buffer inputBuffer;
    Buffer outputBuffer;
    std::mutex bufferMutex_; // 保护 Buffer 相关操作
    std::unordered_map<std::string, std::function<void(const json&)>> handlers;
};


#endif