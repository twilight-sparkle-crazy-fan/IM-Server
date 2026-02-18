#include "chat.h"
#include <sys/socket.h>
#include <iostream>
#include <sys/epoll.h>
#include "usermanager.h"

std::function<void(int, uint32_t)> ChatSession::ModEpollCallback = nullptr;

ChatSession::ChatSession(int fd) : socketFd(fd), userId(0), isLogin(false), isClosed(false)
{
    lastActiveTime = time(nullptr);
    initHandlers();
}

ChatSession::~ChatSession()
{
    close();
}

// 核心功能组
void ChatSession::processRead()
{
    int saveErrno = 0;
    ssize_t n = inputBuffer.readFd(socketFd, &saveErrno);
    if (n > 0)
    {
        lastActiveTime = time(nullptr);
        handlePacket();
    }
    else if (n == 0)
    {
        close();
    }
    else
    {
        if (errno != EAGAIN)
        {
            close();
        }
    }
}

void ChatSession::processWrite()
{
    if (isClosed)
        {return;}
    while (outputBuffer.readableBytes() > 0)
    {
        ssize_t n = write(socketFd, outputBuffer.peek(), outputBuffer.readableBytes());
        if (n > 0)
        {
            outputBuffer.retrieve(n);
        }
        else if (n < 0)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                break;
            }
            close();
            return;
        }
        else
        {
            return;
        }
    }
    if (outputBuffer.readableBytes() == 0 && ModEpollCallback) {
        ModEpollCallback(socketFd, EPOLLIN | EPOLLET);
    }
}

void ChatSession::close()
{
    if (isClosed.exchange(true))
        return;

    if (socketFd >= 0)
    {
        ::close(socketFd);
        socketFd = -1;
    }

    if (isLogin) {
        UserManager::getInstance().removeSession(userId);
    }
}

// 发送接口
void ChatSession::send(const json &message)
{
    if (isClosed)
        return;

    std::string jsonStr = message.dump();
    int32_t len = jsonStr.size();

    outputBuffer.appendInt32(len);
    outputBuffer.append(jsonStr);

    if (ModEpollCallback)
    {
        ModEpollCallback(socketFd, EPOLLIN | EPOLLOUT | EPOLLET);
    }
}

// 尝试解包
void ChatSession::handlePacket()
{
    while (inputBuffer.readableBytes() >= 4)
    {
        int32_t packetLen = inputBuffer.peekInt32();

        if (packetLen < 0 || packetLen > 2 * 1024 * 1024)
        {
            std::cerr << "packetLen error" << std::endl;
            close();
            return;
        }

        if (inputBuffer.readableBytes() < (size_t)packetLen + 4)
        {
            break;
        }

        inputBuffer.retrieve(4); // 跳过包头
        std::string jsonStr = inputBuffer.retrieveAsString(packetLen);

        try
        {
            json message = json::parse(jsonStr);
            dispatch(message);
        }
        catch (const std::exception &e)
        {
            std::cerr << "json parse error" << std::endl;
        }
    }
}

// 心跳检测
bool ChatSession::checkTimeout(int timeoutSeconds)
{
    time_t now = time(nullptr);
    if (now - lastActiveTime > timeoutSeconds)
    {
        close();
        return true;
    }
    return false;
}

// 业务逻辑组(负责派活)
void ChatSession::dispatch(const json &message)
{
    if (!message.contains("type"))
    {
        return;
    }

    std::string type = message["type"];

    if (type != "LOGIN" && !isLogin)
    {
        std::cout << "Unauthorized access!" << std::endl;
        return;
    }

    auto it = handlers.find(type);
    if (it != handlers.end())
    {
        it->second(message);
    }
    else
    {
        std::cerr << "unknown message type: " << type << std::endl;
    }
}

void ChatSession::initHandlers()
{
    handlers["LOGIN"] = [this](const json &message)
    { this->handleLogin(message); };
    handlers["CHAT"] = [this](const json &message)
    { this->handleChat(message); };
    handlers["HEARTBEAT"] = [this](const json &message)
    { this->handleHeartbeat(message); };
}

// 细分业务组
void ChatSession::handleLogin(const json &message)
{
    if (message.contains("id") && message["id"].is_number_integer()) {
        userId = message["id"];
        isLogin = true;
        UserManager::getInstance().addSession(userId, this);
        std::cout << "User " << userId << " login success!" << std::endl;
    }
}

void ChatSession::handleChat(const json &message)
{
    if (message.contains("to") && message["to"].is_number_integer()) {
        int toId = message["to"];
        UserManager::getInstance().sendTo(toId, message);
    }
}

void ChatSession::handleHeartbeat(const json &message)
{
    // TODO: 心跳处理
}
