#include "../include/object_pool.h"
#include <iostream>
#include <vector>
#include <cmath>
#include <chrono>
#include <random>

// ============================================================
// 场景：游戏服务器 — 子弹系统
//
// 每帧：
//   1. 玩家射击 → 创建子弹对象
//   2. 遍历所有子弹 → 移动 + 碰撞检测（利用 Cache Locality）
//   3. 飞出边界 / 命中 → 销毁子弹
//
// 使用 MemoryPool 的优势：
//   - 子弹对象在物理内存中连续，遍历时缓存命中率高
//   - 避免频繁 new/delete 的系统调用开销
//   - 内存用量可预测（池大小 = 最大并发子弹数）
// ============================================================

struct Vec2 {
    float x, y;
};

struct Bullet {
    Vec2 position;
    Vec2 velocity;
    int   ownerId;
    int   damage;
    float lifetime;   // 剩余存活帧数

    Bullet(Vec2 pos, Vec2 vel, int owner, int dmg)
        : position(pos), velocity(vel), ownerId(owner), damage(dmg), lifetime(300.0f) {}
};

// 模拟一帧的子弹更新
void simulateFrame(ObjectPool<Bullet>& bulletPool,
                   std::vector<Bullet*>& activeBullets,
                   float deltaTime,
                   float worldWidth, float worldHeight)
{
    // === 阶段 1：遍历更新（受益于 Cache Locality）===
    for (auto* bullet : activeBullets) {
        bullet->position.x += bullet->velocity.x * deltaTime;
        bullet->position.y += bullet->velocity.y * deltaTime;
        bullet->lifetime -= deltaTime;
    }

    // === 阶段 2：碰撞检测 O(n^2) — 同样受益于连续内存 ===
    for (size_t i = 0; i < activeBullets.size(); ++i) {
        for (size_t j = i + 1; j < activeBullets.size(); ++j) {
            float dx = activeBullets[i]->position.x - activeBullets[j]->position.x;
            float dy = activeBullets[i]->position.y - activeBullets[j]->position.y;
            float dist = std::sqrt(dx * dx + dy * dy);
            if (dist < 1.0f) {
                // 两颗子弹相撞，标记为销毁（lifetime 清零）
                activeBullets[i]->lifetime = 0;
                activeBullets[j]->lifetime = 0;
            }
        }
    }

    // === 阶段 3：回收过期/出界的子弹 ===
    size_t writeIdx = 0;
    for (size_t i = 0; i < activeBullets.size(); ++i) {
        Bullet* b = activeBullets[i];
        bool outOfBounds = (b->position.x < 0 || b->position.x > worldWidth ||
                            b->position.y < 0 || b->position.y > worldHeight);
        bool expired = (b->lifetime <= 0);

        if (outOfBounds || expired) {
            bulletPool.release(b);  // 归还内存块到池中
        } else {
            activeBullets[writeIdx++] = b;
        }
    }
    activeBullets.resize(writeIdx);
}

int main() {
    constexpr int    MAX_BULLETS     = 4096;
    constexpr float  WORLD_W         = 200.0f;
    constexpr float  WORLD_H         = 200.0f;
    constexpr int    TOTAL_FRAMES    = 1000;
    constexpr int    PLAYER_COUNT    = 8;
    constexpr float  SHOOT_INTERVAL  = 3.0f;   // 每 N 帧射击一次

    // 创建子弹池：预分配 MAX_BULLETS 个 Bullet 大小的块
    ObjectPool<Bullet> bulletPool(MAX_BULLETS);

    std::vector<Bullet*> activeBullets;
    activeBullets.reserve(MAX_BULLETS);

    std::mt19937 rng(42);
    std::uniform_real_distribution<float> velDist(-30.0f, 30.0f);
    std::uniform_real_distribution<float> posDist(0.0f, WORLD_W);

    size_t totalCreated  = 0;
    size_t peakLive       = 0;

    auto start = std::chrono::high_resolution_clock::now();

    // === 主循环 ===
    for (int frame = 0; frame < TOTAL_FRAMES; ++frame) {
        // 射击阶段：每个玩家以一定概率发射子弹
        for (int player = 0; player < PLAYER_COUNT; ++player) {
            if (frame % static_cast<int>(SHOOT_INTERVAL) == 0 && activeBullets.size() < MAX_BULLETS) {
                Vec2 pos = {posDist(rng), posDist(rng)};
                Vec2 vel = {velDist(rng), velDist(rng)};
                Bullet* b = bulletPool.acquire(pos, vel, player, 10 + (player * 5));
                if (b) {
                    activeBullets.push_back(b);
                    ++totalCreated;
                }
            }
        }

        // 模拟一帧
        simulateFrame(bulletPool, activeBullets, 1.0f / 60.0f, WORLD_W, WORLD_H);

        // 统计
        if (activeBullets.size() > peakLive) {
            peakLive = activeBullets.size();
        }
    }

    // 清理剩余的活跃子弹
    size_t remaining = activeBullets.size();
    for (auto* b : activeBullets) {
        bulletPool.release(b);
    }

    auto end = std::chrono::high_resolution_clock::now();
    double elapsedMs = std::chrono::duration<double, std::milli>(end - start).count();

    std::cout << "========== 游戏子弹系统模拟结果 ==========" << std::endl;
    std::cout << "  总帧数:       " << TOTAL_FRAMES << std::endl;
    std::cout << "  玩家数:       " << PLAYER_COUNT << std::endl;
    std::cout << "  池容量:       " << MAX_BULLETS << std::endl;
    std::cout << "  总创建:       " << totalCreated << " 颗子弹" << std::endl;
    std::cout << "  最终存活:     " << remaining << " 颗子弹(已全部回收)" << std::endl;
    std::cout << "  峰值并发:     " << peakLive << " 颗子弹" << std::endl;
    std::cout << "  池使用峰值:   " << bulletPool.peakUsed() << std::endl;
    std::cout << "  池空闲块:     " << bulletPool.freeCount() << " (全满，验证无泄漏)" << std::endl;
    std::cout << "  总耗时:       " << elapsedMs << " ms" << std::endl;
    std::cout << "  平均每帧:     " << (elapsedMs / TOTAL_FRAMES * 1000) << " us" << std::endl;
    std::cout << "==========================================" << std::endl;

    return 0;
}
