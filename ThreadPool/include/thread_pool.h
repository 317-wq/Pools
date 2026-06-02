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
#include <atomic>
#include <iostream>
#include <chrono>

class ThreadPool{
public:
    using Task = std::function<void()>;
    using Clock = std::chrono::steady_clock;

private:
    struct WorkerInfo{
        std::thread worker;
        std::atomic<bool> idle{true}; // 是否空闲
        std::atomic<bool> exit{false}; // 是否退出
        std::atomic<bool> stopped{false}; // 正式退出
        Clock::time_point last_active;

        WorkerInfo()
        :last_active(Clock::now())
        {}

        ~WorkerInfo() = default;
    };

private:
    size_t _min_threads; // 最少线程数
    size_t _max_threads; // 最大线程数
    std::queue<Task> _tasks; // 任务队列[多线程共享这个资源]
    std::vector<std::unique_ptr<WorkerInfo>> _workers; // 工作线程
    std::thread _manager; // 监控线程池线程
    mutable std::mutex _mutex; // 保护任务队列[const函数中支持最小修改]
    mutable std::mutex _worker_mutex; // 保护工作线程统计[const函数中支持最小修改]
    std::condition_variable _cv; // 条件变量，线程同步
    std::atomic<bool> _stop{false}; // 线程池停止标记[确保在锁内操作]

private:
    // 工作线程执行的函数
    void work(WorkerInfo* worker);

    // 检查是否需要扩容
    bool check_expand() const;

    // 扩容
    void expand();

    // 检查是否需要缩容
    bool check_shrink() const;

    // 缩容
    void shrink();

    // 监控线程执行的函数
    void manager();

    // 添加工作线程
    void add_worker(size_t n);

    // 清理每次扩缩容之后退出的线程
    void clean_worker();
    
    // 超时缩容
    bool timeout_shrink(const WorkerInfo* worker) const;

public:
    ThreadPool(size_t min_threads, size_t max_threads);
    
    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;
    
    ~ThreadPool();

public:
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

public:
    // 线程总数
    size_t thread_count() const;

    // 活跃线程数
    size_t active_threads() const;

    // 空闲线程数
    size_t idle_threads() const;

    // 待完成的任务数量
    size_t pending_tasks() const;

    // 打印线程池状态
    void dump_status() const;
};