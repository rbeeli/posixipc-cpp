#pragma once

#include <atomic>
#include <cstdint>
#include <cstddef>
#include <expected>
#include <format>
#include <source_location>

#include "posix_ipc/errors.hpp"

static_assert(std::atomic<uint64_t>::is_always_lock_free, "Atomic of size_t is not lock-free");
static_assert(sizeof(void*) == 8, "Only supporting 64 bit builds");

namespace posix_ipc
{
namespace queues
{
namespace spsc
{
using byte = std::byte;

/// @brief Storage for SPSCQueue.
/// @details
/// This struct is used to store the read and write indices of the queue.
/// It also stores the overall storage size in bytes as an uint64_t at the beginning.
/// The queue buffer is stored in shared memory right after the storage object itself
/// as a contiguous block of memory.
struct SPSCStorage
{
    static constexpr uint64_t CACHE_LINE_SIZE = 64;

    // buffer_size = storage_size_ - buffer_offset
    static constexpr uint64_t BUFFER_OFFSET = 3 * CACHE_LINE_SIZE;

    alignas(CACHE_LINE_SIZE) uint64_t storage_size_;
    alignas(CACHE_LINE_SIZE) std::atomic<uint64_t> read_ix_;
    alignas(CACHE_LINE_SIZE) std::atomic<uint64_t> write_ix_;

    SPSCStorage(uint64_t storage_size) noexcept
        : storage_size_(storage_size), read_ix_(0), write_ix_(0)
    {
        assert(
            reinterpret_cast<char*>(&write_ix_) - reinterpret_cast<char*>(&read_ix_) ==
            static_cast<ptrdiff_t>(CACHE_LINE_SIZE)
        );
    }

    // disable copy & move
    SPSCStorage(const SPSCStorage&) = delete;
    SPSCStorage& operator=(const SPSCStorage&) = delete;
    SPSCStorage(SPSCStorage&&) = delete;
    SPSCStorage& operator=(SPSCStorage&&) = delete;

    [[nodiscard]] inline uint64_t buffer_size() const noexcept
    {
        return storage_size_ - BUFFER_OFFSET;
    }

    [[nodiscard]] inline byte* buffer() noexcept
    {
        return reinterpret_cast<byte*>(this) + BUFFER_OFFSET;
    }


    /* ---------- factory ---------- */

    struct Deleter
    {
        void operator()(SPSCStorage* p) const noexcept
        {
            if (p)
            {
                p->~SPSCStorage();
                ::operator delete(p, std::align_val_t{CACHE_LINE_SIZE});
            }
        }
    };

    static std::expected<std::unique_ptr<SPSCStorage, Deleter>, PosixIpcError> create(
        uint64_t storage_size, const std::source_location& loc = std::source_location::current()
    ) noexcept
    {
        if (storage_size <= BUFFER_OFFSET)
        {
            return std::unexpected{PosixIpcError(
                PosixIpcErrorCode::spsc_storage_error,
                std::format("Storage size {} too small", storage_size),
                loc
            )};
        }

        void* raw = ::operator new(
            storage_size, std::align_val_t{CACHE_LINE_SIZE}, std::nothrow_t{}
        );
        if (!raw)
        {
            return std::unexpected{PosixIpcError(
                PosixIpcErrorCode::spsc_storage_error,
                std::format("Allocating {} bytes failed", storage_size),
                loc
            )};
        }

        return std::unique_ptr<SPSCStorage, Deleter>{new (raw) SPSCStorage(storage_size)};
    }
};

static_assert(
    alignof(SPSCStorage) == SPSCStorage::CACHE_LINE_SIZE, "SPSCStorage not cache aligned"
);
static_assert(sizeof(SPSCStorage) == 3 * SPSCStorage::CACHE_LINE_SIZE, "");
static_assert(sizeof(SPSCStorage) == SPSCStorage::BUFFER_OFFSET, "");
} // namespace spsc
} // namespace queues
} // namespace posix_ipc
