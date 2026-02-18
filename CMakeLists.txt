cmake_minimum_required(VERSION 3.10)

# 项目名称
project(Server)

# 设置 C++ 标准为 17 或 20 (这里默认设为 17)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 打印编译信息（可选）
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")

# --- 1. 设置包含目录 (Header paths) ---
# 这样你在代码里就可以直接 #include "buffer.h" 或 "threadpool.h"
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/chat
    ${CMAKE_CURRENT_SOURCE_DIR}/threadpool
)

# --- 2. 收集源文件 (Source files) ---
# 按照你的要求，只包含 buffer 的 cpp 和 main.cpp
set(SOURCES
    main.cpp
    chat/buffer.cpp
    chat/chat.cpp
    chat/usermanager.cpp
    # config.cpp
    # webserver.cpp
)

# --- 3. 生成可执行文件 ---
add_executable(${PROJECT_NAME} ${SOURCES})

# --- 4. 链接库 (如果是 Linux 上的多线程项目，通常需要 pthread) ---
find_package(Threads REQUIRED)
target_link_libraries(${PROJECT_NAME} PRIVATE Threads::Threads)

# (可选) 设置输出目录为 build 文件夹之外
# set(EXECUTABLE_OUTPUT_PATH ${PROJECT_DIR}/bin)