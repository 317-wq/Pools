#include "../include/memory_pool.h"
#include <iostream>

struct Test
{
    int value;

    Test(int v)
        : value(v)
    {
        std::cout << "ctor\n";
    }

    ~Test()
    {
        std::cout << "dtor\n";
    }
};

int main()
{
    MemoryPool pool(2, sizeof(int));

    auto* p1 = pool.newObject<int>(1);
    auto* p2 = pool.newObject<int>(2);

    (void)p1;
    (void)p2;
}