//目前main只用于做单元测试
#include "chat/chat.h"
#include <iostream>
#include <sys/socket.h>
#include <thread>
#include <chrono>
#include <cstring>
#include <cassert>

// 辅助函数：手动构建协议包 [4字节长度 + JSON字符串]
void sendPacketToSocket(int fd, const json& j) {
    std::string str = j.dump();
    int32_t len = str.size();
    int32_t netLen = htonl(len); // 模拟网络序

    // 1. 发送长度
    write(fd, &netLen, 4);
    // 2. 发送Body
    write(fd, str.c_str(), len);
}

// 辅助函数：从Socket读取并验证
std::string readFromSocket(int fd) {
    int32_t len = 0;
    // 1. 读长度
    int n = read(fd, &len, 4);
    if (n <= 0) return "";
    len = ntohl(len);

    // 2. 读Body
    char* buf = new char[len + 1];
    memset(buf, 0, len + 1);
    read(fd, buf, len);
    std::string ret(buf);
    delete[] buf;
    return ret;
}

int main() {
    // 0. 准备环境：使用 socketpair 模拟网络连接
    // sv[0] 给服务器(ChatSession)用, sv[1] 给测试客户端用
    int sv[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) < 0) {
        perror("socketpair");
        return 1;
    }

    // 设置非阻塞 (简单起见这里省略 fcntl，但在真实 server 中是必须的)

    // 1. 初始化 Mock 的 Epoll 回调
    // 这一步非常关键！防止空指针崩溃
    ChatSession::ModEpollCallback = [](int fd, uint32_t events) {
        std::cout << "[MockEpoll] fd:" << fd << " event updated to: " << events << std::endl;
    };

    std::cout << "=== Test Start: ChatSession initialized ===\n";
    
    // 2. 创建 Session，绑定 sv[0]
    ChatSession session(sv[0]);

    // --- 测试场景 A: 模拟收到 LOGIN 包 ---
    std::cout << "\n--- Scenario A: Receive LOGIN ---" << std::endl;
    json loginMsg;
    loginMsg["type"] = "LOGIN";
    loginMsg["user"] = "test_user";
    loginMsg["pwd"] = "123456";

    // 模拟客户端向 sv[1] 写入数据
    sendPacketToSocket(sv[1], loginMsg);

    // 触发 Session 读取 sv[0]
    // 正常服务器是 Epoll 通知后调用的，这里我们手动调用
    session.processRead(); 

    // 验证：此时你应该在 handleLogin 里打个断点或加个打印，看看是否触发
    // 由于 handleLogin 还是 TODO，我们假设它把 isLogin 设为 true (需手动在 TODO 里加一行测试代码)
    
    // --- 测试场景 B: 发送数据 (Server -> Client) ---
    std::cout << "\n--- Scenario B: Server Send Msg ---" << std::endl;
    json respMsg;
    respMsg["type"] = "LOGIN_RESP";
    respMsg["status"] = 200;

    // 调用 Session 的发送接口
    session.send(respMsg);

    // 此时数据在 outputBuffer 里，需要触发 processWrite 真正发送
    session.processWrite();

    // 模拟客户端从 sv[1] 读取数据
    std::string clientRecv = readFromSocket(sv[1]);
    std::cout << "[Client] Received raw: " << clientRecv << std::endl;
    
    // 验证收到的 JSON 是否正确
    try {
        json received = json::parse(clientRecv);
        assert(received["type"] == "LOGIN_RESP");
        std::cout << "PASS: Server response matches!" << std::endl;
    } catch (...) {
        std::cerr << "FAIL: Invalid JSON response" << std::endl;
    }

    // --- 测试场景 C: 粘包测试 (进阶) ---
    std::cout << "\n--- Scenario C: Sticky Packets ---" << std::endl;
    // 模拟一次性发两个包
    sendPacketToSocket(sv[1], loginMsg);
    sendPacketToSocket(sv[1], respMsg); // 随便发另一个包

    // 触发一次读取，看 handlePacket 是否能循环处理两个包
    session.processRead();
    std::cout << "(Check logs to see if two dispatches happened)" << std::endl;


    // 清理
    // Session 析构会自动 close(sv[0])
    close(sv[1]);
    std::cout << "\n=== All Tests Finished ===" << std::endl;

    return 0;
}