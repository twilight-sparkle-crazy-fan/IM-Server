#include "buffer.h"

// 扩容与空间回收组
void Buffer::ensureWritable(size_t len)
{
    if (writableBytes() < len)
    {
        makeSpace(len);
    }
}

char *Buffer::beginWrite() { return begin() + writePos; }
const char *Buffer::beginWrite() const { return begin() + writePos; }

void Buffer::append(const std::string &str)
{
    append(str.c_str(), str.size());
}

void Buffer::append(const char *str, size_t len)
{
    ensureWritable(len);
    std::copy(str, str + len, beginWrite());
    writePos += len;
}

// 确保空间
void Buffer::makeSpace(size_t len)
{
    if (writableBytes() + prependableBytes() < len + kCheapPrepend)
    {
        buffer.resize(writePos + len);
    }
    else
    {
        size_t readable = readableBytes();
        std::copy(begin() + readPos, begin() + writePos, begin() + kCheapPrepend);
        readPos = kCheapPrepend;
        writePos = readPos + readable;
    }
}

// 读文件
ssize_t Buffer::readFd(int fd, int *err)
{
    // 栈空间
    char extrabuf[65536];

    struct iovec vec[2];
    const size_t writable = writableBytes();

    vec[0].iov_base = begin() + writePos;
    vec[0].iov_len = writable;

    vec[1].iov_base = extrabuf;
    vec[1].iov_len = sizeof extrabuf;

    const int iovcnt = (writable < sizeof extrabuf) ? 2 : 1;

    const ssize_t n = readv(fd, vec, iovcnt); // 读入所有数据

    if (n < 0)
    {
        *err = errno;
    }
    else if (static_cast<size_t>(n) <= writable)
    {
        writePos += n;
    }
    else
    {
        writePos = buffer.size();
        append(extrabuf, n - writable);
    }
    //错误码/buffer够用//不够用

    return n;
}

