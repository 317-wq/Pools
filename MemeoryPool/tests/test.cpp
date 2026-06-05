#include "../include/object_pool.h"
#include <iostream>

int main()
{
    ObjectPool<int> pool(2);

    auto* p1 = pool.acquire(1);

    auto* p2 = pool.acquire(2);

    auto* p3 = pool.acquire(3);

    pool.release(p1);

    pool.shrink();

    std::cout
        << pool.totalBlockCount()
        << std::endl;
}