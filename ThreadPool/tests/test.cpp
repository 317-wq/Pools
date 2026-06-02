#include "../include/thread_pool.h"
#include <iostream>

int main()
{
    ThreadPool pool(4);
    auto f = pool.submit([](int a, int b){
        return a * b;
    }, 10, 20);
    std::cout << f.get() << std::endl;
    // std::vector<std::future<int>> futures;

    // for (int i = 0; i < 10; i++)
    // {
    //     futures.emplace_back(
    //         pool.submit(
    //             [i]()
    //             {
    //                 std::this_thread::sleep_for(
    //                     std::chrono::milliseconds(100));

    //                 return i * i;
    //             }));
    // }

    // for (auto& future : futures)
    // {
    //     std::cout
    //         << future.get()
    //         << " ";
    // }

    std::cout << std::endl;
}