#include "../include/thread_pool.h"

// 工作线程执行[生产消费模型]
void ThreadPool::work(WorkerInfo* worker){
    while(true){
        Task task;
        // 取任务逻辑
        {
            std::unique_lock<std::mutex> lock(_mutex);

            _cv.wait(lock, [this, worker]{
                return !_tasks.empty() || _stop || worker->exit;
            });

            // 调度结束并且任务全部完成才退出循环
            if(_stop && _tasks.empty()){
                return;
            }

            if(worker->exit){
                return;
            }

            if(!_tasks.empty()){
                task = std::move(_tasks.front());
                _tasks.pop();
            }
        }
        
        worker->idle = false; // 非空闲
        try{
            task(); // 任务执行
        }catch(const std::exception& e){
            std::cout << e.what() << std::endl;
        }catch(...){
            std::cout << "未知异常" << std::endl;
        }
        worker->idle = true; // 恢复空闲状态
    }
}

// 添加工作线程
void ThreadPool::add_worker(size_t n){
    for(size_t i = 0; i < n; ++i){
        auto worker = std::make_unique<WorkerInfo>();
        WorkerInfo* ptr = worker.get();
        ptr->worker = std::thread(&ThreadPool::work, this, ptr);
        _workers.emplace_back(std::move(worker));
    }
}

// 检查是否需要扩容
bool ThreadPool::check_expand(){
    return pending_tasks() > active_threads()
        && thread_count() < _max_threads;
}

// 扩容[一次增加1个]
void ThreadPool::expand(){
    if(!check_expand())
        return;
    
    add_worker(1);
}

// 检查是否需要缩容
bool ThreadPool::check_shrink(){
    return pending_tasks() == 0
        && active_threads() == 0
        && thread_count() > _min_threads;
}

// 缩容[一次减少1个]
void ThreadPool::shrink(){
    if(!check_shrink())
        return;

    for(auto& worker : _workers){
        if(worker->idle && !worker->exit){
            worker->exit = true;
            _cv.notify_all(); // 防止其他线程都在wait
            break;
        }
    }
}

// 核心：扩容，缩容
void ThreadPool::manager(){
    while(!_stop){
        expand();
        shrink();
        // 每隔50ms检查一次
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
}

ThreadPool::ThreadPool(size_t min_threads, size_t max_threads)
    :_min_threads(min_threads)
    ,_max_threads(max_threads)
{
    add_worker(min_threads);
    _manager = std::thread(&ThreadPool::manager, this);
}

// 线程总数
size_t ThreadPool::thread_count() const{
    std::lock_guard lock(_mutex);
    size_t count = 0;
    for(auto &worker : _workers){
        if(!worker->exit){
            ++count;
        }
    }
    return count;  
}

// 活跃线程数
size_t ThreadPool::active_threads() const{
    std::lock_guard lock(_mutex);
    size_t count = 0;
    for(auto &worker : _workers){
        if(!worker->idle){
            ++count;
        }
    }
    return count;
}

// 空闲线程数
size_t ThreadPool::idle_threads() const{
    return thread_count() - active_threads();
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

    if(_manager.joinable()){
        _manager.join();
    }

    for(auto& worker : _workers){
        if(worker->worker.joinable()){
            worker->worker.join();
        }
    }
}