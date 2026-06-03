#include "../include/thread_pool.h"

thread_local ThreadPool::WorkerInfo* ThreadPool::tls_worker = nullptr;

// 工作线程执行[生产消费模型 + 工作窃取]
void ThreadPool::work(WorkerInfo* worker){
    tls_worker = worker;
    while(true){
        Task task;

        // 1. 优先处理自己的任务队列（无锁快速路径）
        {
            std::unique_lock<std::mutex> lock(worker->mutex, std::try_to_lock);
            if (lock.owns_lock() && !worker->tasks.empty()) {
                task = std::move(worker->tasks.front());
                worker->tasks.pop_front();
            }
        }

        // 2. 自己队列为空，尝试窃取其他线程的任务
        if (!task) {
            steal_task(worker, task);
        }

        // 3. 仍然没有任务，等待条件变量通知
        if (!task) {
            std::unique_lock<std::mutex> lock(worker->mutex);
            // 双重检查：等待期间可能有任务被放入
            if (worker->tasks.empty() && !_stop && !worker->exit) {
                _cv.wait(lock);
            }
            // 被唤醒后取任务
            if (!worker->tasks.empty()) {
                task = std::move(worker->tasks.front());
                worker->tasks.pop_front();
            }
        }

        // 处理退出
        if (!task) {
            if (_stop && worker->tasks.empty()) {
                worker->stopped = true;
                return;
            }
            if (worker->exit && worker->tasks.empty()) {
                worker->stopped = true;
                return;
            }
            // 虚假唤醒，继续循环尝试获取任务
            continue;
        }

        // 4. 执行任务
        worker->idle = false;
        try {
            task();
        } catch (const std::exception& e) {
            std::cout << e.what() << std::endl;
        } catch (...) {
            std::cout << "未知异常" << std::endl;
        }
        worker->last_active = ThreadPool::Clock::now();
        worker->idle = true;
    }
}

// 偷取其他线程的任务[从尾部偷取，减少锁竞争]
bool ThreadPool::steal_task(WorkerInfo *self, Task &task){
    // 快照 worker 的 shared_ptr，避免长时间持有全局锁
    std::vector<std::shared_ptr<WorkerInfo>> workers_snapshot;
    {
        std::lock_guard<std::mutex> lock(_worker_mutex);
        workers_snapshot.reserve(_workers.size());
        for (auto& w : _workers) {
            if (w.get() != self) {
                workers_snapshot.push_back(w);  // shared_ptr 保证安全
            }
        }
    }

    // 随机起点，避免所有窃取者争抢同一个 victim
    size_t n = workers_snapshot.size();
    if (n == 0) return false;
    size_t start = reinterpret_cast<uintptr_t>(self) % n;

    for (size_t i = 0; i < n; ++i) {
        auto& victim = workers_snapshot[(start + i) % n];
        // try_lock 避免阻塞：如果 victim 正在操作自己的队列，跳过
        std::unique_lock<std::mutex> lock(victim->mutex, std::try_to_lock);
        if (!lock.owns_lock() || victim->tasks.empty()) {
            continue;
        }
        task = std::move(victim->tasks.back());
        victim->tasks.pop_back();
        return true;
    }
    return false;
}

// 添加工作线程
void ThreadPool::add_worker(size_t n){
    std::lock_guard<std::mutex> lock(_worker_mutex);
    for (size_t i = 0; i < n; ++i) {
        auto w = std::make_shared<WorkerInfo>();
        WorkerInfo* ptr = w.get();
        ptr->worker = std::thread(&ThreadPool::work, this, ptr);
        _workers.emplace_back(std::move(w));
    }
}

// 检查是否需要扩容
bool ThreadPool::check_expand() const{
    return pending_tasks() > active_threads()
        && thread_count() < _max_threads;
}

// 扩容[一次增加1个]
void ThreadPool::expand(){
    if (!check_expand())
        return;
    add_worker(1);
}

// 检查是否需要缩容
bool ThreadPool::check_shrink() const{
    return idle_threads() > 0
        && thread_count() > _min_threads;
}

// 超时空闲缩容
bool ThreadPool::timeout_shrink(const WorkerInfo* worker) const{
    using namespace std::chrono;
    auto now = ThreadPool::Clock::now();
    auto gap = duration_cast<seconds>(now - worker->last_active);
    return gap.count() >= 10;
}

// 缩容[一次减少1个]
void ThreadPool::shrink(){
    if (!check_shrink())
        return;
    if (thread_count() <= _min_threads)
        return;
    std::lock_guard<std::mutex> lock(_worker_mutex);
    for (auto& worker : _workers) {
        if (worker->idle && !worker->exit && timeout_shrink(worker.get())) {
            worker->exit = true;
            _cv.notify_all();  // 唤醒以确保退出信号被处理
            break;
        }
    }
}

// 清理每次扩缩容之后退出的线程
void ThreadPool::clean_worker(){
    std::lock_guard<std::mutex> lock(_worker_mutex);
    auto it = _workers.begin();
    while (it != _workers.end()) {
        auto& worker = *it;
        if (worker->stopped) {
            if (worker->worker.joinable()) {
                worker->worker.join();
            }
            it = _workers.erase(it);
        } else {
            ++it;
        }
    }
}

// 核心：扩容，缩容
void ThreadPool::manager(){
    while (!_stop) {
        expand();
        shrink();
        clean_worker();
        // 自适应检查间隔：任务堆积时加快检查频率
        size_t pending = pending_tasks();
        auto interval = (pending > active_threads() * 2)
            ? std::chrono::milliseconds(5)   // 任务堆积严重，快速扩容
            : std::chrono::milliseconds(20);  // 正常情况，降低开销
        std::this_thread::sleep_for(interval);
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
    std::lock_guard<std::mutex> lock(_worker_mutex);
    return _workers.size();
}

// 活跃线程数
size_t ThreadPool::active_threads() const{
    std::lock_guard<std::mutex> lock(_worker_mutex);
    size_t count = 0;
    for (auto &worker : _workers) {
        if (!worker->idle && !worker->stopped) {
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
    std::lock_guard<std::mutex> lock(_worker_mutex);
    size_t count = 0;
    for (auto& worker : _workers) {
        std::lock_guard<std::mutex> glock(worker->mutex);
        count += worker->tasks.size();
    }
    return count;
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
    _stop = true;

    _cv.notify_all(); // 唤醒所有线程

    if (_manager.joinable()) {
        _manager.join();
    }

    for (auto& worker : _workers) {
        if (worker->worker.joinable()) {
            worker->worker.join();
        }
    }
}