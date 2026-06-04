#include "../include/memory_pool.h"

#include <cstdlib>
#include <iostream>

// 内存对齐函数[将size取整到alignment的最小整数倍]
size_t MemoryPool::alignUp(size_t size, size_t alignment)
{
    return (size + alignment - 1) & ~(alignment - 1);
}

// 判断当前指针是否属于这个内存池，避免deleteObj两次，造成链表回环
bool MemoryPool::owns(void *ptr) const    
{
    if(ptr == nullptr)
    {
        return false;
    }

    char* p = static_cast<char*>(ptr);

    char* begin = _memory;
    char* end = _memory + _blockCount * _blockSize;

    // 是否落在内存池范围内
    if(p < begin || p >= end)
    {
        return false;
    }

    // 是否正好位于块起始地址，因为从内存池分配的都是块的起始，避免了野指针的可能
    size_t offset = p - begin;

    return offset % _blockSize == 0;
}

MemoryPool::MemoryPool(size_t blockCount, size_t blockSize)
    : _freeList(nullptr),
      _memory(nullptr),
      _blockCount(blockCount),
      _freeCount(blockCount)
{
    // Block至少要能存储一个指针的空间大小, sizeof(Block)天然规避了4/8字节的问题
    if (blockSize < sizeof(Block))
    {
        blockSize = sizeof(Block);
    }

    // 内存对齐，用户申请的内存块大小对应到编译器处理，不是对齐的可能
    _blockSize = alignUp(blockSize, alignof(std::max_align_t));

    // 一次性申请所有内存[大块内存]
    _memory = static_cast<char *>(std::malloc(_blockSize * _blockCount));

    if (!_memory)
    {
        throw std::bad_alloc();
    }

    // freelist空闲内存块链表
    _freeList = reinterpret_cast<Block *>(_memory);

    Block *current = _freeList;

    for (size_t i = 0; i < _blockCount - 1; ++i)
    {
        current->next = reinterpret_cast<Block *>(_memory + (i + 1) * _blockSize);
        current = current->next;
    }

    current->next = nullptr;
}

// 只释放大块内存，碎片小内存会重复使用
MemoryPool::~MemoryPool()
{
    std::free(_memory);
}

// 分配内存块
void *MemoryPool::allocate()
{
    std::lock_guard<std::mutex> lock(_mutex);

    // 内存池使用完
    if (_freeList == nullptr)
    {
        return nullptr;
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