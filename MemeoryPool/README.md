# MemoryPool — 高性能 C++ 内存池

[![C++17](https://img.shields.io/badge/C%2B%2B-17-blue.svg)](https://en.cppreference.com/w/cpp/17)
[![License](https://img.shields.io/badge/license-MIT-green.svg)](LICENSE)

## 概述

一个**线程安全、可弹性伸缩**的固定大小内存池实现。通过在用户态预分配大块内存并复用小块内存，减少系统调用 `malloc/free` 的频率，同时利用**连续内存分配**提升 CPU 缓存命中率。

### 核心特性

| 特性 | 说明 |
|------|------|
| 线程安全 | `std::mutex` 保护所有关键路径，经过多线程压力测试验证 |
| 弹性伸缩 | 内存不足时自动扩容，空闲时支持收缩，自适应流量变化 |
| 二次释放检测 | `_usedBlocks` 集合追踪已分配块，防止 double-free 导致链表回环 |
| 连续内存 | 同一 Chunk 内的对象物理地址连续，提升 Cache Locality |
| Placement New | 分离内存分配与对象构造，支持任意构造参数完美转发 |
| 统计接口 | `usedCount()` / `freeCount()` / `peakUsed()` / `dumpChunks()` |
| 内存对齐 | 自动对齐到 `std::max_align_t`，兼容 SIMD 等对齐需求 |
| ObjectPool 封装 | 模板类自动推导块大小，类型安全的 RAII 风格接口 |

## 快速开始

```cpp
#include "object_pool.h"

// 创建对象池：预分配 1024 个 int 大小的块
ObjectPool<int> pool(1024);

// 获取对象（在池内存上 placement new）
int* obj = pool.acquire(42);

// 使用对象
*obj += 1;

// 释放对象（调用析构函数 + 归还内存块）
pool.release(obj);
```

### 编译运行

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)

# 运行单元测试
./test

# 运行性能测试
./bench
```

## 架构设计

### 内存布局

```
                         MemoryPool
┌─────────────────────────────────────────────────────────┐
│  _freeList ──> Block -> Block -> Block -> ... -> nullptr│
│               (空闲链表 — 侵入式，复用块内存的前8字节)    │
├─────────────────────────────────────────────────────────┤
│  _chunks: [Chunk 0] [Chunk 1] [Chunk 2]  (按地址排序)    │
│            │         │         │                        │
│            v         v         v                        │
│         ┌──────┐ ┌──────┐ ┌──────┐                      │
│         │malloc│ │malloc│ │malloc│  (大块内存，连续分配) │
│         │16MB  │ │16MB  │ │16MB  │                      │
│         └──────┘ └──────┘ └──────┘                      │
│                                                         │
│  _usedBlocks: {ptr1, ptr2, ...}  (已分配块集合，调试安全) │
└─────────────────────────────────────────────────────────┘
```

### 两层结构

1. **Chunk（大块内存）**：一次性 `malloc` 获取的大块连续内存，按 `_blockCount x _blockSize` 切分为等大小的小块
2. **Block（小块内存）**：单个对象占用的内存区域，空闲时前 8 字节存储 `next` 指针，分配后全部用于对象数据

### 侵入式空闲链表

空闲 Block 的头 8 字节被复用为 `Block* next` 指针，指向下一个空闲块。这种设计**零额外内存开销**——空闲链表不需要单独分配节点。

```
空闲时:  [ next_ptr | 垃圾数据 | 垃圾数据 | ... ]  <-- 复用前8字节
分配后:  [ 对象数据 | 对象数据 | 对象数据 | ... ]  <-- 全部用于对象
```

## 性能分析 (Benchmark)

> 测试环境：Linux x86-64, GCC, `-O2 -DNDEBUG`

### 场景1：单线程 100 万次顺序分配->释放 (16B 对象)

| 方案 | 耗时 | 吞吐量 | 相对 new/delete |
|------|------|--------|-----------------|
| `new/delete` | ~70 ms | ~14K ops/ms | 1.0x (基准) |
| `malloc/free` | ~70 ms | ~14K ops/ms | ~1.0x |
| **MemoryPool** | ~200 ms | ~5K ops/ms | ~0.3x |

> **分析**：单线程场景下 `new/delete` 直接走 glibc 的 per-thread tcache（无锁），而 MemoryPool 每次操作需要 mutex + `_usedBlocks` 哈希表操作。glibc 经过数十年优化，单线程分配极快，这是预期结果。

### 场景2：不同对象大小对比

| 对象大小 | MemoryPool | new/delete | 加速比 |
|----------|-----------|------------|--------|
| 16 B | 14.7 ms | 5.5 ms | 0.4x |
| 64 B | 18.2 ms | 4.3 ms | 0.2x |
| 512 B | 63.2 ms | 44.6 ms | 0.7x |
| **2048 B** | **645 ms** | **906 ms** | **1.4x** |

> **分析**：对于大对象（>512B），`malloc` 可能触发 `mmap` 系统调用，而 MemoryPool 始终在预分配的连续内存中操作，优势开始显现。

### 场景3：随机分配/释放交错（模拟真实负载）

| 方案 | 耗时 | 吞吐量 |
|------|------|--------|
| `new/delete` | ~12 ms | ~41K ops/ms |
| **MemoryPool** | ~40 ms | ~12K ops/ms |

### 场景4：多线程并发 (8 线程 x 10万 ops)

| 方案 | 耗时 | 吞吐量 |
|------|------|--------|
| `new/delete` | ~16 ms | ~48K ops/ms |
| **MemoryPool** | ~189 ms | ~4K ops/ms |

> **分析**：当前使用单一全局锁，多线程竞争激烈。但每个 MemoryPool 实例拥有独立锁——若为不同模块/线程组创建独立 Pool 实例，可避免全局 malloc 锁竞争。

### 场景5：内存局部性 — 连续内存遍历写入

| 方案 | 耗时 |
|------|------|
| **MemoryPool (连续内存)** | **14.4 ms** |
| new/delete (分散内存) | 19.5 ms |

> **关键发现**：MemoryPool 分配的 **1.3x 遍历性能优势** 来自 Cache Locality。同一 Chunk 内的对象在物理内存中连续，大幅提升 L1/L2 缓存命中率。这是内存池相比通用分配器的核心优势。

### 性能总结

| 维度 | MemoryPool | new/delete |
|------|:--:|:--:|
| 单线程分配吞吐量 | | 胜 |
| 多线程分配吞吐量（单 Pool） | | 胜 |
| Cache Locality（遍历） | 胜 | |
| 大对象分配（>512B） | 胜 | |
| 内存碎片控制 | 胜 | |
| 二次释放检测 | 胜 | |
| 通用性（可变大小） | | 胜 |

**适合 MemoryPool 的场景**：固定大小对象 + 频繁创建销毁 + 需要遍历访问（如游戏实体、网络连接池、日志事件）

## 设计决策

### 为什么用侵入式链表？
- **零内存开销**：不需要额外的链表节点分配
- **Cache-friendly**：链表指针与数据在同一缓存行
- **O(1)** 头插/头取

### 为什么用 `std::unordered_set` 追踪已分配块？
- 防御性编程：检测 double-free，避免空闲链表回环
- O(1) 平均查找
- 代价：每次 alloc/free 都要哈希表操作，是主要性能瓶颈之一

### 为什么用全局互斥锁？
- 实现简单、正确性易验证
- 对于多 Pool 实例场景（每个模块独立 Pool），锁竞争很低
- 未来可优化为 per-Chunk 锁或无锁 freelist

### O(log n) Chunk 定位
- Chunks 按内存地址排序存储
- 使用 `std::upper_bound` 二分查找定位指针所属 Chunk
- 替代原有 O(n) 线性扫描

## 测试覆盖

```
[PASSED] basic allocate and free
[PASSED] double free detection
[PASSED] invalid pointer detection
[PASSED] null pointer delete
[PASSED] expand when exhausted
[PASSED] oversized object detection
[PASSED] memory shrink
[PASSED] shrink with partial usage
[PASSED] multithreaded stress test (5000+ ops, 8 threads)
[PASSED] concurrent double delete (TOCTOU)
[PASSED] ObjectPool wrapper
[PASSED] peak used tracking
[PASSED] zero blockCount
[PASSED] expand and shrink cycle
```

## 未来改进方向

- [ ] Per-Chunk 锁 / Lock-free freelist（降低多线程竞争）
- [ ] Debug/Release 模式分离（Release 移除 `_usedBlocks` 换取更高性能）
- [ ] Thread-local Cache（每个线程缓存一批 Block）
- [ ] 支持 `std::allocator` 接口（可直接用于 STL 容器）
- [ ] ASAN/Valgrind 集成

## License

MIT
