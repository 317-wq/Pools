#include "../include/object_pool.h"
#include <iostream>
#include <thread>
#include <vector>
#include <atomic>
#include <cassert>
#include <chrono>
#include <set>

// 简单的测试框架宏
static int g_passed = 0;
static int g_failed = 0;

#define TEST(name) \
    do { \
        std::cout << "[TEST] " << name << " ... "; \
        std::cout.flush(); \
    } while(0)

#define PASS() \
    do { \
        std::cout << "PASSED" << std::endl; \
        ++g_passed; \
    } while(0)

#define FAIL(msg) \
    do { \
        std::cout << "FAILED: " << msg << std::endl; \
        ++g_failed; \
    } while(0)

#define CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            FAIL(msg); \
            return; \
        } \
    } while(0)

// ============================================================
// 测试1: 基本分配和释放
// ============================================================
void test_basic_alloc_free()
{
    TEST("basic allocate and free");
    MemoryPool pool(4, sizeof(int));
    CHECK(pool.freeCount() == 4, "initial freeCount should be 4");
    CHECK(pool.usedCount() == 0, "initial usedCount should be 0");

    int* a = pool.newObject<int>(42);
    int* b = pool.newObject<int>(99);
    CHECK(a != nullptr, "a should not be null");
    CHECK(b != nullptr, "b should not be null");
    CHECK(*a == 42, "*a should be 42");
    CHECK(*b == 99, "*b should be 99");
    CHECK(pool.usedCount() == 2, "usedCount should be 2 after 2 allocs");

    pool.deleteObject(a);
    CHECK(pool.usedCount() == 1, "usedCount should be 1 after 1 free");
    pool.deleteObject(b);
    CHECK(pool.usedCount() == 0, "usedCount should be 0 after 2 frees");
    CHECK(pool.freeCount() == 4, "freeCount should be back to 4");

    // 重新分配，验证复用
    int* c = pool.newObject<int>(7);
    CHECK(c != nullptr, "c should not be null");
    CHECK(*c == 7, "*c should be 7");
    pool.deleteObject(c);

    PASS();
}

// ============================================================
// 测试2: 二次释放检测
// ============================================================
void test_double_free_detection()
{
    TEST("double free detection");
    MemoryPool pool(4, sizeof(int));
    int* p = pool.newObject<int>(100);

    pool.deleteObject(p);

    bool caught = false;
    try {
        pool.deleteObject(p);  // 二次释放
    } catch (const std::runtime_error& e) {
        caught = true;
    }
    CHECK(caught, "should throw on double free");
    PASS();
}

// ============================================================
// 测试3: 非法指针检测（不属于内存池的指针）
// ============================================================
void test_invalid_pointer()
{
    TEST("invalid pointer detection");
    MemoryPool pool(4, sizeof(int));
    int stackVar = 42;

    bool caught = false;
    try {
        pool.deleteObject(&stackVar);  // 栈变量，不属于内存池
    } catch (const std::invalid_argument& e) {
        caught = true;
    }
    CHECK(caught, "should throw on invalid pointer");
    PASS();
}

// ============================================================
// 测试4: 空指针释放
// ============================================================
void test_null_pointer()
{
    TEST("null pointer delete");
    MemoryPool pool(4, sizeof(int));
    pool.deleteObject<int>(nullptr);  // 应该安全返回
    PASS();
}

// ============================================================
// 测试5: 扩容测试（分配超过初始容量的对象）
// ============================================================
void test_expand()
{
    TEST("expand when exhausted");
    MemoryPool pool(2, sizeof(int));  // 初始只有2个块

    int* a = pool.newObject<int>(1);
    int* b = pool.newObject<int>(2);
    CHECK(pool.totalBlockCount() == 2, "should have 2 blocks initially");
    CHECK(pool.freeCount() == 0, "freeCount should be 0");

    // 内存池已用完，应该触发扩容
    int* c = pool.newObject<int>(3);
    CHECK(c != nullptr, "c should not be null after expand");
    CHECK(*c == 3, "*c should be 3");
    CHECK(pool.totalBlockCount() == 4, "should have 4 blocks after expand");

    pool.deleteObject(a);
    pool.deleteObject(b);
    pool.deleteObject(c);
    PASS();
}

// ============================================================
// 测试6: 超大对象检测（sizeof(T) > _blockSize）
// ============================================================
void test_oversized_object()
{
    TEST("oversized object detection");

    struct BigStruct {
        char data[1024];
    };

    MemoryPool pool(4, sizeof(int));  // blockSize 只有 sizeof(int)

    bool caught = false;
    try {
        pool.newObject<BigStruct>();
    } catch (const std::runtime_error& e) {
        caught = true;
    }
    CHECK(caught, "should throw on oversized object");
    PASS();
}

// ============================================================
// 测试7: 内存收缩测试
// ============================================================
void test_shrink()
{
    TEST("memory shrink");

    MemoryPool pool(2, sizeof(int));

    // 触发扩容
    int* a = pool.newObject<int>(1);
    int* b = pool.newObject<int>(2);
    int* c = pool.newObject<int>(3);
    int* d = pool.newObject<int>(4);
    CHECK(pool.totalBlockCount() >= 4, "should have expanded");

    // 全部释放
    pool.deleteObject(a);
    pool.deleteObject(b);
    pool.deleteObject(c);
    pool.deleteObject(d);
    CHECK(pool.usedCount() == 0, "all should be freed");

    size_t before = pool.totalBlockCount();
    pool.shrink();
    size_t after = pool.totalBlockCount();
    CHECK(after < before, "should have shrunk");
    CHECK(after == 2, "should be back to initial blockCount");

    // 收缩后应该还能正常分配
    int* e = pool.newObject<int>(99);
    CHECK(e != nullptr, "should allocate after shrink");
    CHECK(*e == 99, "value should be correct after shrink");
    pool.deleteObject(e);

    PASS();
}

// ============================================================
// 测试8: 部分使用时收缩（chunk 0 有使用中的块，chunk 1+ 全部空闲）
// ============================================================
void test_shrink_partial_usage()
{
    TEST("shrink with partial usage (chunk 0 in use)");

    MemoryPool pool(2, sizeof(int));

    // 分配2个触发扩容到4个块（2个chunk）
    int* a = pool.newObject<int>(10);
    int* b = pool.newObject<int>(20);
    int* c = pool.newObject<int>(30);
    CHECK(pool.totalBlockCount() >= 4, "should have 2 chunks");

    // 释放 c（来自 chunk1），但保留 a, b（来自 chunk0）
    pool.deleteObject(c);
    // 现在 chunk0 的 usedCount=2, chunk1 的 usedCount=0

    size_t before = pool.totalBlockCount();
    pool.shrink();
    size_t after = pool.totalBlockCount();

    // 应该能收缩（只保留 chunk0，释放 chunk1）
    CHECK(after == 2, "should shrink to only chunk 0");
    CHECK(after < before, "should have shrunk");

    // a 和 b 应该仍然有效
    CHECK(*a == 10, "*a should still be 10 after shrink");
    CHECK(*b == 20, "*b should still be 20 after shrink");

    // 收缩后可以继续分配
    int* d = pool.newObject<int>(40);
    CHECK(d != nullptr, "should allocate after partial shrink");
    CHECK(*d == 40, "new allocation should work");

    pool.deleteObject(a);
    pool.deleteObject(b);
    pool.deleteObject(d);

    PASS();
}

// ============================================================
// 测试9: 多线程并发分配释放（压力测试 + 死锁检测）
// ============================================================
void test_multithreaded_stress()
{
    TEST("multithreaded stress test (deadlock + race detection)");

    constexpr int NUM_THREADS = 8;
    constexpr int OPS_PER_THREAD = 1000;
    constexpr int POOL_SIZE = 256;

    MemoryPool pool(POOL_SIZE, sizeof(int));
    std::atomic<bool> running{true};
    std::atomic<int> totalAllocs{0};
    std::atomic<int> totalFrees{0};

    auto worker = [&](int tid) {
        std::vector<int*> allocs;
        allocs.reserve(OPS_PER_THREAD);

        for (int i = 0; i < OPS_PER_THREAD; ++i) {
            if (allocs.size() >= POOL_SIZE || (rand() % 3 == 0 && !allocs.empty())) {
                // 随机释放一个
                int idx = rand() % allocs.size();
                int* p = allocs[idx];
                pool.deleteObject(p);
                allocs[idx] = allocs.back();
                allocs.pop_back();
                ++totalFrees;
            } else {
                // 分配
                try {
                    int* p = pool.newObject<int>(tid * 10000 + i);
                    if (p) {
                        allocs.push_back(p);
                        ++totalAllocs;
                    }
                } catch (const std::bad_alloc&) {
                    // 内存不足，忽略
                }
            }
        }

        // 清理剩余的
        for (int* p : allocs) {
            pool.deleteObject(p);
            ++totalFrees;
        }
    };

    std::vector<std::thread> threads;
    auto start = std::chrono::steady_clock::now();

    for (int i = 0; i < NUM_THREADS; ++i) {
        threads.emplace_back(worker, i);
    }

    for (auto& t : threads) {
        t.join();
    }

    auto end = std::chrono::steady_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

    std::cout << "(" << totalAllocs.load() << " allocs, "
              << totalFrees.load() << " frees, "
              << ms << "ms) ";

    // 验证最终状态一致性
    CHECK(totalAllocs.load() == totalFrees.load(),
          "total allocs should equal total frees");
    CHECK(pool.usedCount() == 0,
          "usedCount should be 0 after all frees");

    PASS();
}

// ============================================================
// 测试10: 多线程并发删除同一对象（TOCTOU 检测）
// ============================================================
void test_concurrent_double_delete()
{
    TEST("concurrent double delete (TOCTOU detection)");

    constexpr int NUM_THREADS = 4;

    MemoryPool pool(64, sizeof(int));
    int* p = pool.newObject<int>(42);

    std::atomic<int> successCount{0};
    std::atomic<int> failureCount{0};

    auto worker = [&]() {
        try {
            pool.deleteObject(p);
            ++successCount;
        } catch (const std::exception&) {
            ++failureCount;
        }
    };

    std::vector<std::thread> threads;
    for (int i = 0; i < NUM_THREADS; ++i) {
        threads.emplace_back(worker);
    }
    for (auto& t : threads) {
        t.join();
    }

    // 恰好一个线程成功释放，其余都应该检测到二次释放
    CHECK(successCount.load() == 1,
          "exactly one thread should succeed");
    CHECK(failureCount.load() == NUM_THREADS - 1,
          "remaining threads should detect double free");

    PASS();
}

// ============================================================
// 测试11: ObjectPool 封装测试
// ============================================================
void test_object_pool()
{
    TEST("ObjectPool wrapper");

    struct TestObj {
        int x;
        double y;
        TestObj(int a, double b) : x(a), y(b) {}
    };

    ObjectPool<TestObj> pool(8);
    CHECK(pool.freeCount() == 8, "ObjectPool initial freeCount should be 8");

    auto* obj = pool.acquire(42, 3.14);
    CHECK(obj != nullptr, "acquired object should not be null");
    CHECK(obj->x == 42, "obj->x should be 42");
    CHECK(obj->y == 3.14, "obj->y should be 3.14");
    CHECK(pool.usedCount() == 1, "ObjectPool usedCount should be 1");

    pool.release(obj);
    CHECK(pool.usedCount() == 0, "ObjectPool usedCount should be 0 after release");

    // 二次释放检测
    bool caught = false;
    try {
        pool.release(obj);
    } catch (const std::runtime_error&) {
        caught = true;
    }
    CHECK(caught, "ObjectPool should detect double release");

    PASS();
}

// ============================================================
// 测试12: peakUsed 统计
// ============================================================
void test_peak_used()
{
    TEST("peak used tracking");

    MemoryPool pool(2, sizeof(int));  // 初始2个块，便于触发扩容
    CHECK(pool.peakUsed() == 0, "initial peak should be 0");

    int* a = pool.newObject<int>(1);
    CHECK(pool.peakUsed() == 1, "peak should be 1");
    int* b = pool.newObject<int>(2);
    CHECK(pool.peakUsed() == 2, "peak should be 2");

    pool.deleteObject(b);
    CHECK(pool.peakUsed() == 2, "peak should stay at 2 after free");

    int* c = pool.newObject<int>(3);
    CHECK(pool.peakUsed() == 2, "peak should still be 2 (reuses freed slot)");

    // 现在2个块都在使用中(a,c)，下一次分配触发扩容(新增2个块，共4个块)
    int* d = pool.newObject<int>(4);
    CHECK(pool.peakUsed() == 3, "peak should be 3 (1 from new chunk)");

    pool.deleteObject(a);
    pool.deleteObject(c);
    pool.deleteObject(d);

    PASS();
}

// ============================================================
// 测试13: nullptr 分配边界（blockCount=0 应抛出异常）
// ============================================================
void test_zero_block_count()
{
    TEST("zero blockCount should throw");

    bool caught = false;
    try {
        MemoryPool pool(0, sizeof(int));
    } catch (const std::invalid_argument& e) {
        caught = true;
    }
    CHECK(caught, "should throw on blockCount=0");
    PASS();
}

// ============================================================
// 测试14: 多次扩容和收缩循环
// ============================================================
void test_expand_shrink_cycle()
{
    TEST("expand and shrink cycle");

    MemoryPool pool(2, sizeof(int));

    // 第1轮：扩容到4
    int* a1 = pool.newObject<int>(1);
    int* b1 = pool.newObject<int>(2);
    int* c1 = pool.newObject<int>(3);
    CHECK(pool.totalBlockCount() == 4, "round1: should have 4 blocks");

    pool.deleteObject(a1);
    pool.deleteObject(b1);
    pool.deleteObject(c1);
    pool.shrink();
    CHECK(pool.totalBlockCount() == 2, "round1: should shrink to 2");

    // 第2轮：再次扩容
    int* a2 = pool.newObject<int>(10);
    int* b2 = pool.newObject<int>(20);
    int* c2 = pool.newObject<int>(30);
    CHECK(pool.totalBlockCount() == 4, "round2: should have 4 blocks again");

    pool.deleteObject(a2);
    pool.deleteObject(b2);
    pool.deleteObject(c2);
    pool.shrink();
    CHECK(pool.totalBlockCount() == 2, "round2: should shrink to 2 again");

    PASS();
}

// ============================================================
// main
// ============================================================
int main()
{
    std::cout << "========== MemoryPool Tests ==========" << std::endl << std::endl;

    test_basic_alloc_free();
    test_double_free_detection();
    test_invalid_pointer();
    test_null_pointer();
    test_expand();
    test_oversized_object();
    test_shrink();
    test_shrink_partial_usage();
    test_multithreaded_stress();
    test_concurrent_double_delete();
    test_object_pool();
    test_peak_used();
    test_zero_block_count();
    test_expand_shrink_cycle();

    std::cout << std::endl;
    std::cout << "========== Results ==========" << std::endl;
    std::cout << "Passed: " << g_passed << std::endl;
    std::cout << "Failed: " << g_failed << std::endl;

    return g_failed > 0 ? 1 : 0;
}
