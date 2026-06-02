#include "../include/thread_pool.h"

#include <iostream>
#include <chrono>

int main()
{
    ThreadPool pool(4);
    std::mutex cout_mutex;
    for (int i = 0; i < 10; ++i)
    {
        pool.submit(
            [i, &cout_mutex]()
            {
                {
                    std::lock_guard<std::mutex> lock(cout_mutex);

                    std::cout
                        << "task "
                        << i
                        << " running in thread "
                        << std::this_thread::get_id()
                        << '\n';
                }

                std::this_thread::sleep_for(
                    std::chrono::milliseconds(500));
            });
    }

    return 0;
}