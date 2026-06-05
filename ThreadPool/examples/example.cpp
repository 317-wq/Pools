/**
 * 场景示例：模拟 Web 服务器并发请求处理
 *
 * 演示特性：
 *   1. 多种任务类型混投（轻量/中量/重量）
 *   2. 自适应扩缩容（观察线程数变化）
 *   3. 池内线程互相提交任务（递归并行）
 *   4. future 异步获取结果
 */

#include "../include/thread_pool.h"
#include <iostream>
#include <iomanip>
#include <chrono>
#include <random>
#include <vector>
#include <future>

using namespace std::chrono;

// 模拟数据库查询（重量操作）
int queryDatabase(int userId) {
    std::this_thread::sleep_for(milliseconds(20 + (userId % 10))); // 20~29ms
    return userId * 100; // 模拟返回结果
}

// 模拟缓存读取（轻量操作）
int readCache(int key) {
    std::this_thread::sleep_for(milliseconds(1));
    return key * 10;
}

// 模拟日志写入（中量操作）
void writeLog(const std::string& msg) {
    std::this_thread::sleep_for(milliseconds(3));
    static std::mutex logMutex;
    std::lock_guard<std::mutex> lock(logMutex);
    std::cout << "  [LOG] " << msg << std::endl;
}

int main() {
    // 最少 4 个线程，最多 16 个
    ThreadPool pool(4, 16);

    std::cout << "╔══════════════════════════════════════════════════════════╗\n";
    std::cout << "║     ThreadPool 场景示例 — Web 服务器请求处理              ║\n";
    std::cout << "╚══════════════════════════════════════════════════════════╝\n\n";

    std::cout << "初始状态: ";
    pool.dump_status();

    constexpr int REQUESTS = 50;
    std::vector<std::future<int>> results;

    // 模拟 50 个并发请求到达
    auto t0 = std::chrono::steady_clock::now();

    for (int i = 0; i < REQUESTS; ++i) {
        int userId = i % 10;

        auto future = pool.submit([&pool, userId]() -> int {
            // 阶段 1: 检查缓存
            int cached = readCache(userId);

            // 阶段 2: 缓存未命中，查数据库
            if (cached < 50) {
                writeLog("Cache miss for user " + std::to_string(userId) + " → query DB");

                // 池内提交子任务 — 利用 work-stealing 并行
                auto dbFuture = pool.submit([userId]() -> int {
                    return queryDatabase(userId);
                });

                int dbResult = dbFuture.get(); // 阻塞等待子任务完成
                return dbResult + cached;
            }

            writeLog("Cache hit for user " + std::to_string(userId));
            return cached;
        });

        results.push_back(std::move(future));
    }

    // 等待所有请求完成，收集结果
    int totalResult = 0;
    for (auto& f : results) {
        totalResult += f.get();
    }

    auto t1 = std::chrono::steady_clock::now();
    double elapsed = duration_cast<milliseconds>(t1 - t0).count();

    std::cout << "\n══════════════════════════════════════════════════════════\n";
    std::cout << "  处理请求数:   " << REQUESTS << "\n";
    std::cout << "  总耗时:       " << elapsed << " ms\n";
    std::cout << "  平均每请求:   " << (elapsed / REQUESTS) << " ms\n";
    std::cout << "  结果累积值:   " << totalResult << "\n";
    std::cout << "  最终状态:     ";
    pool.dump_status();
    std::cout << "══════════════════════════════════════════════════════════\n";

    return 0;
}
