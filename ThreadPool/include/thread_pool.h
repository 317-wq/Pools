#pragma once

/*
    线程池
*/

#include <functional>
#include <thread>
#include <vector>
#include <mutex>
#include <condition_variable>
#include <memory>
#include <queue>
#include <future>
#include <type_traits>
#include <stdexcept>

class ThreadPool{
public:
    using Task = std::function<void()>;

private:
    std::queue<Task> _tasks; // 任务队列[多线程共享这个资源]
    std::vector<std::thread> _workers; // 工作线程
    std::mutex _mutex; // 保护任务队列
    std::condition_variable _cv; // 条件变量，线程同步
    bool _stop{false}; // 线程池停止标记[确保在锁内操作]

private:
    // 工作线程执行的函数
    void work();

public:
    explicit ThreadPool(size_t thread_num);
    
    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    // 提交任务->自动推导返回值(支持多种可调用对象)
    template<typename F, typename... Args>
    auto submit(F&& f, Args&&... args) -> std::future<std::invoke_result_t<F, Args...>>{
        using ReturnType = std::invoke_result_t<F, Args...>;

        auto task = std::make_shared<std::packaged_task<ReturnType()>>(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...)
        );
        
        std::future<ReturnType> future = task->get_future();

        {
            std::lock_guard<std::mutex> lock(_mutex);

            if(_stop){
                throw std::runtime_error("submit on stopped ThreadPool");
            }

            _tasks.emplace([task]{
                (*task)();
            });
        }

        _cv.notify_one(); // 唤醒任意一个线程

        return future;
    }

    ~ThreadPool();
};