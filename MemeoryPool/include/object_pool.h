#pragma once

#include "memory_pool.h"

template <typename T>
class ObjectPool
{
private:
    MemoryPool _memoryPool;

public:
    explicit ObjectPool(size_t count)
        : _memoryPool(count, sizeof(T))
    {
    }

public:
    // 封装获取内存(构造对象)操作
    template <typename... Args>
    T *acquire(Args &&...args)
    {
        return _memoryPool.template newObject<T>(std::forward<Args>(args)...);
    }

    // 封装归还内存(销毁对象)操作
    void release(T *obj)
    {
        _memoryPool.deleteObject(obj);
    }

public:
    size_t freeCount() const
    {
        return _memoryPool.freeCount();
    }

    size_t usedCount() const
    {
        return _memoryPool.usedCount();
    }

    size_t totalBlockCount() const
    {
        return _memoryPool.totalBlockCount();
    }

    size_t peakUsed() const
    {
        return _memoryPool.peakUsed();
    }
};