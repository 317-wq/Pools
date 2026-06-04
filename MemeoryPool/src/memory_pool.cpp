#include "../include/memory_pool.h"

#include <cstdlib>
#include <iostream>

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

    _chunks.push_back(memory);

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

// 判断当前指针是否属于这个内存池 [但是无法避免二次deleteObj的情况,造成链表回环]
bool MemoryPool::owns(void *ptr) const    
{
    std::lock_guard<std::mutex> lock(_mutex);
    
    if(ptr == nullptr)
    {
        return false;
    }

    char* p = static_cast<char*>(ptr);

    // 遍历所有的大块内存进行检查
    for (auto memory : _chunks)
    {
        char *begin = memory;
        char *end = memory + _blockCount * _blockSize;

        // 是否落在该大块内存范围内
        if (p >= begin && p < end)
        {
            // 是否正好位于块起始地址，因为从内存池分配的都是块的起始，避免了野指针的可能
            size_t offset = p - begin;

            return offset % _blockSize == 0;
        }
    }
    return false;
}

MemoryPool::MemoryPool(size_t blockCount, size_t blockSize)
    : _freeList(nullptr),
      _blockCount(blockCount),
      _freeCount(0),
      _totalBlockCount(0)
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
    for(auto memory : _chunks)
    {
        std::free(memory);
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

    _freeList = _freeList->next;

    --_freeCount; // 空闲块数量减少

    return block;
}

// 回收内存块
void MemoryPool::deallocate(void *ptr)
{
    if (ptr == nullptr)
    {
        return;
    }

    if(!owns(ptr))
    {
        throw std::invalid_argument("pointer does not belong to memory pool");
    }

    std::lock_guard<std::mutex> lock(_mutex);
    Block *block = static_cast<Block *>(ptr);

    // 回收ptr指向的内存块，头插法归还，O(1)
    block->next = _freeList;

    _freeList = block;

    ++_freeCount; // 空闲块数量增加
}