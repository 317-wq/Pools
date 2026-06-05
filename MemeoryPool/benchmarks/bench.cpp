#include "../include/object_pool.h"
#include <iostream>
#include <iomanip>
#include <chrono>
#include <vector>
#include <thread>
#include <atomic>
#include <cstdlib>
#include <algorithm>
#include <random>
#include <string>
#include <memory>

// ============================================================
// Benchmark 工具
// ============================================================
using Clock = std::chrono::high_resolution_clock;
using ms = std::chrono::milliseconds;
using us = std::chrono::microseconds;

struct BenchResult {
    std::string name;
    double elapsedMs;
    size_t ops;
    double opsPerMs;
};

// 用于测试的小/中/大对象
struct SmallObj  { int64_t a; int64_t b; };                     // 16 bytes
struct MediumObj { int64_t data[8]; };                          // 64 bytes
struct LargeObj  { int64_t data[64]; };                         // 512 bytes
struct HugeObj   { int64_t data[256]; };                        // 2048 bytes

// ============================================================
// 场景1：单线程 — 顺序分配后顺序释放
// ============================================================
template<typename Obj>
BenchResult bench_new_delete_sequential(size_t iterations) {
    auto start = Clock::now();
    {
        std::vector<Obj*> ptrs;
        ptrs.reserve(iterations);
        for (size_t i = 0; i < iterations; ++i) {
            ptrs.push_back(new Obj());
        }
        for (auto* p : ptrs) {
            delete p;
        }
    }
    auto end = Clock::now();
    double t = std::chrono::duration<double, std::milli>(end - start).count();
    return {"new/delete 顺序", t, iterations, iterations / t};
}

template<typename Obj>
BenchResult bench_pool_sequential(size_t iterations) {
    auto start = Clock::now();
    {
        MemoryPool pool(iterations, sizeof(Obj));
        std::vector<Obj*> ptrs;
        ptrs.reserve(iterations);
        for (size_t i = 0; i < iterations; ++i) {
            ptrs.push_back(pool.newObject<Obj>());
        }
        for (auto* p : ptrs) {
            pool.deleteObject(p);
        }
    }
    auto end = Clock::now();
    double t = std::chrono::duration<double, std::milli>(end - start).count();
    return {"MemoryPool 顺序", t, iterations, iterations / t};
}

template<typename Obj>
BenchResult bench_malloc_sequential(size_t iterations) {
    auto start = Clock::now();
    {
        std::vector<void*> ptrs;
        ptrs.reserve(iterations);
        for (size_t i = 0; i < iterations; ++i) {
            ptrs.push_back(std::malloc(sizeof(Obj)));
        }
        for (auto* p : ptrs) {
            std::free(p);
        }
    }
    auto end = Clock::now();
    double t = std::chrono::duration<double, std::milli>(end - start).count();
    return {"malloc/free 顺序", t, iterations, iterations / t};
}

// ============================================================
// 场景2：单线程 — 随机分配/释放交错（模拟真实场景）
// ============================================================
template<typename Obj>
BenchResult bench_new_delete_random(size_t poolSize, size_t totalOps) {
    std::vector<Obj*> ptrs;
    ptrs.reserve(poolSize);
    std::mt19937 rng(42);

    auto start = Clock::now();
    for (size_t i = 0; i < totalOps; ++i) {
        if (ptrs.size() >= poolSize || (rng() % 3 == 0 && !ptrs.empty())) {
            size_t idx = rng() % ptrs.size();
            delete ptrs[idx];
            ptrs[idx] = ptrs.back();
            ptrs.pop_back();
        } else {
            ptrs.push_back(new Obj());
        }
    }
    // cleanup
    for (auto* p : ptrs) delete p;
    auto end = Clock::now();
    double t = std::chrono::duration<double, std::milli>(end - start).count();
    return {"new/delete 随机", t, totalOps, totalOps / t};
}

template<typename Obj>
BenchResult bench_pool_random(size_t poolSize, size_t totalOps) {
    MemoryPool pool(poolSize, sizeof(Obj));
    std::vector<Obj*> ptrs;
    ptrs.reserve(poolSize);
    std::mt19937 rng(42);

    auto start = Clock::now();
    for (size_t i = 0; i < totalOps; ++i) {
        if (ptrs.size() >= poolSize || (rng() % 3 == 0 && !ptrs.empty())) {
            size_t idx = rng() % ptrs.size();
            pool.deleteObject(ptrs[idx]);
            ptrs[idx] = ptrs.back();
            ptrs.pop_back();
        } else {
            ptrs.push_back(pool.newObject<Obj>());
        }
    }
    // cleanup
    for (auto* p : ptrs) pool.deleteObject(p);
    auto end = Clock::now();
    double t = std::chrono::duration<double, std::milli>(end - start).count();
    return {"MemoryPool 随机", t, totalOps, totalOps / t};
}

template<typename Obj>
BenchResult bench_malloc_random(size_t poolSize, size_t totalOps) {
    std::vector<void*> ptrs;
    ptrs.reserve(poolSize);
    std::mt19937 rng(42);

    auto start = Clock::now();
    for (size_t i = 0; i < totalOps; ++i) {
        if (ptrs.size() >= poolSize || (rng() % 3 == 0 && !ptrs.empty())) {
            size_t idx = rng() % ptrs.size();
            std::free(ptrs[idx]);
            ptrs[idx] = ptrs.back();
            ptrs.pop_back();
        } else {
            ptrs.push_back(std::malloc(sizeof(Obj)));
        }
    }
    // cleanup
    for (auto* p : ptrs) std::free(p);
    auto end = Clock::now();
    double t = std::chrono::duration<double, std::milli>(end - start).count();
    return {"malloc/free 随机", t, totalOps, totalOps / t};
}

// ============================================================
// 场景3：多线程 — 并发分配/释放
// ============================================================
template<typename Obj>
BenchResult bench_new_delete_multithread(size_t opsPerThread, int numThreads) {
    std::atomic<bool> startFlag{false};

    auto worker = [&](int tid) {
        std::vector<Obj*> ptrs;
        ptrs.reserve(256);
        std::mt19937 rng(tid + 42);
        while (!startFlag.load(std::memory_order_acquire)) {}
        for (size_t i = 0; i < opsPerThread; ++i) {
            if (ptrs.size() >= 256 || (rng() % 3 == 0 && !ptrs.empty())) {
                size_t idx = rng() % ptrs.size();
                delete ptrs[idx];
                ptrs[idx] = ptrs.back();
                ptrs.pop_back();
            } else {
                ptrs.push_back(new Obj());
            }
        }
        for (auto* p : ptrs) delete p;
    };

    std::vector<std::thread> threads;
    for (int i = 0; i < numThreads; ++i) threads.emplace_back(worker, i);

    auto start = Clock::now();
    startFlag.store(true, std::memory_order_release);
    for (auto& t : threads) t.join();
    auto end = Clock::now();

    double t = std::chrono::duration<double, std::milli>(end - start).count();
    size_t total = opsPerThread * numThreads;
    return {"new/delete 多线程", t, total, total / t};
}

template<typename Obj>
BenchResult bench_pool_multithread(size_t opsPerThread, int numThreads) {
    MemoryPool pool(4096, sizeof(Obj));
    std::atomic<bool> startFlag{false};

    auto worker = [&](int tid) {
        std::vector<Obj*> ptrs;
        ptrs.reserve(256);
        std::mt19937 rng(tid + 42);
        while (!startFlag.load(std::memory_order_acquire)) {}
        for (size_t i = 0; i < opsPerThread; ++i) {
            if (ptrs.size() >= 256 || (rng() % 3 == 0 && !ptrs.empty())) {
                size_t idx = rng() % ptrs.size();
                pool.deleteObject(ptrs[idx]);
                ptrs[idx] = ptrs.back();
                ptrs.pop_back();
            } else {
                ptrs.push_back(pool.newObject<Obj>());
            }
        }
        for (auto* p : ptrs) pool.deleteObject(p);
    };

    std::vector<std::thread> threads;
    for (int i = 0; i < numThreads; ++i) threads.emplace_back(worker, i);

    auto start = Clock::now();
    startFlag.store(true, std::memory_order_release);
    for (auto& t : threads) t.join();
    auto end = Clock::now();

    double t = std::chrono::duration<double, std::milli>(end - start).count();
    size_t total = opsPerThread * numThreads;
    return {"MemoryPool 多线程", t, total, total / t};
}

template<typename Obj>
BenchResult bench_malloc_multithread(size_t opsPerThread, int numThreads) {
    std::atomic<bool> startFlag{false};

    auto worker = [&](int tid) {
        std::vector<void*> ptrs;
        ptrs.reserve(256);
        std::mt19937 rng(tid + 42);
        while (!startFlag.load(std::memory_order_acquire)) {}
        for (size_t i = 0; i < opsPerThread; ++i) {
            if (ptrs.size() >= 256 || (rng() % 3 == 0 && !ptrs.empty())) {
                size_t idx = rng() % ptrs.size();
                std::free(ptrs[idx]);
                ptrs[idx] = ptrs.back();
                ptrs.pop_back();
            } else {
                ptrs.push_back(std::malloc(sizeof(Obj)));
            }
        }
        for (auto* p : ptrs) std::free(p);
    };

    std::vector<std::thread> threads;
    for (int i = 0; i < numThreads; ++i) threads.emplace_back(worker, i);

    auto start = Clock::now();
    startFlag.store(true, std::memory_order_release);
    for (auto& t : threads) t.join();
    auto end = Clock::now();

    double t = std::chrono::duration<double, std::milli>(end - start).count();
    size_t total = opsPerThread * numThreads;
    return {"malloc/free 多线程", t, total, total / t};
}

// ============================================================
// 打印表格
// ============================================================
void printSection(const std::string& title) {
    std::cout << std::endl;
    std::cout << "╔══════════════════════════════════════════════════════════════╗" << std::endl;
    std::cout << "║  " << std::left << std::setw(58) << title << "║" << std::endl;
    std::cout << "╚══════════════════════════════════════════════════════════════╝" << std::endl;
}

void printRow(const BenchResult& r, double baselineMs) {
    double speedup = baselineMs / r.elapsedMs;
    std::cout << "  " << std::left << std::setw(28) << r.name
              << std::right << std::setw(8) << std::fixed << std::setprecision(2) << r.elapsedMs << " ms"
              << std::right << std::setw(12) << std::fixed << std::setprecision(1) << r.opsPerMs << " ops/ms"
              << std::right << std::setw(8) << std::fixed << std::setprecision(1) << speedup << "x"
              << std::endl;
}

// ============================================================
// main
// ============================================================
int main() {
    std::cout << "╔══════════════════════════════════════════════════════════════╗" << std::endl;
    std::cout << "║         MemoryPool Benchmark — 性能对比测试                    ║" << std::endl;
    std::cout << "╚══════════════════════════════════════════════════════════════╝" << std::endl;

    constexpr size_t SEQ_ITERS = 1'000'000;
    constexpr size_t RAND_POOL = 4096;
    constexpr size_t RAND_OPS  = 500'000;
    constexpr int    MT_THREADS = 8;
    constexpr size_t MT_OPS_PER = 100'000;

    // ========================================
    // 场景1：单线程顺序
    // ========================================
    {
        printSection("场景1: 单线程顺序分配→顺序释放 (SmallObj 16B, N=" + std::to_string(SEQ_ITERS) + ")");

        auto poolR = bench_pool_sequential<SmallObj>(SEQ_ITERS);
        auto newR  = bench_new_delete_sequential<SmallObj>(SEQ_ITERS);
        auto malR  = bench_malloc_sequential<SmallObj>(SEQ_ITERS);

        double baseline = newR.elapsedMs;
        std::cout << "  " << std::left << std::setw(28) << "方案"
                  << std::right << std::setw(10) << "耗时"
                  << std::right << std::setw(14) << "吞吐量"
                  << std::right << std::setw(10) << "加速比" << std::endl;
        std::cout << "  " << std::string(62, '-') << std::endl;
        printRow(newR,  baseline);
        printRow(malR,  baseline);
        printRow(poolR, baseline);
    }

    // ========================================
    // 场景2：不同对象大小
    // ========================================
    {
        printSection("场景2: 不同对象大小对比 (单线程顺序, N=" + std::to_string(SEQ_ITERS / 10) + ")");

        constexpr size_t N = SEQ_ITERS / 10;

        std::cout << "  " << std::left << std::setw(28) << "对象大小"
                  << std::right << std::setw(12) << "MemoryPool"
                  << std::right << std::setw(12) << "new/delete"
                  << std::right << std::setw(10) << "加速比" << std::endl;
        std::cout << "  " << std::string(62, '-') << std::endl;

        {
            auto p = bench_pool_sequential<SmallObj>(N);
            auto n = bench_new_delete_sequential<SmallObj>(N);
            double speedup = n.elapsedMs / p.elapsedMs;
            std::cout << "  " << std::left << std::setw(28) << "SmallObj (16B)"
                      << std::right << std::setw(10) << std::fixed << std::setprecision(2) << p.elapsedMs << " ms"
                      << std::right << std::setw(10) << std::fixed << std::setprecision(2) << n.elapsedMs << " ms"
                      << std::right << std::setw(8) << std::fixed << std::setprecision(1) << speedup << "x"
                      << std::endl;
        }
        {
            auto p = bench_pool_sequential<MediumObj>(N);
            auto n = bench_new_delete_sequential<MediumObj>(N);
            double speedup = n.elapsedMs / p.elapsedMs;
            std::cout << "  " << std::left << std::setw(28) << "MediumObj (64B)"
                      << std::right << std::setw(10) << std::fixed << std::setprecision(2) << p.elapsedMs << " ms"
                      << std::right << std::setw(10) << std::fixed << std::setprecision(2) << n.elapsedMs << " ms"
                      << std::right << std::setw(8) << std::fixed << std::setprecision(1) << speedup << "x"
                      << std::endl;
        }
        {
            auto p = bench_pool_sequential<LargeObj>(N);
            auto n = bench_new_delete_sequential<LargeObj>(N);
            double speedup = n.elapsedMs / p.elapsedMs;
            std::cout << "  " << std::left << std::setw(28) << "LargeObj (512B)"
                      << std::right << std::setw(10) << std::fixed << std::setprecision(2) << p.elapsedMs << " ms"
                      << std::right << std::setw(10) << std::fixed << std::setprecision(2) << n.elapsedMs << " ms"
                      << std::right << std::setw(8) << std::fixed << std::setprecision(1) << speedup << "x"
                      << std::endl;
        }
        {
            auto p = bench_pool_sequential<HugeObj>(N);
            auto n = bench_new_delete_sequential<HugeObj>(N);
            double speedup = n.elapsedMs / p.elapsedMs;
            std::cout << "  " << std::left << std::setw(28) << "HugeObj (2048B)"
                      << std::right << std::setw(10) << std::fixed << std::setprecision(2) << p.elapsedMs << " ms"
                      << std::right << std::setw(10) << std::fixed << std::setprecision(2) << n.elapsedMs << " ms"
                      << std::right << std::setw(8) << std::fixed << std::setprecision(1) << speedup << "x"
                      << std::endl;
        }
    }

    // ========================================
    // 场景3：随机模式（模拟真实负载）
    // ========================================
    {
        printSection("场景3: 随机分配/释放交错 (SmallObj 16B, poolSize="
                     + std::to_string(RAND_POOL) + ", ops=" + std::to_string(RAND_OPS) + ")");

        auto poolR = bench_pool_random<SmallObj>(RAND_POOL, RAND_OPS);
        auto newR  = bench_new_delete_random<SmallObj>(RAND_POOL, RAND_OPS);
        auto malR  = bench_malloc_random<SmallObj>(RAND_POOL, RAND_OPS);

        double baseline = newR.elapsedMs;
        std::cout << "  " << std::left << std::setw(28) << "方案"
                  << std::right << std::setw(10) << "耗时"
                  << std::right << std::setw(14) << "吞吐量"
                  << std::right << std::setw(10) << "加速比" << std::endl;
        std::cout << "  " << std::string(62, '-') << std::endl;
        printRow(newR,  baseline);
        printRow(malR,  baseline);
        printRow(poolR, baseline);
    }

    // ========================================
    // 场景4：多线程并发
    // ========================================
    {
        printSection("场景4: 多线程并发 (SmallObj 16B, " + std::to_string(MT_THREADS)
                     + "线程 × " + std::to_string(MT_OPS_PER) + " ops)");

        auto poolR = bench_pool_multithread<SmallObj>(MT_OPS_PER, MT_THREADS);
        auto newR  = bench_new_delete_multithread<SmallObj>(MT_OPS_PER, MT_THREADS);
        auto malR  = bench_malloc_multithread<SmallObj>(MT_OPS_PER, MT_THREADS);

        double baseline = newR.elapsedMs;
        std::cout << "  " << std::left << std::setw(28) << "方案"
                  << std::right << std::setw(10) << "耗时"
                  << std::right << std::setw(14) << "吞吐量"
                  << std::right << std::setw(10) << "加速比" << std::endl;
        std::cout << "  " << std::string(62, '-') << std::endl;
        printRow(newR,  baseline);
        printRow(malR,  baseline);
        printRow(poolR, baseline);
    }

    // ========================================
    // 场景5：Cache Locality 测试
    // ========================================
    {
        printSection("场景5: 内存局部性 — 遍历写入 (SmallObj 16B, N=100000, 100轮)");

        constexpr size_t N = 100'000;
        constexpr int ROUNDS = 100;

        // MemoryPool: 所有对象在连续内存中
        {
            MemoryPool pool(N, sizeof(SmallObj));
            std::vector<SmallObj*> ptrs(N);
            for (size_t i = 0; i < N; ++i) ptrs[i] = pool.newObject<SmallObj>();

            auto start = Clock::now();
            for (int r = 0; r < ROUNDS; ++r) {
                for (size_t i = 0; i < N; ++i) {
                    ptrs[i]->a = r;
                    ptrs[i]->b = r + 1;
                }
            }
            auto end = Clock::now();
            double t = std::chrono::duration<double, std::milli>(end - start).count();

            for (auto* p : ptrs) pool.deleteObject(p);
            std::cout << "  " << std::left << std::setw(28) << "MemoryPool (连续内存)"
                      << std::right << std::setw(10) << std::fixed << std::setprecision(2) << t << " ms"
                      << std::endl;
        }

        // new/delete: 对象散落在堆各处
        {
            std::vector<SmallObj*> ptrs(N);
            for (size_t i = 0; i < N; ++i) ptrs[i] = new SmallObj();

            auto start = Clock::now();
            for (int r = 0; r < ROUNDS; ++r) {
                for (size_t i = 0; i < N; ++i) {
                    ptrs[i]->a = r;
                    ptrs[i]->b = r + 1;
                }
            }
            auto end = Clock::now();
            double t = std::chrono::duration<double, std::milli>(end - start).count();

            for (auto* p : ptrs) delete p;
            std::cout << "  " << std::left << std::setw(28) << "new/delete (分散内存)"
                      << std::right << std::setw(10) << std::fixed << std::setprecision(2) << t << " ms"
                      << std::endl;
        }
    }

    std::cout << std::endl;
    std::cout << "══════════════════════════════════════════════════════════════" << std::endl;
    std::cout << "  Benchmark 完成。加速比 = baseline / MemoryPool 耗时" << std::endl;
    std::cout << "  数值越大，MemoryPool 性能优势越明显。" << std::endl;
    std::cout << "══════════════════════════════════════════════════════════════" << std::endl;

    return 0;
}
