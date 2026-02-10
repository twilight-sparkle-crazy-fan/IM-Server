#pragma once
#ifndef BUFFER_H
#define BUFFER_H

#include <vector>
#include <atomic>
#include <cassert>
#include <string>
#include <arpa/inet.h>  // htonl, ntohl
#include <cstring>      // perror, memcpy
#include <sys/uio.h>    // struct iovec, readv, writev

class Buffer
{

public:
    static const size_t kCheapPrepend = 8;
    static const size_t kInitialSize = 1024;

    explicit Buffer(size_t initialSize = kInitialSize)
        : buffer(kCheapPrepend + initialSize),
          readPos(kCheapPrepend),
          writePos(kCheapPrepend) {}

    // 可读字节数
    size_t readableBytes() const { return writePos - readPos; }
    // 可写字节数
    size_t writableBytes() const { return buffer.size() - writePos; }
    // 头部预留字节数
    size_t prependableBytes() const { return readPos; }
    
public:
    // 协议感知组    
    // 获取可读数据的起始
    const char *peek() const { return begin() + readPos; }

    void retrieve(size_t len)
    {
        assert(len <= readableBytes());
        if (len < readableBytes())
        {
            readPos += len;
        }
        else
        {
            retrieveAll();
        }
    }

    void retrieveAll()
    {
        readPos = kCheapPrepend;
        writePos = kCheapPrepend;
    }

    // 读出并返回字符串用于解析json
    std::string retrieveAllAsString()
    {
        std::string str(peek(), readableBytes());
        retrieveAll();
        return str;
    }

    std::string retrieveAsString(int32_t len)
    {
        assert(len <= readableBytes());
        std::string str(peek(), len);
        retrieve(len);
        return str;
    }

public:
    // 字节序转换组
    void appendInt32(int32_t x)
    {
        int32_t be32 = htonl(x); // 主机序 → 网络序
        append(reinterpret_cast<char *>(&be32), sizeof be32);
    }

    int32_t readInt32()
    {
        int32_t result = peekInt32(); 
        retrieve(sizeof(int32_t));    
        return result;
    }

    int32_t peekInt32() const
    {
        assert(readableBytes() >= sizeof(int32_t));
        int32_t be32 = 0;
        ::memcpy(&be32, peek(), sizeof(int32_t));
        return ntohl(be32); // 网络序 → 主机序
    }

public: 
    //扩容与空间回收组
    void append(const std::string &str);
    void append(const char *data, size_t len);
    void ensureWritable(size_t len);
    char *beginWrite();
    const char *beginWrite() const;

public:
    // 读写文件描述符
    ssize_t readFd(int fd, int* saveErrno);


private:
    char *begin() { return buffer.data(); }
    const char *begin() const { return buffer.data(); }
    void makeSpace(size_t len);

    std::vector<char> buffer;  
    std::atomic<size_t> readPos;  //读起始位置
    std::atomic<size_t> writePos; //写起始位置
};

#endif