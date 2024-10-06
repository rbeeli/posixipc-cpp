#pragma once

#include <atomic>

namespace posix_ipc
{
class IdSpinLock
{
    static_assert(std::atomic<int>::is_always_lock_free);

private:
    alignas(64) std::atomic<int> lock_id{-1};

public:
    inline void lock(int id) noexcept
    {
        int expected = -1;
        while (!lock_id.compare_exchange_weak(expected, id, std::memory_order::acquire))
        {
            expected = -1;
        }
    }

    inline void unlock(int id) noexcept
    {
        lock_id.store(-1, std::memory_order::release);
    }
};

struct IdSpinLockGuard
{
    IdSpinLock& lock;
    int id;

    inline IdSpinLockGuard(IdSpinLock& lock, int id) noexcept : lock(lock), id(id)
    {
        lock.lock(id);
    }

    inline ~IdSpinLockGuard() noexcept
    {
        lock.unlock(id);
    }
};
} // namespace posix_ipc
