#pragma once

#include <cstddef>

/*
    内存池
*/

class MemoryPool
{
private:
    // 空闲块节点，那一块内存的前4/8个字节存储的是下一个节点的地址
    // 存在内存对齐的考虑，后续补充对应函数
    struct Block
    {
        Block *next;
    };

private:
    Block *_freeList; // 空闲链表头
    char *_memory; // 整块内存起始地址
    size_t _blockCount; // 块数量
    size_t _blockSize; // 每个块大小
    size_t _freeCount; // 当前空闲内存块数量

private:
    // 内存对齐函数
    size_t alignUp(size_t size, size_t alignment);

public:
    MemoryPool(size_t blockCount, size_t blockSize);

    ~MemoryPool();

public:
    // 分配一个内存块
    void *allocate();

    // 回收一个内存块
    void deallocate(void *ptr);

public:
    // 获取内存块数量
    size_t blockSize() const
    {
        return _blockSize;
    }

    // 获取内存块数量
    size_t blockCount() const
    {
        return _blockCount;
    }

    // 获取空闲内存块数量
    size_t freeCount() const
    {
        return _freeCount;
    }

    // 获取已使用的内存块数量
    size_t usedCount() const
    {
        return _blockCount - _freeCount;
    }

};