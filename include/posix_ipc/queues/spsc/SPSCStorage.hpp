#pragma once

#include <atomic>
#include <cstdint>
#include <cstddef>

namespace posix_ipc
{
namespace queues
{
namespace spsc
{
using std::byte;

/// @brief Storage for SPSCQueue.
/// @details
/// This struct is used to store the read and write indices of the queue.
/// It also stores the overall storage size in bytes as an uint64_t at the beginning.
/// The queue buffer is stored in shared memory right after the storage object itself
/// as a contiguous block of memory.
struct SPSCStorage
{
    static constexpr uint64_t CACHE_LINE_SIZE = 64;

    // buffer_size = storage_size - buffer_offset)
    static constexpr uint64_t BUFFER_OFFSET = 3 * CACHE_LINE_SIZE;

    alignas(CACHE_LINE_SIZE) uint64_t storage_size;
    alignas(CACHE_LINE_SIZE) std::atomic<uint64_t> read_ix;
    alignas(CACHE_LINE_SIZE) std::atomic<uint64_t> write_ix;

    SPSCStorage(uint64_t storage_size) noexcept
        : storage_size(storage_size), read_ix(0), write_ix(0)
    {
        assert(
            reinterpret_cast<char*>(&write_ix) - reinterpret_cast<char*>(&read_ix) ==
            static_cast<ptrdiff_t>(CACHE_LINE_SIZE)
        );
    }

    // disable copy
    SPSCStorage(const SPSCStorage&) = delete;
    SPSCStorage& operator=(const SPSCStorage&) = delete;

    // disable move
    SPSCStorage(SPSCStorage&&) = delete;
    SPSCStorage& operator=(SPSCStorage&&) = delete;

    [[nodiscard]] inline uint64_t buffer_size() const noexcept
    {
        return storage_size - BUFFER_OFFSET;
    }

    [[nodiscard]] inline byte* buffer() noexcept
    {
        return reinterpret_cast<byte*>(this) + BUFFER_OFFSET;
    }
};

// compile-time checks
static_assert(
    alignof(SPSCStorage) == SPSCStorage::CACHE_LINE_SIZE, "SPSCStorage not cache aligned"
);
static_assert(sizeof(SPSCStorage) == 3 * SPSCStorage::CACHE_LINE_SIZE, "");
static_assert(sizeof(SPSCStorage) == SPSCStorage::BUFFER_OFFSET, "");
static_assert(std::atomic<uint64_t>::is_always_lock_free, "Atomic of size_t is not lock-free");
} // namespace spsc
} // namespace queues
} // namespace posix_ipc
