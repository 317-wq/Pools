#include <iostream>
#include "../include/memory_pool.h"

struct Test
{
    int value;

    Test(int v)
        : value(v)
    {
    }
};

int main()
{
    MemoryPool pool(2, sizeof(Test));

    auto *p1 = pool.newObject<Test>(1);
    auto *p2 = pool.newObject<Test>(2);

    std::cout << pool.usedCount() << '\n';

    auto *p3 = pool.newObject<Test>(3);

    std::cout << pool.usedCount() << '\n';

    pool.deleteObject(p1);
    pool.deleteObject(p2);
    pool.deleteObject(p3);

    std::cout << pool.freeCount() << '\n';
    std::cout << pool.totalBlockCount() << '\n';
}