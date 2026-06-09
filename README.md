# Pools — C++ 高性能池化技术

[![C++17](https://img.shields.io/badge/C%2B%2B-17-blue.svg)](https://en.cppreference.com/w/cpp/17)
[![License](https://img.shields.io/badge/license-MIT-green.svg)](LICENSE)

本项目包含两个**生产级** C++ 池化技术实现，均采用现代 C++17 标准编写，追求高性能、线程安全与工程实用性。

| 子项目 | 简介 |
|--------|------|
| [**MemoryPool**](MemeoryPool/) | 固定大小对象的内存池，通过预分配 + 复用减少系统调用，提升 Cache Locality |
| [**ThreadPool**](ThreadPool/) | 自适应线程池，支持弹性扩缩容与工作窃取，高并发任务调度 |

---

## 子项目概览

### 1. MemoryPool（内存池）

> 详细文档：[README](MemeoryPool/README.md) | [架构文档](MemeoryPool/docs/architecture.md)

**线程安全、可弹性伸缩**的固定大小内存池。通过用户态预分配大块内存并复用小块内存，减少 `malloc/free` 频率，同时利用连续内存分配提升 CPU 缓存命中率。

**核心特性：**

- **侵入式空闲链表** — 复用空闲 Block 的前 8 字节作为 next 指针，零额外内存开销
- **两层架构** — Chunk（大块连续内存）+ Block（等大小切分），O(log n) 二分查找定位
- **弹性伸缩** — 内存不足自动扩容，空闲 Chunk 支持收缩释放
- **二次释放检测** — `_usedBlocks` 集合追踪已分配块，防止 double-free 导致链表回环
- **Placement New** — 分离内存分配与对象构造，支持任意构造参数完美转发
- **ObjectPool 封装** — 模板类自动推导块大小，类型安全的 RAII 风格接口

**快速示例：**

```cpp
#include "object_pool.h"

ObjectPool<int> pool(1024);      // 预分配 1024 个 int 大小的块
int* obj = pool.acquire(42);     // placement new 构造
*obj += 1;
pool.release(obj);               // 析构 + 归还
```

**关键性能数据：**

| 场景 | MemoryPool | new/delete | 优势 |
|------|:--:|:--:|------|
| 大对象分配 (2048B) | 645 ms | 906 ms | **1.4x 更快** |
| 连续内存遍历 | 14.4 ms | 19.5 ms | **1.3x 更快 (Cache Locality)** |

---

### 2. ThreadPool（线程池）

> 详细文档：[README](ThreadPool/README.md) | [架构文档](ThreadPool/docs/architecture.md)

**自适应扩缩容**的高性能线程池，支持**工作窃取**负载均衡。适用于 Web 服务器请求处理、计算任务并行调度等场景。

**核心特性：**

- **自适应扩缩容** — Manager 线程周期性监控，空闲 10s 后回收，任务堆积时自动扩容
- **工作窃取** (Work Stealing) — 空闲线程从繁忙线程队列尾部窃取任务，负载自动均衡
- **锁分离** — 全局锁仅用于索引计算，任务入队只需单 Worker 锁，多客户端可并发提交
- **防惊群** — `notify_one()` + work-stealing 替代 `notify_all()`，v2.0 性能提升 4.6x
- **泛型接口** — `submit(f, args...)` 支持任意可调用对象，自动推导返回值类型

**快速示例：**

```cpp
#include "thread_pool.h"

ThreadPool pool(4, 16);          // 最少 4 线程，最多 16 线程
auto future = pool.submit([](int a, int b) {
    return a + b;
}, 3, 5);
int result = future.get();       // 8
```

**关键性能数据 (8 核, 1M 任务)：**

| 指标 | 数值 |
|------|------|
| 轻量任务 TPS | **63,690** |
| 稳态吞吐 (8 clients) | **106,964** TPS |
| 峰值吞吐 | **163,894** TPS |

---

## 设计哲学

两个子项目共享相同的设计原则：

| 原则 | MemoryPool 体现 | ThreadPool 体现 |
|------|----------------|----------------|
| **预分配 + 复用** | 预分配 Chunk，Block 周转复用 | 预创建线程，复用线程生命周期 |
| **用户态管理** | 绕过内核 `malloc/free` | 绕过内核 `clone/exit` |
| **弹性伸缩** | 按需扩容/收缩 Chunk | 按需扩缩容 Worker |
| **防御性编程** | double-free 检测、指针归属校验 | 异常捕获不逃逸、退出顺序保证 |
| **性能可测** | Benchmark 对比 new/delete | Stress Test TPS/QPS/延迟 |

---

## 项目结构

```
Pools/
├── README.md                       # 本文件 — 项目总览
├── MemeoryPool/                    # 内存池子项目
│   ├── README.md                   # 快速开始 & API
│   ├── docs/architecture.md        # 架构设计文档
│   ├── include/memory_pool.h       # MemoryPool 核心实现
│   ├── include/object_pool.h       # ObjectPool 模板封装
│   ├── src/memory_pool.cpp         # 实现文件
│   ├── benchmarks/bench.cpp        # 性能基准测试
│   ├── tests/test.cpp             # 单元测试
│   ├── examples/example.cpp       # 使用示例
│   └── CMakeLists.txt
└── ThreadPool/                     # 线程池子项目
    ├── README.md                   # 快速开始 & API
    ├── docs/architecture.md        # 架构设计文档
    ├── include/thread_pool.h       # ThreadPool 头文件
    ├── src/thread_pool.cpp         # 实现文件
    ├── tests/test.cpp              # 基础测试
    ├── tests/stress_test.cpp       # TPS/QPS 压力测试
    ├── examples/example.cpp        # 模拟 Web 服务器并发处理
    └── CMakeLists.txt
```

---

## 构建 & 测试

每个子项目独立构建，均使用 CMake + C++17。

### MemoryPool

```bash
cd MemeoryPool && mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)

# 运行单元测试
./test

# 运行性能基准测试
./bench
```

### ThreadPool

```bash
cd ThreadPool && mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)

# 运行基础测试
./test

# 运行 TPS/QPS 压力测试
./stress_test

# 运行模拟 Web 服务器示例
./example
```

---

## 适用场景

| 场景 | 推荐方案 |
|------|----------|
| 游戏实体管理（固定大小、频繁创建销毁、需要遍历） | MemoryPool |
| 网络连接池（连接对象复用、生命周期明确） | MemoryPool |
| 高频日志事件（小对象、高频分配释放） | MemoryPool |
| Web 服务器请求处理（大量并发短任务） | ThreadPool |
| 计算密集型任务并行调度 | ThreadPool |
| 数据库连接池（长生命周期、需要并发控制） | ThreadPool |

---

## 技术栈

- **语言标准**：C++17（`std::invoke_result_t`、`std::make_shared`、完美转发等）
- **构建系统**：CMake
- **线程安全**：`std::mutex`、`std::condition_variable`、`std::atomic`
- **测试**：自包含断言宏（无需外部测试框架）

---
