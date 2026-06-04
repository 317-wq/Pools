#include <iostream>
#include "../include/memory_pool.h"

struct BigObject
{
    char data[1024];
};

int main()
{
    MemoryPool pool(10, 64);

    try
    {
        auto* obj =
            pool.newObject<BigObject>();
    }
    catch(const std::exception& e)
    {
        std::cout
            << e.what()
            << std::endl;
    }
}