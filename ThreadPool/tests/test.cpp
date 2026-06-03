#include "../include/thread_pool.h"

#include <iostream>
#include <atomic>
#include <chrono>

int main()
{
    ThreadPool pool(4, 16);

    constexpr int TASK_COUNT = 1000000;

    std::atomic<int> counter = 0;

    auto begin =
        std::chrono::steady_clock::now();

    for(int i=0;i<TASK_COUNT;i++)
    {
        pool.submit([&]{
            counter.fetch_add(
                1,
                std::memory_order_relaxed
            );
        });
    }

    while(counter.load() != TASK_COUNT)
    {
        std::this_thread::sleep_for(
            std::chrono::milliseconds(10)
        );
    }

    auto end =
        std::chrono::steady_clock::now();

    auto cost =
        std::chrono::duration_cast<
            std::chrono::milliseconds
        >(end-begin).count();

    double tps =
        TASK_COUNT * 1000.0 / cost;

    std::cout
        << "tasks=" << TASK_COUNT
        << std::endl;

    std::cout
        << "cost=" << cost << " ms"
        << std::endl;

    std::cout
        << "TPS=" << tps
        << std::endl;
}