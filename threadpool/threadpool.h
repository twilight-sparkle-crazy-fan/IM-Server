#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <vector>
#include <queue>
#include <memory>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <future>
#include <functional>
#include <stdexcept>

class Threadpool
{

public:
    explicit Threadpool(size_t threads_number);
    ~Threadpool();

    /*
    template<class F, class... Args>
    auto enqueue(F&& f, Args&&... args)
        -> std::future<typename std::result_of<F(Args...)>::type>;
    c++11 写法
    */

    template <class F, class... Args>
    auto enqueue(F &&f, Args &&...args)
        -> std::future<decltype(std::invoke(std::forward<F>(f), std::forward<Args>(args)...))>;
    //  -> std::future<std::invoke_result_t<F, Args...>>

private:
    std::vector<std::thread> workers;
    std::queue<std::function<void()>> work_queue;

    std::mutex queue_mutex;
    std::condition_variable m_cond;
    bool m_stop;
};

inline Threadpool::Threadpool(size_t threads_number) : m_stop(false)
{
    for (size_t i = 0; i < threads_number; ++i)
    {
        workers.emplace_back([this]
                             {
            for(;;)
            {
                std::function<void()> task;
                {
                    std::unique_lock<std::mutex> lock(queue_mutex);
                    m_cond.wait(lock, [this] { return m_stop || !work_queue.empty(); });
                    if (m_stop && work_queue.empty())
                        return;
                    task = std::move(work_queue.front());
                    work_queue.pop();
                }
                task();
            } });
    }
}

inline Threadpool::~Threadpool()
{
    {
        std::unique_lock<std::mutex> lock(queue_mutex);
        m_stop = true;
    }
    m_cond.notify_all();
    for (auto &worker : workers)
        worker.join();
}

template <class F, class... Args>
auto Threadpool::enqueue(F &&f, Args &&...args)
    -> std::future<decltype(std::invoke(std::forward<F>(f), std::forward<Args>(args)...))>
{
    using return_type = decltype(std::invoke(std::forward<F>(f), std::forward<Args>(args)...));

    auto task = std::make_shared<std::packaged_task<return_type()>>(
        std::bind(std::forward<F>(f), std::forward<Args>(args)...));

    std::future<return_type> res = task->get_future();
    {
        std::unique_lock<std::mutex> lock(queue_mutex);
        if (m_stop)
            throw std::runtime_error("enqueue on stopped Threadpool");
        work_queue.emplace([task]() { (*task)(); });
    }
    m_cond.notify_one();
    return res;
}

#endif