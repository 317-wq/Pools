#include "../include/memory_pool.h"

#include <cstdlib>
#include <iostream>
#include <algorithm>

// 内存对齐函数[将size取整到alignment的最小整数倍]
size_t MemoryPool::alignUp(size_t size, size_t alignment)
{
    return (size + alignment - 1) & ~(alignment - 1);
}

// 大块内存扩容
void MemoryPool::expand()
{
    // 一次性申请所有内存[大块内存]
    char *memory = static_cast<char *>(std::malloc(_blockCount * _blockSize));

    if (!memory)
    {
        throw std::bad_alloc();
    }

    // 保持 chunks 按内存地址排序，支持 O(log n) 二分查找定位
    Chunk newChunk(memory, _blockCount);
    auto pos = std::lower_bound(_chunks.begin(), _chunks.end(), memory,
        [](const Chunk &chunk, const char *addr) {
            return chunk.memory < addr;
        });
    _chunks.insert(pos, newChunk);

    Block *first =reinterpret_cast<Block *>(memory);

    Block *current = first;

    for (size_t i = 0; i < _blockCount - 1; ++i)
    {
        current->next = reinterpret_cast<Block *>(memory + (i + 1) * _blockSize);
        current = current->next;
    }

    // 将新的大块内存尾部与上一块内存的_freelist相连，构成一整块新的空闲内存块
    // 物理地址是连续的对于同一块大块内存，从逻辑层面上将，这两个链表应该是两块[中间可能存在截取的可能]
    current->next = _freeList;

    _freeList = first;

    _totalBlockCount += _blockCount;
    _freeCount += _blockCount;
}

// 内存收缩，避免流量的不同造成内存的占用，避免抖动
void MemoryPool::shrink()
{
    std::lock_guard<std::mutex> lock(_mutex);
    // 只保留第一块大块内存
    if (_chunks.size() <= 1)
    {
        return;
    }

    // 检查除第一块外的其他大块内存是否还有被使用中的小块内存
    bool canShrink = true;
    for (size_t i = 1; i < _chunks.size(); ++i)
    {
        if (_chunks[i].usedCount > 0)
        {
            canShrink = false;
            break;
        }
    }
    if (!canShrink)
    {
        return;
    }

    char *firstChunk = _chunks.front().memory;
    // 释放剩余内存空间
    for (size_t i = 1; i < _chunks.size(); ++i)
    {
        std::free(_chunks[i].memory);
    }

    // 只保留第一块大块内存
    size_t freedChunks = _chunks.size() - 1;
    _chunks.resize(1);

    // 重新构建空闲链表，仅使用第一块大块内存中未被占用的块[物理地址连续]
    _freeList = nullptr;

    size_t freeInFirstChunk = 0;
    for (size_t i = 0; i < _blockCount; ++i)
    {
        Block *block = reinterpret_cast<Block *>(firstChunk + i * _blockSize);
        if (_usedBlocks.find(block) == _usedBlocks.end())
        {
            block->next = _freeList;
            _freeList = block;
            ++freeInFirstChunk;
        }
    }

    _totalBlockCount = _blockCount;

    _freeCount = freeInFirstChunk;
}

// 统计
void MemoryPool::dumpChunks() const
{
    std::lock_guard<std::mutex> lock(_mutex);

    std::cout
        << "========== Chunks =========="
        << std::endl;

    for (size_t i = 0; i < _chunks.size(); ++i)
    {
        const auto &chunk = _chunks[i];

        std::cout
            << "Chunk["
            << i
            << "] "
            << " blocks="
            << chunk.blockCount
            << " used="
            << chunk.usedCount
            << std::endl;
    }

    std::cout
        << "============================"
        << std::endl;
}

// 定位指针所属的大块内存（调用者必须持有 _mutex），O(log n) 二分查找
MemoryPool::Chunk* MemoryPool::findChunk(void* ptr)
{
    if (ptr == nullptr || _chunks.empty())
    {
        return nullptr;
    }

    char* p = static_cast<char*>(ptr);

    // 二分查找：chunks 按 memory 地址排序
    auto it = std::upper_bound(_chunks.begin(), _chunks.end(), p,
        [](const char* addr, const Chunk& chunk) {
            return addr < chunk.memory;
        });

    if (it == _chunks.begin())
    {
        return nullptr; // 指针在所有 chunk 之前
    }

    --it;
    char* begin = it->memory;
    char* end = begin + it->blockCount * _blockSize;

    if (p >= begin && p < end)
    {
        return &(*it);
    }
    return nullptr;
}

// 判断当前指针是否属于这个内存池 [但是无法避免二次deleteObj的情况,造成链表回环]
// 注意：调用者必须已持有 _mutex 锁
bool MemoryPool::owns(void *ptr) const
{
    if(ptr == nullptr)
    {
        return false;
    }

    // 复用 findChunk 的二分查找逻辑
    // const_cast: findChunk 返回非 const 指针，但 owns 是 const 方法，这里仅用于判断归属
    Chunk* chunk = const_cast<MemoryPool*>(this)->findChunk(ptr);
    if (chunk == nullptr)
    {
        return false;
    }

    // 是否正好位于块起始地址，因为从内存池分配的都是块的起始，避免了野指针的可能
    char* p = static_cast<char*>(ptr);
    size_t offset = p - chunk->memory;
    return offset % _blockSize == 0;
}

MemoryPool::MemoryPool(size_t blockCount, size_t blockSize)
    : _freeList(nullptr),
      _blockCount(blockCount),
      _freeCount(0),
      _totalBlockCount(0),
      _peakUsed(0)
{
    if(blockCount == 0)
    {
        throw std::invalid_argument("blockCount must be > 0");
    }

    // Block至少要能存储一个指针的空间大小, sizeof(Block)天然规避了4/8字节的问题
    if (blockSize < sizeof(Block))
    {
        blockSize = sizeof(Block);
    }

    // 内存对齐，用户申请的内存块大小对应到编译器处理，不是对齐的可能
    _blockSize = alignUp(blockSize, alignof(std::max_align_t));
    // 构建大块内存块[扩容]
    expand();
}

// 只释放大块内存，碎片小内存会重复使用
MemoryPool::~MemoryPool()
{
    // 确保使用的小块内存全部归还，避免造成内存泄漏，方便后面一次性销毁
    if(!_usedBlocks.empty())
    {
        std::cerr << "[MemoryPool] Leak Detected" << std::endl;
        std::cerr << "active blocks = " << _usedBlocks.size() << std::endl;
    }

    // 一次性销毁
    for(auto& chunk : _chunks)
    {
        std::free(chunk.memory);
    }
}

// 分配内存块
void *MemoryPool::allocate()
{
    std::lock_guard<std::mutex> lock(_mutex);

    // 内存池使用完
    if (_freeList == nullptr)
    {
        // 扩容 -> 再次开辟大块内存
        expand();
    }

    Block *block = _freeList;

    _usedBlocks.insert(block); // 添加到使用内存块中
    if (_usedBlocks.size() > _peakUsed)
    {
        _peakUsed = _usedBlocks.size();
    }

    _freeList = _freeList->next;

    --_freeCount; // 空闲块数量减少

    // 定位 block 属于哪一个大块内存，进行计数 O(log n)
    Chunk *chunk = findChunk(block);
    if (chunk)
    {
        ++chunk->usedCount;
    }

    return block;
}

// 回收内存块[将内存块归还到空闲链表，调用者必须已从 _usedBlocks 中移除并递减了 chunk.usedCount]
void MemoryPool::deallocate(void *ptr)
{
    if (ptr == nullptr)
    {
        return;
    }

    std::lock_guard<std::mutex> lock(_mutex);

    Block *block = static_cast<Block *>(ptr);

    // 头插法归还，O(1)
    block->next = _freeList;

    _freeList = block;

    ++_freeCount; // 空闲块数量增加
}