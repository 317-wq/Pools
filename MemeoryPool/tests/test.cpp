#include <iostream>
#include <thread>
#include <vector>

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
    MemoryPool pool(1000, sizeof(Test));

    std::vector<std::thread> threads;

    for(int i = 0; i < 8; ++i)
    {
        threads.emplace_back([&pool]()
        {
            for(int j = 0; j < 10000; ++j)
            {
                auto* obj =
                    pool.newObject<Test>(j);

                if(obj)
                {
                    pool.deleteObject(obj);
                }
            }
        });
    }

    for(auto& t : threads)
    {
        t.join();
    }

    std::cout
        << "free = "
        << pool.freeCount()
        << std::endl;

    std::cout
        << "used = "
        << pool.usedCount()
        << std::endl;
}