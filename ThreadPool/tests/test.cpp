#include "../include/thread_pool.h"

#include <chrono>
#include <thread>

int main()
{
    ThreadPool pool(4);

    for(int i = 0; i < 20; ++i)
    {
        pool.submit(
            []{
                std::this_thread::sleep_for(
                    std::chrono::seconds(2));
            });
    }

    for(int i = 0; i < 6; ++i)
    {
        pool.dump_status();

        std::this_thread::sleep_for(
            std::chrono::seconds(1));
    }

    return 0;
}