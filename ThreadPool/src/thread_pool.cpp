#include "../include/thread_pool.h"

// 工作线程执行[生产消费模型]
void ThreadPool::work(){
    while(true){
        Task task;
        // 取任务逻辑
        {
            std::unique_lock<std::mutex> lock(_mutex);

            _cv.wait(lock, [this]{
                return !_tasks.empty() || _stop;
            });

            // 调度结束并且任务全部完成才退出循环
            if(_stop && _tasks.empty()){
                return;
            }

            task = std::move(_tasks.front());
            _tasks.pop();
        }
        task(); // 任务执行
    }
}

ThreadPool::ThreadPool(size_t thread_num){
    for(int i = 0; i < thread_num; ++i){
        _workers.emplace_back([this]{
            work();
        });
    }
}

// 提交任务
void ThreadPool::submit(Task task){
    {
        std::unique_lock<std::mutex> lock(_mutex);
        _tasks.push(std::move(task));
    }
    _cv.notify_one(); // 唤醒任意一个工作线程
}

ThreadPool::~ThreadPool(){
    {
        std::unique_lock<std::mutex> lock(_mutex);
        _stop = true;
    }

    for(auto& worker : _workers){
        if(worker.joinable()){
            worker.join();
        }
    }
}