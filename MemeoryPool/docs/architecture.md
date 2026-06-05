# C++ 高性能内存池 — 架构文档

## 1. 项目背景 & 动机

### 1.1 为什么需要内存池？

在 C++ 中，频繁使用 `new/delete` 或 `malloc/free` 存在以下问题：

- **系统调用开销**：每次 `malloc` 都可能陷入内核态（通过 `brk` 或 `mmap`），频繁分配小块内存时开销显著
- **内存碎片**：长期运行后堆内存碎片化，大块连续内存难以分配，且缓存局部性变差
- **不确定延迟**：`malloc` 的实现复杂度高（如 glibc 的 ptmalloc），分配时间不可预测，不适合实时系统
- **无生命周期管理**：通用分配器不关心对象的语义，无法检测 double-free 或 use-after-free

内存池通过**预分配 + 复用**的模式，将内存管理从「每次向 OS 申请」变为「在用户态池内周转」，大幅减少系统调用并提升缓存命中率。

### 1.2 本项目目标

设计一个**线程安全的固定大小内存池**，具备以下特性：

| 特性 | 说明 |
|------|------|
| **线程安全** | `std::mutex` 保护关键路径，防止并发分配的竞态条件 |
| **弹性伸缩** | 内存不足时自动扩容，空闲时支持收缩，自适应流量变化 |
| **侵入式空闲链表** | 复用 Block 的前 8 字节作为 `next` 指针，零额外内存开销 |
| **二次释放检测** | `_usedBlocks` 集合追踪已分配块，防止 double-free 导致链表回环 |
| **Placement New** | 分离内存分配与对象构造，支持完美转发任意构造参数 |
| **O(log n) 定位** | Chunks 按地址排序，二分查找定位指针归属 |

---

## 2. 核心设计思想

### 2.1 两层内存架构

```
┌──────────────────────────────────────────────────────────┐
│                     MemoryPool                            │
│                                                           │
│  Chunk 层 (大块内存)                                       │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐               │
│  │ Chunk 0  │  │ Chunk 1  │  │ Chunk 2  │  ← malloc 获取│
│  │ 16MB 连续 │  │ 16MB 连续 │  │ 16MB 连续 │    按地址排序 │
│  └──────────┘  └──────────┘  └──────────┘               │
│       │              │              │                     │
│       ▼              ▼              ▼                     │
│  Block 层 (小块内存)                                      │
│  ┌──┬──┬──┬──┐ ┌──┬──┬──┬──┐ ┌──┬──┬──┬──┐            │
│  │B0│B1│B2│B3│ │B0│B1│B2│B3│ │B0│B1│B2│B3│  ← 等大小   │
│  └──┴──┴──┴──┘ └──┴──┴──┴──┘ └──┴──┴──┴──┘    切分      │
│       │              │              │                     │
│       └──────────────┴──────────────┘                     │
│                      │                                    │
│              _freeList (侵入式链表)                         │
│         Block→Block→Block→...→nullptr                    │
└──────────────────────────────────────────────────────────┘
```

**Chunk**：一次性 `malloc` 的大块连续内存（如 16MB），按 `_blockCount × _blockSize` 切分为等大小的 Block。

**Block**：单个对象占用的内存区域。空闲时前 8 字节复用为 `next` 指针，分配后全部用于对象数据。

### 2.2 侵入式空闲链表

```
空闲 Block 内存布局:
┌──────────┬──────────────────────────────┐
│ next ptr │      (未使用/垃圾数据)         │
│  8 bytes │      _blockSize - 8 bytes    │
└──────────┴──────────────────────────────┘
     │
     ▼ 指向下一个空闲 Block

已分配 Block 内存布局:
┌─────────────────────────────────────────┐
│           对象数据 (placement new)        │
│           _blockSize bytes              │
└─────────────────────────────────────────┘
```

**关键设计**：空闲链表不需要额外分配节点——链表指针就存在空闲块的内存里。这是 Linux 内核 `kmem_cache` 和众多内存分配器（jemalloc、tcmalloc）的标准做法。

### 2.3 扩容与收缩

```
      allocate() 发现 _freeList == nullptr
              │
              ▼
         expand()
              │
    ┌─────────┼──────────┐
    │ malloc 新 Chunk     │
    │ 初始化 Block 链表    │
    │ 连接到 _freeList    │  ← 新块插到链表头部
    │ _totalBlockCount += │
    └─────────────────────┘

      shrink() (用户主动调用或定时触发)
              │
    ┌─────────┼──────────┐
    │ 检查 chunks[1..]    │
    │ 的 usedCount == 0   │  ← 只回收完全空闲的 Chunk
    │ free 多余 malloc     │
    │ 重建 chunk[0] 链表   │
    └─────────────────────┘
```

**防抖动**：缩容只释放完全空闲的 Chunk（chunks[1..] 的 usedCount 都为 0），chunk[0] 始终保留作为最小内存占用。

### 2.4 O(log n) Chunk 定位

```
Chunks 按内存地址排序:
┌──────────┬──────────┬──────────┐
│ Chunk 0  │ Chunk 1  │ Chunk 2  │
│ 0x1000   │ 0x2000   │ 0x3000   │
└──────────┴──────────┴──────────┘

查询 ptr=0x2500:
  std::upper_bound → 第一个 memory > 0x2500 的是 Chunk2(0x3000)
  --it → Chunk1(0x2000)
  检查: 0x2000 ≤ 0x2500 < 0x2000 + size? → YES → 返回 Chunk1
```

- `expand()` 时使用 `std::lower_bound` 保持插入有序
- 查询时使用 `std::upper_bound` 二分查找，O(log n)

---

## 3. 系统架构

### 3.1 类图

```
┌─────────────────────────────────────────────────┐
│                  MemoryPool                      │
├─────────────────────────────────────────────────┤
│  - _freeList: Block*          // 空闲链表头       │
│  - _chunks: vector<Chunk>     // 大块内存(已排序)  │
│  - _usedBlocks: unordered_set // 已分配块集合     │
│  - _blockCount / _blockSize   // 块配置          │
│  - _freeCount / _totalBlockCount / _peakUsed    │
│  - _mutex: mutex              // 全局互斥锁       │
├─────────────────────────────────────────────────┤
│  + newObject<T>(args...) → T* // placement new  │
│  + deleteObject<T>(obj)       // 显式析构+归还   │
│  + shrink()                   // 收缩内存        │
│  + dumpChunks()               // 统计输出        │
│  + freeCount() / usedCount() / peakUsed()       │
├─────────────────────────────────────────────────┤
│  - allocate() → void*         // 从空闲链表取块   │
│  - deallocate(void*)          // 归还块到空闲链表  │
│  - owns(void*) → bool         // 指针归属判断     │
│  - findChunk(void*) → Chunk*  // O(log n) 定位   │
│  - expand()                   // 扩容            │
│  - alignUp()                  // 内存对齐        │
└─────────────────────────────────────────────────┘
         │
         │ 模板封装
         ▼
┌─────────────────────────────────────────────────┐
│               ObjectPool<T>                      │
├─────────────────────────────────────────────────┤
│  - _memoryPool: MemoryPool                      │
├─────────────────────────────────────────────────┤
│  + acquire(args...) → T*     // 构造对象         │
│  + release(T*)               // 销毁对象         │
│  + shrink() / dumpChunks()   // 委托给 MemoryPool │
│  + freeCount() / usedCount() / peakUsed()       │
└─────────────────────────────────────────────────┘
```

### 3.2 核心数据结构

```cpp
// 空闲链表节点（侵入式，复用 Block 的前 8 字节）
struct Block {
    Block *next;  // 仅当 Block 空闲时有效
};

// 大块内存
struct Chunk {
    char  *memory;       // malloc 返回的原始指针
    size_t blockCount;   // 此 Chunk 包含的 Block 数量
    size_t usedCount;    // 当前已分配的 Block 数量
};
```

### 3.3 关键设计决策

| 决策 | 选择 | 理由 |
|------|------|------|
| 链表类型 | 侵入式单链表 | 零额外内存，头插头取 O(1) |
| 大块管理 | `std::vector<Chunk>` 按地址排序 | 支持二分查找 O(log n) + 动态扩容 |
| 线程安全 | 单一 `std::mutex` | 实现简单、正确性易验证；多 Pool 实例场景下锁竞争低 |
| 对象构造 | Placement New | 分离内存分配和对象构造，支持任意构造函数参数 |
| 二次释放防护 | `std::unordered_set<void*>` | O(1) 查找，防御性编程，代价是每次操作多一次哈希查找 |
| 内存对齐 | `alignof(std::max_align_t)` | 兼容所有标准类型的对齐要求 |

---

## 4. 核心业务流程

### 4.1 对象分配流程

```
newObject<T>(args...)
    │
    ├── 1. sizeof(T) > _blockSize? ──→ 抛异常
    │
    ├── 2. allocate()
    │       ├── lock(_mutex)
    │       ├── _freeList == nullptr? ──→ expand() 扩容
    │       ├── block = _freeList
    │       ├── _usedBlocks.insert(block)     // 标记为已使用
    │       ├── _freeList = _freeList->next   // 从链表移除
    │       ├── --_freeCount
    │       ├── findChunk(block)->usedCount++  // O(log n) 定位
    │       └── unlock(_mutex)
    │
    ├── 3. 内存为空? ──→ return nullptr
    │
    └── 4. new(memory) T(args...)  ← Placement New
            └── return T*
```

### 4.2 对象释放流程

```
deleteObject<T>(obj)
    │
    ├── obj == nullptr? ──→ return
    │
    ├── 1. 原子区间 (lock _mutex)
    │       ├── owns(obj)? ──→ 否 → 抛异常
    │       ├── obj 在 _usedBlocks 中? ──→ 否 → 抛异常 (double free)
    │       ├── _usedBlocks.erase(obj)         // 先标记为"已释放"
    │       ├── findChunk(obj)->usedCount--     // O(log n) 定位
    │       └── unlock(_mutex)
    │
    ├── 2. obj->~T()    ← 显式析构 (锁外执行，避免死锁)
    │
    └── 3. deallocate(obj)
            ├── lock(_mutex)
            ├── block->next = _freeList   ← 头插法
            ├── _freeList = block
            ├── ++_freeCount
            └── unlock(_mutex)
```

**关键安全设计**：步骤 1 原子地将指针从 `_usedBlocks` 移除。如果两个线程同时 `deleteObject` 同一指针，只有一个能成功移除并执行析构，另一个会因 `_usedBlocks.find()` 返回 `end()` 而抛异常。这解决了 TOCTOU 竞态条件。

### 4.3 扩容流程

```
allocate() 发现 _freeList == nullptr
    │
    ▼
expand()  [调用者已持有 _mutex]
    │
    ├── malloc(_blockCount * _blockSize)
    │       └── 失败 → throw std::bad_alloc()
    │
    ├── 构建 Chunk，二分查找插入位置，保持排序
    │
    ├── 初始化 Block 链表:
    │       for i in 0.._blockCount-2:
    │           block[i].next = &block[i+1]
    │       block[last].next = _freeList  ← 与旧链表连接
    │
    ├── _freeList = &block[0]  ← 新块在链表头部
    ├── _totalBlockCount += _blockCount
    └── _freeCount += _blockCount
```

### 4.4 收缩流程

```
shrink()
    │
    ├── lock(_mutex)
    ├── _chunks.size() <= 1? ──→ 不收缩
    │
    ├── for i in 1.._chunks.size()-1:
    │       _chunks[i].usedCount > 0? ──→ 不收缩 (有在用块)
    │
    ├── 释放 chunks[1..] 的 malloc 内存
    ├── _chunks.resize(1)  保留 chunk[0]
    │
    ├── 重建空闲链表 (仅使用 chunk[0] 的空闲块):
    │       for i in 0.._blockCount-1:
    │           if block[i] 不在 _usedBlocks 中:
    │               头插入 _freeList
    │
    ├── _totalBlockCount = _blockCount
    ├── _freeCount = 空闲块数量
    └── unlock(_mutex)
```

---

## 5. 技术实现细节

### 5.1 泛型对象管理

```cpp
template<typename T, typename... Args>
T* newObject(Args&&... args) {
    if (sizeof(T) > _blockSize)
        throw std::runtime_error("object size exceeds block size");
    void* memory = allocate();
    if (!memory) return nullptr;
    return new(memory) T(std::forward<Args>(args)...);
}
```

- **编译期大小检查**：`sizeof(T) > _blockSize` 在编译期即可确定，防止越界写入
- **完美转发**：`std::forward<Args>(args)...` 保持参数的左值/右值属性
- **Placement New**：在已分配的内存上构造对象，不触发额外的内存分配

### 5.2 线程安全策略

| 操作 | 锁策略 | 说明 |
|------|--------|------|
| `allocate()` | `lock_guard(_mutex)` 全程 | 保护 `_freeList`、`_usedBlocks`、`_chunks` |
| `deallocate()` | `lock_guard(_mutex)` 全程 | 仅操作 `_freeList`，轻量 |
| `deleteObject()` | 两段锁 + 中段无锁 | 原子标记阶段有锁，析构无锁，归还阶段有锁 |
| `owns()` / `findChunk()` | 调用者持有锁 | 内部不加锁，由外层保证 |
| `dumpChunks()` / 统计 | `lock_guard(_mutex)` | const 方法通过 `mutable mutex` 加锁 |

### 5.3 内存对齐

```cpp
_blockSize = alignUp(blockSize, alignof(std::max_align_t));
// alignUp: (size + alignment - 1) & ~(alignment - 1)
```

- 将用户指定的 `blockSize` 向上取整到平台最大对齐边界
- 确保 placement new 的对象满足任何类型的对齐要求
- 如果 `blockSize < sizeof(Block)`，自动提升到至少能存下一个指针

### 5.4 安全防护

| 防护 | 机制 | 位置 |
|------|------|------|
| 二次释放 | `_usedBlocks.find()` → 抛异常 | `deleteObject()` |
| 非法指针 | `owns()` 检查块边界和对齐 | `deleteObject()` |
| 超大对象 | `sizeof(T) > _blockSize` → 抛异常 | `newObject()` |
| 空指针释放 | `if (!obj) return` 安全处理 | `deleteObject()` |
| 内存泄漏检测 | 析构函数检查 `_usedBlocks` | `~MemoryPool()` |

---

## 6. 性能表现

### 6.1 Benchmark 结果摘要

> 测试环境：Linux x86-64, GCC, `-O2 -DNDEBUG`

| 场景 | MemoryPool | new/delete | 结论 |
|------|-----------|------------|------|
| 单线程吞吐量 (16B) | ~5K ops/ms | ~14K ops/ms | glibc tcache 更快（预期内） |
| 大对象分配 (2048B) | ~645 ms | ~906 ms | **MemoryPool 快 1.4x** |
| 内存遍历 (Cache Locality) | ~14.4 ms | ~19.5 ms | **MemoryPool 快 1.3x** |

### 6.2 性能分析

- **Cache Locality 是核心优势**：同一 Chunk 的对象在物理内存中连续，CPU 预取器可以提前加载后续对象的数据
- **单线程吞吐量不如 glibc tcache**：glibc 的 per-thread cache 是无锁的，而 MemoryPool 需要 mutex + 哈希表，这是设计上的 trade-off
- **大对象优势**：`malloc` 对大对象可能触发 `mmap` 系统调用，MemoryPool 始终在预分配内存中操作
- **多 Pool 场景**：每个 Pool 实例有独立锁，避免全局 malloc 锁竞争

---

## 7. 不足 & 优化方向

### 7.1 当前不足

| 问题 | 影响 | 根因 |
|------|------|------|
| `_usedBlocks` 哈希表开销 | 每次 alloc/free 额外 O(1) 哈希操作 | 防御性编程 vs 性能的 trade-off |
| 全局锁 | 多线程吞吐量受限 | 单一 `_mutex` 串行化所有操作 |
| 无法 DEBUG/RELEASE 切换 | Release 也承担安全检查开销 | 缺少条件编译 |
| O(log n) 对于单 Chunk | 二分查找对常见情况（1 个 Chunk）过度设计 | 多个 Chunk 只在频繁扩容时出现 |

### 7.2 优化方向

1. **DEBUG/RELEASE 模式**：`#ifdef MEMORY_POOL_DEBUG` 控制 `_usedBlocks`，Release 下移除哈希表开销
2. **Per-Chunk 锁 / Lock-free freelist**：降低多线程竞争
3. **Thread-local Cache**：每个线程缓存一小批 Block，减少全局锁获取频率
4. **单 Chunk 快速路径**：当 `_chunks.size() == 1` 时跳过二分查找
5. **支持 `std::allocator` 接口**：可直接用于 STL 容器如 `std::vector`
6. **Benchmark CI**：自动化性能回归测试，防止性能退化
