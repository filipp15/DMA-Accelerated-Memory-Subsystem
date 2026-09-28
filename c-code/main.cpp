#include "arena_allocator.hpp"
#include "pool_allocator.hpp"

#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <vector>

using Clock = std::chrono::steady_clock;

static bool is_aligned(const void* ptr, std::size_t alignment) {
    return (reinterpret_cast<std::uintptr_t>(ptr) % alignment) == 0;
}

void test_arena_correctness() {
    std::printf("== ArenaAllocator: test ispravnosti ==\n");

    mem::ArenaAllocator arena(1 << 20); 
    
    float* buffer = arena.allocate<float>(1024);
    assert(buffer != nullptr);
    assert(is_aligned(buffer, 64));

    for (int i = 0; i < 1024; ++i) buffer[i] = static_cast<float>(i);
    for (int i = 0; i < 1024; ++i) assert(buffer[i] == static_cast<float>(i));

    std::printf("  Poravnanje OK (64B), podaci OK, iskorišćeno %zu / %zu B\n",
                arena.used(), arena.capacity());

    arena.reset();
    assert(arena.used() == 0);
    std::printf("  reset() OK - arena je ponovo prazna\n\n");
}

void test_pool_correctness() {
    std::printf("== PoolAllocator: test ispravnosti ==\n");

    mem::PoolAllocator pool(128, 100);

    std::vector<void*> taken;
    for (int i = 0; i < 100; ++i) {
        void* p = pool.allocate();
        assert(p != nullptr);
        assert(is_aligned(p, 64));
        taken.push_back(p);
    }
    assert(pool.free_count() == 0);
    assert(pool.allocate() == nullptr); 

    for (int i = 0; i < 50; ++i) pool.deallocate(taken[i]);
    assert(pool.free_count() == 50);

    for (int i = 0; i < 50; ++i) {
        void* p = pool.allocate();
        assert(p != nullptr);
    }
    assert(pool.free_count() == 0);

    std::printf("  Alokacija/dealokacija 100 blokova OK, poravnanje OK\n\n");
}

void benchmark_arena_vs_malloc() {
    constexpr int N = 1'000'000;
    constexpr std::size_t BLOCK = 64; 

    std::printf("== Benchmark: %d alokacija po %zu B ==\n", N, BLOCK);

    {
        auto t0 = Clock::now();
        std::vector<void*> ptrs;
        ptrs.reserve(N);
        for (int i = 0; i < N; ++i) {
            ptrs.push_back(std::malloc(BLOCK));
        }
        auto t1 = Clock::now();
        for (void* p : ptrs) std::free(p);

        double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
        std::printf("  malloc:          %.2f ms  (%.1f ns/alokacija)\n",
                    ms, ms * 1e6 / N);
    }

    {
        mem::ArenaAllocator arena(static_cast<std::size_t>(N) * BLOCK + (1 << 20));
        auto t0 = Clock::now();
        for (int i = 0; i < N; ++i) {
            void* p = arena.allocate(BLOCK);
            assert(p != nullptr);
        }
        auto t1 = Clock::now();

        double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
        std::printf("  ArenaAllocator:  %.2f ms  (%.1f ns/alokacija)\n",
                    ms, ms * 1e6 / N);
    }

    {
        mem::PoolAllocator pool(BLOCK, N);
        std::vector<void*> ptrs;
        ptrs.reserve(N);

        auto t0 = Clock::now();
        for (int i = 0; i < N; ++i) {
            ptrs.push_back(pool.allocate());
        }
        auto t1 = Clock::now();
        for (void* p : ptrs) pool.deallocate(p);

        double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
        std::printf("  PoolAllocator:   %.2f ms  (%.1f ns/alokacija)\n\n",
                    ms, ms * 1e6 / N);
    }
}

int main() {
    test_arena_correctness();
    test_pool_correctness();
    benchmark_arena_vs_malloc();
    std::printf("Svi testovi prošli.\n");
    return 0;
}
