#include "chat.h"
#include <sys/socket.h>
#include <iostream>
#include <sys/epoll.h>
#include <unordered_set>
#include <vector>
#include <cstring>
#include "usermanager.h"
#include "../mysql/sqlConnectionPool.h"

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

    std::lock_guard<std::mutex> lock(bufferMutex_);
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
    std::lock_guard<std::mutex> lock(bufferMutex_);
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
    handlers["REGISTER"] = [this](const json &message)
    { this->handleRegister(message); };
    handlers["CHAT"] = [this](const json &message)
    { this->handleChat(message); };
    handlers["HEARTBEAT"] = [this](const json &message)
    { this->handleHeartbeat(message); };
    handlers["ADD_FRIEND"] = [this](const json &message)
    { this->handleAddFriend(message); };
    handlers["GET_FRIENDS"] = [this](const json &message)
    { this->handleGetFriends(message); };
}

// 细分业务组

// ─── 注册 ───────────────────────────────────────────────────────────────────
void ChatSession::handleRegister(const json &message)
{
    if (!message.contains("user") || !message.contains("pwd")) {
        json resp = {{"type", "REGISTER_RESP"}, {"success", false}, {"msg", "missing user or pwd"}};
        send(resp);
        return;
    }

    std::string user = message["user"];
    std::string pwd  = message["pwd"];

    if (user.empty() || pwd.empty()) {
        json resp = {{"type", "REGISTER_RESP"}, {"success", false}, {"msg", "user/pwd cannot be empty"}};
        send(resp);
        return;
    }

    auto conn = SqlConnPool::getInstance().getConn();
    if (!conn) {
        json resp = {{"type", "REGISTER_RESP"}, {"success", false}, {"msg", "database unavailable"}};
        send(resp);
        return;
    }

    // 防注入转义
    char escapedUser[101], escapedPwd[129];
    mysql_real_escape_string(conn.get(), escapedUser, user.c_str(), user.size());
    mysql_real_escape_string(conn.get(), escapedPwd,  pwd.c_str(),  pwd.size());

    // 检查用户名是否已存在
    char sql[512];
    snprintf(sql, sizeof(sql),
             "SELECT id FROM User WHERE username='%s'", escapedUser);

    if (mysql_query(conn.get(), sql) != 0) {
        json resp = {{"type", "REGISTER_RESP"}, {"success", false}, {"msg", "query error"}};
        send(resp);
        return;
    }

    MYSQL_RES* res = mysql_store_result(conn.get());
    if (res && mysql_num_rows(res) > 0) {
        mysql_free_result(res);
        json resp = {{"type", "REGISTER_RESP"}, {"success", false}, {"msg", "username already exists"}};
        send(resp);
        return;
    }
    if (res) mysql_free_result(res);

    // 插入新用户
    snprintf(sql, sizeof(sql),
             "INSERT INTO User(username, password) VALUES('%s', '%s')", escapedUser, escapedPwd);

    if (mysql_query(conn.get(), sql) != 0) {
        json resp = {{"type", "REGISTER_RESP"}, {"success", false},
                     {"msg", std::string("register failed: ") + mysql_error(conn.get())}};
        send(resp);
        return;
    }

    int newUserId = static_cast<int>(mysql_insert_id(conn.get()));
    std::cout << "[Register] user=" << user << " userId=" << newUserId << std::endl;

    json resp = {{"type", "REGISTER_RESP"}, {"success", true}, {"userId", newUserId}, {"msg", "register success"}};
    send(resp);
}

// ─── 登录（数据库校验） ──────────────────────────────────────────────────────
void ChatSession::handleLogin(const json &message)
{
    if (!message.contains("user") || !message.contains("pwd")) {
        json resp = {{"type", "LOGIN_RESP"}, {"success", false}, {"msg", "missing user or pwd"}};
        send(resp);
        return;
    }

    std::string user = message["user"];
    std::string pwd  = message["pwd"];

    if (user.empty() || pwd.empty()) {
        json resp = {{"type", "LOGIN_RESP"}, {"success", false}, {"msg", "invalid credentials"}};
        send(resp);
        return;
    }

    auto conn = SqlConnPool::getInstance().getConn();
    if (!conn) {
        json resp = {{"type", "LOGIN_RESP"}, {"success", false}, {"msg", "database unavailable"}};
        send(resp);
        return;
    }

    // 防注入转义
    char escapedUser[101], escapedPwd[129];
    mysql_real_escape_string(conn.get(), escapedUser, user.c_str(), user.size());
    mysql_real_escape_string(conn.get(), escapedPwd,  pwd.c_str(),  pwd.size());

    char sql[512];
    snprintf(sql, sizeof(sql),
             "SELECT id, nickname FROM User WHERE username='%s' AND password='%s'",
             escapedUser, escapedPwd);

    if (mysql_query(conn.get(), sql) != 0) {
        json resp = {{"type", "LOGIN_RESP"}, {"success", false}, {"msg", "query error"}};
        send(resp);
        return;
    }

    MYSQL_RES* res = mysql_store_result(conn.get());
    if (!res || mysql_num_rows(res) == 0) {
        if (res) mysql_free_result(res);
        json resp = {{"type", "LOGIN_RESP"}, {"success", false}, {"msg", "wrong username or password"}};
        send(resp);
        return;
    }

    MYSQL_ROW row = mysql_fetch_row(res);
    int dbUserId = std::stoi(row[0]);
    std::string nickname = row[1] ? row[1] : user;
    mysql_free_result(res);

    // 状态变更
    userId    = dbUserId;
    username_ = user;
    isLogin   = true;

    // 注册映射
    UserManager::getInstance().addSession(userId, shared_from_this());

    std::cout << "[Login] user=" << user << " userId=" << userId << std::endl;

    // 回执给客户端
    json resp = {{"type", "LOGIN_RESP"}, {"success", true},
                 {"userId", userId}, {"nickname", nickname}, {"msg", "login success"}};
    send(resp);

    // 登录成功后拉取离线消息
    pullOfflineMessages();
}

void ChatSession::handleChat(const json &message)
{
    // 未登录拒绝
    if (!isLogin) {
        json resp = {{"type", "SYSTEM"}, {"msg", "please login first"}};
        send(resp);
        return;
    }

    if (!message.contains("to") || !message.contains("content")) {
        json resp = {{"type", "SYSTEM"}, {"msg", "missing to or content"}};
        send(resp);
        return;
    }

    int toId            = message["to"];
    std::string content = message["content"];

    // 构造转发消息，附上发送者 ID
    json forwardMsg = {
        {"type",    "CHAT"},
        {"from",    userId},
        {"to",      toId},
        {"content", content}
    };

    bool ok = UserManager::getInstance().sendTo(toId, forwardMsg);
    if (!ok) {
        // 目标不在线，存入离线消息表
        storeOfflineMessage(toId, userId, forwardMsg.dump());

        json notify = {{"type", "SYSTEM"}, {"msg", "user " + std::to_string(toId) + " is offline, message saved"}};
        send(notify);
    }
}

void ChatSession::handleHeartbeat(const json &message)
{
    // 更新活跃时间，保持连接存活
    lastActiveTime = time(nullptr);
}

// ─── 添加好友 ────────────────────────────────────────────────────────────────
void ChatSession::handleAddFriend(const json &message)
{
    if (!isLogin) {
        json resp = {{"type", "SYSTEM"}, {"msg", "please login first"}};
        send(resp);
        return;
    }

    if (!message.contains("friendId")) {
        json resp = {{"type", "ADD_FRIEND_RESP"}, {"success", false}, {"msg", "missing friendId"}};
        send(resp);
        return;
    }

    int friendId = message["friendId"];
    if (friendId == userId) {
        json resp = {{"type", "ADD_FRIEND_RESP"}, {"success", false}, {"msg", "cannot add yourself"}};
        send(resp);
        return;
    }

    auto conn = SqlConnPool::getInstance().getConn();
    if (!conn) {
        json resp = {{"type", "ADD_FRIEND_RESP"}, {"success", false}, {"msg", "database unavailable"}};
        send(resp);
        return;
    }

    // 检查目标用户是否存在
    char sql[512];
    snprintf(sql, sizeof(sql), "SELECT id FROM User WHERE id=%d", friendId);

    if (mysql_query(conn.get(), sql) != 0) {
        json resp = {{"type", "ADD_FRIEND_RESP"}, {"success", false}, {"msg", "query error"}};
        send(resp);
        return;
    }

    MYSQL_RES* checkRes = mysql_store_result(conn.get());
    if (!checkRes || mysql_num_rows(checkRes) == 0) {
        if (checkRes) mysql_free_result(checkRes);
        json resp = {{"type", "ADD_FRIEND_RESP"}, {"success", false}, {"msg", "user not found"}};
        send(resp);
        return;
    }
    mysql_free_result(checkRes);

    // 检查是否已是好友
    snprintf(sql, sizeof(sql),
             "SELECT userid FROM Friend WHERE userid=%d AND friendid=%d", userId, friendId);
    if (mysql_query(conn.get(), sql) == 0) {
        MYSQL_RES* res = mysql_store_result(conn.get());
        if (res && mysql_num_rows(res) > 0) {
            mysql_free_result(res);
            json resp = {{"type", "ADD_FRIEND_RESP"}, {"success", false}, {"msg", "already friends"}};
            send(resp);
            return;
        }
        if (res) mysql_free_result(res);
    }

    // 双向插入好友关系
    snprintf(sql, sizeof(sql),
             "INSERT INTO Friend(userid, friendid) VALUES(%d, %d), (%d, %d)",
             userId, friendId, friendId, userId);

    if (mysql_query(conn.get(), sql) != 0) {
        json resp = {{"type", "ADD_FRIEND_RESP"}, {"success", false},
                     {"msg", std::string("add friend failed: ") + mysql_error(conn.get())}};
        send(resp);
        return;
    }

    json resp = {{"type", "ADD_FRIEND_RESP"}, {"success", true}, {"friendId", friendId}};
    send(resp);
}

// ─── 获取好友列表 ────────────────────────────────────────────────────────────
void ChatSession::handleGetFriends(const json &message)
{
    if (!isLogin) {
        json resp = {{"type", "SYSTEM"}, {"msg", "please login first"}};
        send(resp);
        return;
    }

    auto conn = SqlConnPool::getInstance().getConn();
    if (!conn) {
        json resp = {{"type", "GET_FRIENDS_RESP"}, {"success", false}, {"msg", "database unavailable"}};
        send(resp);
        return;
    }

    char sql[512];
    snprintf(sql, sizeof(sql),
             "SELECT u.id, u.username, u.nickname FROM Friend f "
             "JOIN User u ON f.friendid = u.id WHERE f.userid = %d", userId);

    if (mysql_query(conn.get(), sql) != 0) {
        json resp = {{"type", "GET_FRIENDS_RESP"}, {"success", false}, {"msg", "query error"}};
        send(resp);
        return;
    }

    MYSQL_RES* res = mysql_store_result(conn.get());
    json friends = json::array();
    if (res) {
        MYSQL_ROW row;
        while ((row = mysql_fetch_row(res)) != nullptr) {
            json f;
            f["id"]       = std::stoi(row[0]);
            f["username"] = row[1] ? row[1] : "";
            f["nickname"] = row[2] ? row[2] : "";
            friends.push_back(f);
        }
        mysql_free_result(res);
    }

    // 用 getOnlineUsers 批量查询在线状态
    auto onlineUsers = UserManager::getInstance().getOnlineUsers();
    std::unordered_set<int> onlineSet(onlineUsers.begin(), onlineUsers.end());
    for (auto& f : friends) {
        f["online"] = onlineSet.count(f["id"].get<int>()) > 0;
    }

    json resp = {{"type", "GET_FRIENDS_RESP"}, {"success", true}, {"friends", friends}};
    send(resp);
}

// ─── 离线消息：拉取 ──────────────────────────────────────────────────────────
void ChatSession::pullOfflineMessages()
{
    auto conn = SqlConnPool::getInstance().getConn();
    if (!conn) return;

    char sql[512];
    snprintf(sql, sizeof(sql),
             "SELECT id, content FROM OfflineMessage WHERE to_userid=%d ORDER BY send_time ASC",
             userId);

    if (mysql_query(conn.get(), sql) != 0) {
        std::cerr << "[OfflineMsg] query error: " << mysql_error(conn.get()) << std::endl;
        return;
    }

    MYSQL_RES* res = mysql_store_result(conn.get());
    if (!res) return;

    std::vector<int> idsToDelete;

    MYSQL_ROW row;
    while ((row = mysql_fetch_row(res)) != nullptr) {
        int msgId = std::stoi(row[0]);
        std::string content = row[1];

        try {
            json msg = json::parse(content);
            send(msg);
            idsToDelete.push_back(msgId);
        } catch (const std::exception& e) {
            std::cerr << "[OfflineMsg] parse error for id=" << msgId << std::endl;
            idsToDelete.push_back(msgId); // 解析失败也删除，避免反复推送坏数据
        }
    }
    mysql_free_result(res);

    // 批量删除已发送的离线消息
    for (int id : idsToDelete) {
        snprintf(sql, sizeof(sql), "DELETE FROM OfflineMessage WHERE id=%d", id);
        mysql_query(conn.get(), sql);
    }

    if (!idsToDelete.empty()) {
        std::cout << "[OfflineMsg] Delivered " << idsToDelete.size()
                  << " offline messages to userId=" << userId << std::endl;
    }
}

// ─── 离线消息：存储 ──────────────────────────────────────────────────────────
void ChatSession::storeOfflineMessage(int toId, int fromId, const std::string& content)
{
    auto conn = SqlConnPool::getInstance().getConn();
    if (!conn) {
        std::cerr << "[OfflineMsg] Cannot store: database unavailable" << std::endl;
        return;
    }

    // 转义消息内容，防止 SQL 注入
    std::string escaped(content.size() * 2 + 1, '\0');
    mysql_real_escape_string(conn.get(), &escaped[0], content.c_str(), content.size());
    escaped.resize(strlen(escaped.c_str()));

    char sql[4096];
    snprintf(sql, sizeof(sql),
             "INSERT INTO OfflineMessage(to_userid, from_userid, content) VALUES(%d, %d, '%s')",
             toId, fromId, escaped.c_str());

    if (mysql_query(conn.get(), sql) != 0) {
        std::cerr << "[OfflineMsg] store error: " << mysql_error(conn.get()) << std::endl;
    }
}