// 在 main.cpp 中添加测试代码
#include "threadpool/threadpool.h"
#include <iostream>

int main() {
    Threadpool pool(4);
    
    // 测试可变参数
    auto future = pool.enqueue([](int a, int b) {
        return a + b;
    }, 10, 20);
    
    std::cout << "Result: " << future.get() << std::endl;  // 应该输出 30
    
    return 0;
}