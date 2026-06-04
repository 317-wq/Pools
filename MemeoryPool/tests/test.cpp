#include <iostream>
#include "../include/memory_pool.h"

struct Test
{
    int x;

    Test(int v)
        : x(v)
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
    MemoryPool pool(2, sizeof(Test));

    auto *obj1 = pool.newObject<Test>(100);

    pool.deleteObject(obj1);

    try
    {
        int *p = new int(10);

        pool.deleteObject(p);
    }
    catch (const std::exception &e)
    {
        std::cout
            << "catch exception: "
            << e.what()
            << std::endl;
    }

    return 0;
}