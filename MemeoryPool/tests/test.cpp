#include "../include/memory_pool.h"

#include <iostream>

int main()
{
    MemoryPool pool(5, 30);

    std::cout
        << "block size : "
        << pool.blockSize()
        << '\n';

    std::cout
        << "total block : "
        << pool.blockCount()
        << '\n';

    std::cout
        << "free block : "
        << pool.freeCount()
        << '\n';

    std::cout << '\n';

    void* p1 = pool.allocate();
    void* p2 = pool.allocate();
    void* p3 = pool.allocate();

    std::cout
        << "allocate 3 blocks\n";

    std::cout
        << "used : "
        << pool.usedCount()
        << '\n';

    std::cout
        << "free : "
        << pool.freeCount()
        << '\n';

    std::cout << '\n';

    pool.deallocate(p2);

    std::cout
        << "return p2\n";

    std::cout
        << "used : "
        << pool.usedCount()
        << '\n';

    std::cout
        << "free : "
        << pool.freeCount()
        << '\n';

    std::cout << '\n';

    void* p4 = pool.allocate();

    std::cout
        << "p2 = "
        << p2
        << '\n';

    std::cout
        << "p4 = "
        << p4
        << '\n';

    if (p2 == p4)
    {
        std::cout
            << "memory reused success\n";
    }

    return 0;
}