// // // #include "../include/thread_pool.h"

// // // #include <iostream>
// // // #include <chrono>
// // // #include <thread>

// // // int main()
// // // {
// // //     ThreadPool pool(2, 8);

// // //     std::cout << "========== Start ==========\n";

// // //     // 状态监控线程
// // //     std::thread monitor([&pool]
// // //                         {
// // //         for(int i = 0; i < 30; ++i)
// // //         {
// // //             pool.dump_status();

// // //             std::this_thread::sleep_for(
// // //                 std::chrono::seconds(1));
// // //         } });

// // //     std::mutex mtx;
// // //     // 提交100个任务
// // //     for (int i = 0; i < 100; ++i)
// // //     {
// // //         pool.submit([i, &mtx]
// // //                     {
// // //             {
// // //                 std::unique_lock<std::mutex> lock(mtx);
// // //                             std::cout
// // //                 << "[Task "
// // //                 << i
// // //                 << "] start, thread="
// // //                 << std::this_thread::get_id()
// // //                 << std::endl;
// // //             }

// // //             std::this_thread::sleep_for(
// // //                 std::chrono::seconds(2));

// // //             {
// // //                                 std::unique_lock<std::mutex> lock(mtx);

// // //             std::cout
// // //                 << "[Task "
// // //                 << i
// // //                 << "] finish"
// // //                 << std::endl;
// // //             } });
// // //     }

// // //     monitor.join();

// // //     std::cout
// // //         << "\n========== Test Finish ==========\n";

// // //     return 0;
// // // }

// // #include "../include/thread_pool.h"

// // #include <iostream>

// // int main()
// // {
// //     ThreadPool pool(2, 4);

// //     pool.submit([]
// //                 { throw std::runtime_error(
// //                       "task exception test"); });

// //     pool.submit([]
// //                 { std::cout
// //                       << "normal task"
// //                       << std::endl; });

// //     std::this_thread::sleep_for(
// //         std::chrono::seconds(3));

// //     pool.dump_status();

// //     return 0;
// // }

// #include "../include/thread_pool.h"

// #include <iostream>

// int main()
// {
//     ThreadPool pool(2, 4);

//     auto future1 =
//         pool.submit([]
//                     { return 100; });

//     auto future2 =
//         pool.submit([]
//                     { return std::string("hello"); });

//     std::cout
//         << future1.get()
//         << std::endl;

//     std::cout
//         << future2.get()
//         << std::endl;

//     return 0;
// }

#include "../include/thread_pool.h"

#include <iostream>

int main()
{
    ThreadPool pool(2, 8);

    for(int i = 0; i < 100; ++i)
    {
        pool.submit([]{
            std::this_thread::sleep_for(
                std::chrono::milliseconds(100));
        });
    }

    while(true)
    {
        pool.dump_status();

        std::this_thread::sleep_for(
            std::chrono::seconds(1));
    }
}