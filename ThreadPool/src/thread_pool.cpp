#include "../include/thread_pool.h"

// 工作线程执行[生产消费模型]
void ThreadPool::work(){
    while(true){
        Task task;
        // 取任务逻辑
        {
            std::unique_lock<std::mutex> lock(_mutex);

            _cv.wait(lock, [this]{
                return !_tasks.empty() || _stop || _threads_to_exit > 0;
            });

            // 调度结束并且任务全部完成才退出循环
            if(_stop && _tasks.empty()){
                --_current_threads;
                return;
            }

            if(_threads_to_exit > 0 && _current_threads > _min_threads && _tasks.empty()){
                --_threads_to_exit;
                --_current_threads;
                return;
            }

            if(_tasks.empty()){
                continue; // 完成正在进行的任务
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

// 添加工作线程
void ThreadPool::add_worker(size_t n){
    for(size_t i = 0; i < n; ++i){
        _workers.emplace_back([this]{
            work();
        });
        ++_current_threads;
    }
}

// 检查是否需要扩容
bool ThreadPool::check_expand(){
    return (pending_tasks() > active_threads())
            && (thread_count() < _max_threads);
}

// 扩容
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

// 缩容
void ThreadPool::shrink(){
    if(!check_shrink())
        return;
    ++_threads_to_exit;
    _cv.notify_all();
}

// 核心：扩容，缩容
void ThreadPool::manager(){
    while(!_stop){
        expand();
        shrink();
        // 每隔1秒检查一次
        std::this_thread::sleep_for(std::chrono::seconds(1));
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
    return _current_threads.load();
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

    if(_manager.joinable()){
        _manager.join();
    }

    for(auto& worker : _workers){
        if(worker.joinable()){
            worker.join();
        }
    }
}