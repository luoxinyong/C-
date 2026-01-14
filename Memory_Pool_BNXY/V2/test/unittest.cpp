#include <iostream>
#include <vector>
#include <thread>
#include <atomic>
#include <cassert>
#include <cstring>
#include <chrono>

#include "MemoryPool.h"

using namespace Bnxy_memoryPool;

/* ===========================
 * 工具函数
 * =========================== */
static void fill_pattern(void* p, size_t size, unsigned char v)
{
    std::memset(p, v, size);
}

static void check_pattern(void* p, size_t size, unsigned char v)
{
    unsigned char* c = static_cast<unsigned char*>(p);
    for (size_t i = 0; i < size; ++i)
    {
        assert(c[i] == v);
    }
}

/* ===========================
 * Test 1: 基础分配释放
 * =========================== */
void test_basic_alloc_free()
{
    std::cout << "[Test] basic alloc/free\n";

    void* p = MemoryPool::allocate(32);
    std::cout << "[Test] basic alloc\n";
    assert(p != nullptr);
    
    fill_pattern(p, 32, 0xAA);
    check_pattern(p, 32, 0xAA);

    MemoryPool::deallocate(p, 32);
}

/* ===========================
 * Test 2: 多 size class
 * =========================== */
void test_multi_size()
{
    std::cout << "[Test] multi size class\n";

    std::vector<void*> ptrs;
    std::vector<size_t> sizes = {8, 16, 24, 64, 128, 256, 1024};

    for (size_t s : sizes)
    {
        void* p = MemoryPool::allocate(s);
        assert(p);
        fill_pattern(p, s, static_cast<unsigned char>(s));
        ptrs.push_back(p);
    }
    
    for (size_t i = 0; i < ptrs.size(); ++i)
    {
        check_pattern(ptrs[i], sizes[i], static_cast<unsigned char>(sizes[i]));
        MemoryPool::deallocate(ptrs[i], sizes[i]);
    }
}

/* ===========================
 * Test 3: ThreadCache 复用验证
 * =========================== */
void test_thread_reuse()
{
    std::cout << "[Test] thread cache reuse\n";

    void* p1 = MemoryPool::allocate(64);
    MemoryPool::deallocate(p1, 64);

    void* p2 = MemoryPool::allocate(64);

    // 很大概率是同一块（不是 100%，但在你实现下应该成立）
    assert(p1 == p2);

    MemoryPool::deallocate(p2, 64);
}

/* ===========================
 * Test 4: 大对象（PageCache 路径）
 * =========================== */
void test_large_object()
{
    std::cout << "[Test] large object\n";

    size_t big = 512 * 1024; // 512KB
    void* p = MemoryPool::allocate(big);
    assert(p);

    fill_pattern(p, big, 0x55);
    check_pattern(p, big, 0x55);

    MemoryPool::deallocate(p, big);
}

/* ===========================
 * Test 5: 多线程隔离
 * =========================== */
void test_multithread_isolation()
{
    std::cout << "[Test] multithread isolation\n";

    constexpr int kThreadNum = 4;
    constexpr int kLoop = 1000;

    std::atomic<bool> ok{true};

    auto worker = [&]() {
        for (int i = 0; i < kLoop; ++i)
        {
            void* p = MemoryPool::allocate(64);
            if (!p) ok = false;
            fill_pattern(p, 64, 0xCC);
            MemoryPool::deallocate(p, 64);
        }
    };

    std::vector<std::thread> threads;
    for (int i = 0; i < kThreadNum; ++i)
        threads.emplace_back(worker);

    for (auto& t : threads)
        t.join();

    assert(ok);
}

/* ===========================
 * Test 6: CentralCache 回收压力
 * =========================== */
void test_central_pressure()
{
    std::cout << "[Test] central cache pressure\n";

    constexpr int N = 5000;
    std::vector<void*> ptrs;

    for (int i = 0; i < N; ++i)
    {
        ptrs.push_back(MemoryPool::allocate(128));
    }

    for (void* p : ptrs)
    {
        MemoryPool::deallocate(p, 128);
    }

    // 再来一轮，逼 central / thread 复用
    for (int i = 0; i < N; ++i)
    {
        void* p = MemoryPool::allocate(128);
        assert(p);
        MemoryPool::deallocate(p, 128);
    }
}

/* ===========================
 * main
 * =========================== */
int main()
{
    test_basic_alloc_free();
    //test_multi_size();
    test_thread_reuse();
    test_large_object();
    test_multithread_isolation();
    test_central_pressure();

    std::cout << "\nAll tests passed.\n";
    return 0;
}
