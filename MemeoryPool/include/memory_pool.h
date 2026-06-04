#pragma once

#include <cstddef>
#include <new>
#include <utility>
#include <stdexcept>

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

private:
    // 分配一个内存块[相当于placement new，
    // 之后在这块内促你上面直接构造对象，不需要重新申请内存]
    void *allocate();

    // 回收一个内存块
    void deallocate(void *ptr);

    // 判断当前指针是否属于这个内存池，避免deleteObj两次，造成链表回环
    bool owns(void* ptr) const;

public:
    // 利用已申请的内存块，直接在上面构造对象
    template<typename T, typename... Args>
    T* newObject(Args&&... args)
    {
        // 解决定位new带来的安全隐患，需要构造的对象大小远超过内存块大小
        if (sizeof(T) > _blockSize)
        {
            throw std::runtime_error("object size exceeds block size");
        }

        void* memory = allocate();
        if(!memory)
        {
            return nullptr;
        }

        // 定位new
        return new(memory)T(std::forward<Args>(args)...);
    }

    // 销毁对象
    template<typename T>
    void deleteObject(T* obj)
    {
        if(!obj)
        {
            return;
        }

        // 使用了定位new，需要显示调用销毁对象的析构操作
        obj->~T();
        deallocate(obj); // 归还内存块
    }

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