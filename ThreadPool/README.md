# C++ ThreadPool

高性能 C++17 线程池，支持**自适应扩缩容**和**工作窃取**。

## 特性

- **自适应扩缩容** — 根据负载自动增减线程，空闲 10s 后回收
- **工作窃取** — 空闲线程从繁忙线程偷取任务，负载自动均衡
- **泛型接口** — `submit(f, args...)` 支持任意可调用对象，自动推导返回值
- **锁分离** — 全局锁仅用于索引计算，任务入队只需单 Worker 锁
- **防惊群** — `notify_one()` + work-stealing 替代 `notify_all()`

## 快速开始

```bash
cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make -j$(nproc)
```

```cpp
#include "thread_pool.h"

int main() {
    // 最少 4 个线程，最多 16 个
    ThreadPool pool(4, 16);

    // 提交任务，返回 std::future
    auto future = pool.submit([](int a, int b) {
        return a + b;
    }, 3, 5);

    int result = future.get();  // 8
}
```

## API

```cpp
ThreadPool pool(4, 16);       // min_threads, max_threads

// 提交任务 → std::future<ReturnType>
auto f = pool.submit(func, arg1, arg2, ...);

// 状态查询
size_t n = pool.thread_count();   // 当前线程数
size_t a = pool.active_threads(); // 活跃线程数
size_t i = pool.idle_threads();   // 空闲线程数
size_t p = pool.pending_tasks();  // 等待中的任务数
pool.dump_status();               // 打印状态
```

## 架构

```
Clients → submit(task)
              │
     ┌────────▼────────┐
     │  ThreadPool Core │
     │  Round-Robin     │
     │  _cv.notify_one()│
     │  Manager(扩缩容)  │
     └───┬──────┬───────┘
         │      │
    ┌────▼──┐ ┌─▼─────┐
    │Worker0│ │Worker1│ ...  ← steal_task() 互相窃取
    │ deque │ │ deque │
    │thread │ │thread │
    └───────┘ └───────┘
```

## 性能 (8 核, 1M 任务)

| 指标 | 数值 |
|------|------|
| 轻量任务 TPS | **63,690** |
| 稳态吞吐 (8 clients) | **106,964** TPS |
| 峰值吞吐 | **163,894** TPS |
| 多客户端 QPS | **84,246** (4 clients) |

## 构建 & 测试

```bash
# 基础测试
cd build && make && ./test

# TPS/QPS 压力测试
./stress_test

# 模拟 Web 服务器并发请求处理测试
./example
```

## 项目结构

```
ThreadPool/
├── include/thread_pool.h   # 头文件
├── src/thread_pool.cpp      # 实现
├── examples/example.cpp    # 模拟 Web 服务器并发请求处理
├── tests/
│   ├── test.cpp             # 基础测试
│   └── stress_test.cpp      # 压力测试
├── docs/
│   ├── architecture.md      # 架构文档
└── CMakeLists.txt
```

## 技术点

- C++17：`std::invoke_result_t`、`std::make_shared`
- 工作窃取：`deque` 头取尾偷、`try_lock` 随机 victim
- 锁优化：锁分离 (split-locking)、`shared_ptr` 生命周期安全
- 线程安全：TLS 指针防死锁、Manager 独立线程扩缩容
- 异常处理：捕获不逃逸，保证线程不 crash
