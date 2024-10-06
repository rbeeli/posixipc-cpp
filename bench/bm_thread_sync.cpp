#include <atomic>
#include <mutex>

#include <benchmark/benchmark.h>
#include <posix_ipc/rdtsc.hpp>

namespace bm = benchmark;

class load_store_lock
{
    std::atomic<bool> counter{false};

public:
    inline void lock() noexcept
    {
        while (counter.load(std::memory_order::acquire) != false)
            ;
    }

    inline void unlock() noexcept
    {
        counter.store(false, std::memory_order::release);
    }
};

static void load_store(bm::State& state)
{
    load_store_lock lock;

    int64_t at{0};
    for (auto _ : state)
    {
        lock.lock();
        benchmark::DoNotOptimize(lock);
        at += posix_ipc::rdtsc::read();
        lock.unlock();
    }

    benchmark::DoNotOptimize(at);
}

class mutex_lock
{
    std::mutex _mtx;

public:
    inline void lock() noexcept
    {
        _mtx.lock();
    }

    inline void unlock() noexcept
    {
        _mtx.unlock();
    }
};

static void mutex(bm::State& state)
{
    mutex_lock lock;

    int64_t at{0};
    for (auto _ : state)
    {
        lock.lock();
        benchmark::DoNotOptimize(lock);
        at += posix_ipc::rdtsc::read();
        lock.unlock();
    }

    benchmark::DoNotOptimize(at);
}

class atomic_flag_lock
{
    std::atomic_flag _lock{ATOMIC_FLAG_INIT};

public:
    inline void lock() noexcept
    {
        while (_lock.test_and_set(std::memory_order::acquire))
            ;
    }

    inline void unlock() noexcept
    {
        _lock.clear(std::memory_order::release);
    }
};

static void atomic_flag(bm::State& state)
{
    atomic_flag_lock lock;

    int64_t at{0};
    for (auto _ : state)
    {
        lock.lock();
        benchmark::DoNotOptimize(lock);
        at += posix_ipc::rdtsc::read();
        lock.unlock();
    }

    benchmark::DoNotOptimize(at);
}

BENCHMARK(load_store);
BENCHMARK(mutex);
BENCHMARK(atomic_flag);
