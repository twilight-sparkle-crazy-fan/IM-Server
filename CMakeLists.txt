# 最低CMake版本要求
cmake_minimum_required(VERSION 3.10)

# 项目名称和使用的语言
project(ThreadPoolProject
    VERSION 1.0.0
    DESCRIPTION "ThreadPool implementation with C++17"
    LANGUAGES CXX
)

# 设置C++标准为C++17（这是关键！）
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 如果编译器是GCC，设置一些额外警告
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU")
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# 打印信息，确认配置
message(STATUS "C++标准设置为: ${CMAKE_CXX_STANDARD}")
message(STATUS "编译器: ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")

# 创建可执行文件（这里假设你有一个main.cpp）
add_executable(threadpool_test
    main.cpp
)

# 如果你的threadpool.h是独立头文件，直接包含目录即可
target_include_directories(threadpool_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# 可选：设置输出目录
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)