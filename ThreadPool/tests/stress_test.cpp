/**
 * TPS/QPS 压力测试
 *
 * 测试场景:
 *   1. 单线程提交 - 纯 TPS 测量 (轻量/中量/重量任务)
 *   2. 多线程提交 - QPS + TPS 并发测量
 *   3. 突发流量 - 观察扩缩容行为
 *   4. 延迟分布 - P50/P95/P99 延迟统计
 */

#include "../include/thread_pool.h"

#include <iostream>
#include <iomanip>
#include <atomic>
#include <chrono>
#include <vector>
#include <algorithm>
#include <cmath>
#include <sstream>
#include <thread>

using namespace std::chrono;
using Clock = std::chrono::steady_clock;

// ── 工具函数 ──────────────────────────────────────────────

/// 格式化数值 (如 1234567 → "1,234,567")
static std::string fmt_num(double v) {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(0) << v;
    auto s = oss.str();
    int n = (int)s.size();
    for (int i = n - 3; i > 0; i -= 3)
        s.insert(i, ",");
    return s;
}

static std::string fmt_num_d(double v, int prec = 2) {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(prec) << v;
    return oss.str();
}

/// 统计延迟分布
struct LatencyStats {
    double p50, p95, p99, avg, min, max;

    static LatencyStats compute(std::vector<double>& samples) {
        std::sort(samples.begin(), samples.end());
        LatencyStats s{};
        size_t n = samples.size();
        s.min  = n ? samples.front() : 0;
        s.max  = n ? samples.back()  : 0;
        s.p50  = n ? samples[n * 50 / 100] : 0;
        s.p95  = n ? samples[n * 95 / 100] : 0;
        s.p99  = n ? samples[n * 99 / 100] : 0;
        double sum = 0;
        for (auto v : samples) sum += v;
        s.avg = n ? sum / n : 0;
        return s;
    }
};

/// 打印分隔线
static void sep(const char* title = nullptr) {
    std::cout << "\n" << std::string(72, '=') << "\n";
    if (title) std::cout << "  " << title << "\n" << std::string(72, '=') << "\n";
}

// ── 测试场景 ──────────────────────────────────────────────

/// 场景1a: 轻量级任务 TPS 测试 (单线程提交)
static void test_tps_lightweight(ThreadPool& pool, int task_count) {
    sep("场景1a: 轻量任务 TPS (atomic increment)");

    std::atomic<int64_t> counter{0};
    auto begin = Clock::now();

    for (int i = 0; i < task_count; ++i) {
        pool.submit([&] {
            counter.fetch_add(1, std::memory_order_relaxed);
        });
    }

    // 等待完成
    while (counter.load() < task_count) {
        std::this_thread::sleep_for(milliseconds(5));
    }

    auto end = Clock::now();
    double cost_ms = duration_cast<milliseconds>(end - begin).count();
    double tps = task_count * 1000.0 / cost_ms;

    std::cout << "  任务数:     " << fmt_num(task_count) << "\n";
    std::cout << "  耗时:       " << fmt_num_d(cost_ms, 1) << " ms\n";
    std::cout << "  TPS:        " << fmt_num(tps) << " tasks/s\n";
    std::cout << "  平均延迟:   " << fmt_num_d(cost_ms * 1000 / task_count, 2) << " us\n";
    pool.dump_status();
}

/// 场景1b: 中量级任务 TPS 测试 (计算密集型)
static void test_tps_medium(ThreadPool& pool, int task_count) {
    sep("场景1b: 中量任务 TPS (fibonacci / spin)");

    std::atomic<int64_t> counter{0};
    auto begin = Clock::now();

    for (int i = 0; i < task_count; ++i) {
        pool.submit([&] {
            // 模拟中等计算 (~50us on modern CPU)
            volatile double x = 1.0;
            for (int j = 0; j < 500; ++j) {
                x = x * 1.00001 + 0.00001;
            }
            (void)x;
            counter.fetch_add(1, std::memory_order_relaxed);
        });
    }

    while (counter.load() < task_count) {
        std::this_thread::sleep_for(milliseconds(5));
    }

    auto end = Clock::now();
    double cost_ms = duration_cast<milliseconds>(end - begin).count();
    double tps = task_count * 1000.0 / cost_ms;

    std::cout << "  任务数:     " << fmt_num(task_count) << "\n";
    std::cout << "  耗时:       " << fmt_num_d(cost_ms, 1) << " ms\n";
    std::cout << "  TPS:        " << fmt_num(tps) << " tasks/s\n";
    pool.dump_status();
}

/// 场景1c: 重量级任务 TPS 测试
static void test_tps_heavy(ThreadPool& pool, int task_count) {
    sep("场景1c: 重量任务 TPS (sort 1000 elements)");

    std::atomic<int64_t> counter{0};
    auto begin = Clock::now();

    for (int i = 0; i < task_count; ++i) {
        pool.submit([&] {
            std::vector<int> v(1000);
            for (int j = 0; j < 1000; ++j)
                v[j] = (j * 7919) % 10007;
            std::sort(v.begin(), v.end());
            counter.fetch_add(1, std::memory_order_relaxed);
        });
    }

    while (counter.load() < task_count) {
        std::this_thread::sleep_for(milliseconds(10));
    }

    auto end = Clock::now();
    double cost_ms = duration_cast<milliseconds>(end - begin).count();
    double tps = task_count * 1000.0 / cost_ms;

    std::cout << "  任务数:     " << fmt_num(task_count) << "\n";
    std::cout << "  耗时:       " << fmt_num_d(cost_ms, 1) << " ms\n";
    std::cout << "  TPS:        " << fmt_num(tps) << " tasks/s\n";
    pool.dump_status();
}

/// 场景2: 多客户端并发提交 (QPS + TPS)
static void test_multi_client_qps(ThreadPool& pool, int total_tasks, int num_clients) {
    std::string title2 = "场景2: 多客户端 QPS/TPS (" + std::to_string(num_clients) + " 个提交线程)";
    sep(title2.c_str());

    std::atomic<int64_t> counter{0};
    std::atomic<int64_t> submitted{0};
    std::atomic<bool>   start{false};

    // 收集延迟样本
    std::vector<double> latency_samples;
    latency_samples.reserve(total_tasks);
    std::mutex latency_mutex;

    auto submitter = [&](int client_id, int n) {
        // 等待发令枪
        while (!start.load(std::memory_order_acquire))
            std::this_thread::yield();

        for (int i = 0; i < n; ++i) {
            auto t0 = Clock::now();
            pool.submit([&, t0] {
                // 非常轻量的操作
                volatile int x = 0;
                for (int j = 0; j < 100; ++j) x += j;
                (void)x;

                auto t1 = Clock::now();
                double us = duration_cast<nanoseconds>(t1 - t0).count() / 1000.0;
                {
                    std::lock_guard<std::mutex> lk(latency_mutex);
                    latency_samples.push_back(us);
                }
                counter.fetch_add(1, std::memory_order_relaxed);
            });
            submitted.fetch_add(1, std::memory_order_relaxed);
        }
    };

    // 启动客户端线程
    std::vector<std::thread> clients;
    int per_client = total_tasks / num_clients;
    auto t_submit_begin = Clock::now();

    for (int c = 0; c < num_clients; ++c) {
        clients.emplace_back(submitter, c, per_client);
    }

    start.store(true, std::memory_order_release);

    // 等待所有提交线程完成
    for (auto& t : clients) t.join();
    auto t_submit_end = Clock::now();

    // 等待所有任务执行完成
    while (counter.load() < total_tasks) {
        std::this_thread::sleep_for(milliseconds(5));
    }
    auto t_exec_end = Clock::now();

    // 统计
    double submit_ms = duration_cast<milliseconds>(t_submit_end - t_submit_begin).count();
    double exec_ms   = duration_cast<milliseconds>(t_exec_end - t_submit_begin).count();
    double qps = total_tasks * 1000.0 / submit_ms;
    double tps = total_tasks * 1000.0 / exec_ms;

    std::cout << "  总任务数:   " << fmt_num(total_tasks) << "\n";
    std::cout << "  客户端数:   " << num_clients << "\n";
    std::cout << "  提交耗时:   " << fmt_num_d(submit_ms, 1) << " ms\n";
    std::cout << "  执行耗时:   " << fmt_num_d(exec_ms, 1) << " ms\n";
    std::cout << "  QPS (提交): " << fmt_num(qps) << " submits/s\n";
    std::cout << "  TPS (执行): " << fmt_num(tps) << " tasks/s\n";

    // 延迟分布
    auto stats = LatencyStats::compute(latency_samples);
    std::cout << "  ── 端到端延迟 ──\n";
    std::cout << "    avg:  " << fmt_num_d(stats.avg, 1) << " us\n";
    std::cout << "    min:  " << fmt_num_d(stats.min, 1) << " us\n";
    std::cout << "    max:  " << fmt_num_d(stats.max, 1) << " us\n";
    std::cout << "    P50:  " << fmt_num_d(stats.p50, 1) << " us\n";
    std::cout << "    P95:  " << fmt_num_d(stats.p95, 1) << " us\n";
    std::cout << "    P99:  " << fmt_num_d(stats.p99, 1) << " us\n";

    pool.dump_status();
}

/// 场景3: 突发流量测试 (观察扩缩容)
static void test_burst(ThreadPool& pool) {
    sep("场景3: 突发流量 (100ms 内注入 20000 个任务)");

    constexpr int BURST = 20000;
    std::atomic<int64_t> counter{0};

    std::cout << "  注入前: ";
    pool.dump_status();

    // 快速注入
    auto inject_begin = Clock::now();
    for (int i = 0; i < BURST; ++i) {
        pool.submit([&] {
            // 模拟 ~1ms 任务
            std::this_thread::sleep_for(microseconds(500 + (rand() % 1000)));
            counter.fetch_add(1, std::memory_order_relaxed);
        });
    }
    auto inject_end = Clock::now();
    double inject_ms = duration_cast<milliseconds>(inject_end - inject_begin).count();

    std::cout << "  注入耗时:   " << fmt_num_d(inject_ms, 1) << " ms\n";

    // 观察扩缩容过程
    for (int t = 0; t < 12 && counter.load() < BURST; ++t) {
        std::this_thread::sleep_for(milliseconds(100));
        std::cout << "  t=" << std::setw(4) << (t + 1) * 100 << "ms  ";
        pool.dump_status();
    }

    // 等待全部完成
    while (counter.load() < BURST) {
        std::this_thread::sleep_for(milliseconds(20));
    }

    auto end = Clock::now();
    double total_ms = duration_cast<milliseconds>(end - inject_begin).count();

    std::cout << "  最终状态:   ";
    pool.dump_status();
    std::cout << "  总耗时:     " << fmt_num_d(total_ms, 1) << " ms\n";
    std::cout << "  平均 TPS:   " << fmt_num(BURST * 1000.0 / total_ms) << " tasks/s\n";
}

/// 场景4: 延迟分布详细测试
static void test_latency_distribution(ThreadPool& pool, int task_count) {
    std::string title4 = "场景4: 延迟分布 (" + std::to_string(task_count) + " 个任务)";
    sep(title4.c_str());

    std::vector<double> samples;
    samples.reserve(task_count);
    std::mutex sample_mutex;
    std::atomic<int64_t> counter{0};

    auto begin = Clock::now();

    for (int i = 0; i < task_count; ++i) {
        auto t0 = Clock::now();
        pool.submit([&, t0] {
            auto t1 = Clock::now();
            double us = duration_cast<nanoseconds>(t1 - t0).count() / 1000.0;
            {
                std::lock_guard<std::mutex> lk(sample_mutex);
                samples.push_back(us);
            }
            counter.fetch_add(1, std::memory_order_relaxed);
        });
    }

    while (counter.load() < task_count) {
        std::this_thread::sleep_for(milliseconds(5));
    }

    auto end = Clock::now();
    double cost_ms = duration_cast<milliseconds>(end - begin).count();

    auto stats = LatencyStats::compute(samples);

    std::cout << "  任务数:     " << fmt_num(task_count) << "\n";
    std::cout << "  总耗时:     " << fmt_num_d(cost_ms, 1) << " ms\n";
    std::cout << "  TPS:        " << fmt_num(task_count * 1000.0 / cost_ms) << " tasks/s\n";
    std::cout << "  ── 提交→执行延迟 ──\n";
    std::cout << "    avg:  " << std::setw(10) << fmt_num_d(stats.avg, 1) << " us\n";
    std::cout << "    min:  " << std::setw(10) << fmt_num_d(stats.min, 1) << " us\n";
    std::cout << "    max:  " << std::setw(10) << fmt_num_d(stats.max, 1) << " us\n";
    std::cout << "    P50:  " << std::setw(10) << fmt_num_d(stats.p50, 1) << " us\n";
    std::cout << "    P95:  " << std::setw(10) << fmt_num_d(stats.p95, 1) << " us\n";
    std::cout << "    P99:  " << std::setw(10) << fmt_num_d(stats.p99, 1) << " us\n";
}

/// 场景5: 持续负载下的稳态 TPS/QPS
static void test_steady_state(ThreadPool& pool) {
    sep("场景5: 稳态吞吐 (8 客户端持续提交 30 秒)");

    std::atomic<int64_t> counter{0};
    std::atomic<int64_t> submitted{0};
    std::atomic<bool>   stop{false};
    std::atomic<bool>   start{false};

    auto client = [&] {
        while (!start.load(std::memory_order_acquire))
            std::this_thread::yield();
        while (!stop.load(std::memory_order_acquire)) {
            pool.submit([&] {
                // 极轻量操作
                volatile int x = 0;
                for (int i = 0; i < 50; ++i) x += i;
                (void)x;
                counter.fetch_add(1, std::memory_order_relaxed);
            });
            submitted.fetch_add(1, std::memory_order_relaxed);
        }
    };

    // 启动 8 个客户端
    std::vector<std::thread> clients;
    for (int i = 0; i < 8; ++i)
        clients.emplace_back(client);

    auto t0 = Clock::now();
    start.store(true, std::memory_order_release);

    // 每 5 秒采样一次
    int64_t prev_done = 0;
    for (int sec = 5; sec <= 25; sec += 5) {
        std::this_thread::sleep_for(seconds(5));
        int64_t now_done = counter.load();
        int64_t now_sub  = submitted.load();
        int64_t delta_done = now_done - prev_done;
        prev_done = now_done;
        std::cout << "  [" << std::setw(2) << sec << "s] "
                  << "瞬时TPS=" << fmt_num(delta_done / 5.0)
                  << " 累计提交=" << fmt_num(now_sub)
                  << " 累计完成=" << fmt_num(now_done);
        pool.dump_status();
    }

    stop.store(true, std::memory_order_release);
    for (auto& t : clients) t.join();

    auto t1 = Clock::now();
    double elapsed_s = duration_cast<milliseconds>(t1 - t0).count() / 1000.0;

    std::cout << "\n  运行时间:   " << fmt_num_d(elapsed_s, 1) << " s\n";
    std::cout << "  总提交:     " << fmt_num(submitted.load()) << "\n";
    std::cout << "  总完成:     " << fmt_num(counter.load()) << "\n";
    std::cout << "  平均 QPS:   " << fmt_num(submitted.load() / elapsed_s) << " submits/s\n";
    std::cout << "  平均 TPS:   " << fmt_num(counter.load() / elapsed_s) << " tasks/s\n";
}

// ─────────────────────────────────────────────────────────
int main(int argc, char* argv[]) {
    (void)argc; (void)argv;

    std::cout << "╔══════════════════════════════════════════════════════════════════╗\n";
    std::cout << "║         ThreadPool TPS/QPS 压力测试                              ║\n";
    std::cout << "╚══════════════════════════════════════════════════════════════════╝\n";
    std::cout << "  硬件并发: " << std::thread::hardware_concurrency() << " 核心\n";

    // ── 基础 TPS 测试 (轻/中/重) ──
    {
        ThreadPool pool(4, 16);
        test_tps_lightweight(pool, 500000);
    }
    {
        ThreadPool pool(4, 16);
        test_tps_medium(pool, 50000);
    }
    {
        ThreadPool pool(4, 16);
        test_tps_heavy(pool, 5000);
    }

    // ── 多客户端 QPS ──
    {
        ThreadPool pool(4, 16);
        test_multi_client_qps(pool, 200000, 4);
    }
    {
        ThreadPool pool(4, 16);
        test_multi_client_qps(pool, 200000, 8);
    }

    // ── 延迟分布 ──
    {
        ThreadPool pool(4, 16);
        test_latency_distribution(pool, 100000);
    }

    // ── 突发流量 ──
    {
        ThreadPool pool(4, 16);
        test_burst(pool);
    }

    // ── 稳态测试 ──
    {
        ThreadPool pool(4, 16);
        test_steady_state(pool);
    }

    sep("全部测试完成");
    return 0;
}
