#pragma once

#include <cstddef>
#include <new>
#include <utility>
#include <stdexcept>
#include <mutex>
#include <vector>
#include <unordered_set>

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
    std::vector<char *> _chunks; // 整块内存起始地址 -> 可扩容内存池
    size_t _blockCount; // 每个大块内存分成几个小块内存
    size_t _blockSize; // 每个大块内存中的小块内存单独大小
    size_t _freeCount; // 内存池中空闲小块内存的数量
    size_t _totalBlockCount; // 整个内存池的小块内存数量
    mutable std::mutex _mutex; // 保护空闲内存块链表
    std::unordered_set<void*> _usedBlocks; // 已使用的内存块，解决二次free问题
    size_t _peakUsed; // 小块内存使用峰值

public:
    MemoryPool(size_t blockCount, size_t blockSize);

    ~MemoryPool();

private:
    // 内存对齐函数
    size_t alignUp(size_t size, size_t alignment);

    // 分配一个内存块[相当于placement new，
    // 之后在这块内促你上面直接构造对象，不需要重新申请内存]
    void *allocate();

    // 回收一个内存块
    void deallocate(void *ptr);

    // 判断当前指针是否属于这个内存池，避免deleteObj两次，造成链表回环
    bool owns(void* ptr) const;

    // 大块内存扩容
    void expand();
    
    // 是否是已分配的内存块[是否允许回收]
    bool isAllocated(void* ptr);

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

        // 看是否允许回收，避免二次调用对象的析构函数
        if (!isAllocated(obj))
        {
            throw std::runtime_error("double free detected");
        }

        // 使用了定位new，需要显示调用销毁对象的析构操作
        obj->~T();
        deallocate(obj); // 归还内存块
    }

public:
    // 获取每个大块内存分成几个小块内存
    size_t blockCount() const
    {
        return _blockCount;
    }

    // 获取每个小块内存的大小
    size_t blockSize() const
    {
        return _blockSize;
    }

    // 获取总的小块内存数量
    size_t totalBlockCount() const
    {
        return _totalBlockCount;
    }

    // 获取空闲的小内存块数量
    size_t freeCount() const
    {
        std::lock_guard<std::mutex> lock(_mutex);
        return _freeCount;
    }

    // 获取已使用的小块内存数量
    size_t usedCount() const
    {
        std::lock_guard<std::mutex> lock(_mutex);
        return _totalBlockCount - _freeCount;
    }

    size_t peakUsed() const
    {
        std::lock_guard<std::mutex> lock(_mutex);
        return _peakUsed;
    }

};