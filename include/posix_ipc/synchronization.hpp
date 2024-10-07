#pragma once

#include <atomic>

namespace posix_ipc
{
// class IdSpinLock
// {
//     static_assert(std::atomic<int>::is_always_lock_free);

// private:
//     alignas(64) std::atomic<int> lock_id{-1};

// public:
//     inline void lock(int id) noexcept
//     {
//         int expected = -1;
//         while (!lock_id.compare_exchange_weak(expected, id, std::memory_order::acquire))
//         {
//             expected = -1;
//         }
//     }

//     inline void unlock(int id) noexcept
//     {
//         lock_id.store(-1, std::memory_order::release);
//     }
// };

// struct IdSpinLockGuard
// {
//     IdSpinLock& lock;
//     int id;

//     inline IdSpinLockGuard(IdSpinLock& lock, int id) noexcept : lock(lock), id(id)
//     {
//         lock.lock(id);
//     }

//     inline ~IdSpinLockGuard() noexcept
//     {
//         lock.unlock(id);
//     }
// };

// struct SpinLock
// {
//     std::atomic<bool> lock_{0};

//     __attribute__((always_inline)) inline void lock() noexcept
//     {
//         for (;;)
//         {
//             // Optimistically assume the lock is free on the first try
//             if (!lock_.exchange(true, std::memory_order_acquire))
//                 return;
//             // Wait for lock to be released without generating cache misses
//             while (lock_.load(std::memory_order_relaxed))
//             {
//                 // Issue X86 PAUSE or ARM YIELD instruction to reduce contention between
//                 // hyper-threads
//                 __builtin_ia32_pause();
//             }
//         }
//     }

//     __attribute__((always_inline)) inline bool try_lock() noexcept
//     {
//         // First do a relaxed load to check if lock is free in order to prevent
//         // unnecessary cache misses if someone does while(!try_lock())
//         return !lock_.load(std::memory_order_relaxed) &&
//                !lock_.exchange(true, std::memory_order_acquire);
//     }

//     __attribute__((always_inline)) inline void unlock() noexcept
//     {
//         lock_.store(false, std::memory_order_release);
//     }
// };

class SpinLock
{
    std::atomic_flag locked = ATOMIC_FLAG_INIT;

public:
    void lock()
    {
        while (locked.test_and_set(std::memory_order::acquire))
        {
            ;
        }
    }
    void unlock()
    {
        locked.clear(std::memory_order::release);
    }
};

struct SpinLockGuard
{
    SpinLock& lock_;

    inline SpinLockGuard(SpinLock& lock) noexcept //
        : lock_(lock)
    {
        lock_.lock();
    }

    inline ~SpinLockGuard() noexcept
    {
        lock_.unlock();
    }
};
} // namespace posix_ipc
