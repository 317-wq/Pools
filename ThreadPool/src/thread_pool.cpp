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

        ++_active_threads;

        try{
            task(); // 任务执行
        }catch(const std::exception& e){
            std::cout << e.what() << std::endl;
        }catch(...){
            std::cout << "未知异常" << std::endl;
        }

        // TODO:
        // 当前实现依赖于控制流正常走到这里。
        // 后续可使用 RAII(ScopeGuard)管理 _active_threads。
        // 即使未来出现 return / 新异常路径，也能保证计数正确恢复。
        --_active_threads;
    }
}

bool ThreadPool::check(){

}

// 核心：扩容，缩容
void ThreadPool::manager(){
    while(true){
        if(check()){
            // ...
            ;
        }
    }
}

ThreadPool::ThreadPool(size_t min_thread, size_t max_thread)
    :_manager(std::thread(manager, this))
{
    for(size_t i = 0; i < min_thread; ++i){
        _workers.emplace_back([this]{
            work();
        });
    }
}

// 线程总数
size_t ThreadPool::thread_count() const{
    return _workers.size();
}

// 活跃线程数
size_t ThreadPool::active_threads() const{
    return _active_threads.load();
}

// 待完成的任务数量
size_t ThreadPool::pending_tasks() const{
    std::lock_guard<std::mutex> lock(_mutex);
    return _tasks.size();
}

// 打印线程池状态
void ThreadPool::dump_status() const{
    std::cout
        << "[ThreadPool] "
        << "threads=" << thread_count()
        << " active=" << active_threads()
        << " pending=" << pending_tasks()
        << std::endl;
}

ThreadPool::~ThreadPool(){
    {
        std::unique_lock<std::mutex> lock(_mutex);
        _stop = true;
    }

    _cv.notify_all(); // 唤醒所有线程

    for(auto& worker : _workers){
        if(worker.joinable()){
            worker.join();
        }
    }
}