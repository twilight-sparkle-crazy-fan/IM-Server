#ifndef LOCKER_H
#define LOCKER_H

#include <exception>
#include <mutex>
#include <condition_variable>

class sem {
    public:
    sem(int value = 0) : count(value) {
    }

    void reset(int value = 0){
        std::unique_lock<std::mutex> lock(m_mutex);
        count = value;
    }
    
    void post(){
        std::unique_lock<std::mutex> lock(m_mutex);
        count ++;
        m_cond.notify_one();
    }

    void wait(){
        std::unique_lock<std::mutex> lock(m_mutex);
        m_cond.wait(lock, [this](){return count > 0;});
        count --;
    }

    private:
    int count;
    std::mutex m_mutex;
    std::condition_variable m_cond;

};

#endif