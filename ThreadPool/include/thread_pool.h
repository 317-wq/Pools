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
    
    // 提交任务
    void submit(Task task);

    ~ThreadPool();
};