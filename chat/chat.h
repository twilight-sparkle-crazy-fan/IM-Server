#ifndef CHAT_SESSION_H
#define CHAT_SESSION_H

#include "buffer.h"
#include "nlohmann/json.hpp"
#include <atomic>
#include <ctime>
#include <unordered_map>
#include <exception>
#include <functional>
#include <unistd.h>

using json = nlohmann::json;

class ChatSession {
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
    void handleChat(const json& msg);
    void handleHeartbeat(const json& msg);

private:
    int socketFd;
    int userId;
    bool isLogin;
    std::atomic_bool isClosed;
    time_t lastActiveTime;

    
    Buffer inputBuffer;
    Buffer outputBuffer;
    std::unordered_map<std::string, std::function<void(const json&)>> handlers;
};


#endif