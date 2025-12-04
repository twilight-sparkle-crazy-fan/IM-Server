# 1. 指定 CMake 最低版本
cmake_minimum_required(VERSION 3.10)

# 2. 项目名称
project(TinyWebServer)

# 3. 设置 C++ 标准为 C++14
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 4. 开启调试模式 (方便你用 gdb 调试)
set(CMAKE_BUILD_TYPE Debug)

# 5. 包含头文件路径 (让编译器知道去哪找 .h)
# "." 表示当前根目录
include_directories(.)

# 6. 查找并添加所有源文件
# 把所有 .cpp 文件都列出来，包括子目录里的
set(SOURCE_FILES
    main.cpp
    webserver.cpp
    config.cpp
    log/log.cpp
    timer/lst_timer.cpp
    httpconn/httpconn.cpp
    CGImysql/sql_connection_pool.cpp
)

# 7. 生成可执行文件 (名字叫 server)
add_executable(server ${SOURCE_FILES})

# 8. 链接库文件 (非常重要！)
# 你的项目用到了 线程库(pthread) 和 MySQL库(mysqlclient)
target_link_libraries(server pthread mysqlclient)