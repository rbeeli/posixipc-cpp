#pragma once

#include <atomic>
#include <array>
#include <cstdint>
#include <cstddef>
#include <expected>
#include <new>
#include <format>
#include <source_location>

#include "posix_ipc/errors.hpp"

static_assert(std::atomic<size_t>::is_always_lock_free, "Atomic of size_t is not lock-free");
static_assert(sizeof(void*) == 8, "Only supporting 64 bit builds");

namespace posix_ipc
{
namespace queues
{
namespace spsc
{
inline constexpr size_t SPSC_CACHE_LINE_SIZE = 64;  // bytes per cache line
inline constexpr uint32_t SPSC_MAGIC = 0x53505343u; // 'S' 'P' 'S' 'C'
inline constexpr uint32_t SPSC_ABI_VERSION = 1u;    // bump when layout changes

/// @brief Storage for SPSCQueue.
/// @details
/// This struct is used to store the read and write indices of the queue.
/// It also stores the overall storage size in bytes as an size_t at the beginning.
/// The queue buffer is stored in shared memory right after the storage object itself
/// as a contiguous block of memory.
struct alignas(SPSC_CACHE_LINE_SIZE) SPSCStorage final
{
    // ---------- first 64 byte cache line ----------
    uint32_t magic_;                                        // must equal SPSC_MAGIC
    uint32_t abi_version_;                                  // must equal SPSC_ABI_VERSION
    size_t storage_size_;                                   //
    std::array<std::byte, SPSC_CACHE_LINE_SIZE - 16> _pad0; // keep next field 64-byte aligned

    // ---------- remaining 4 cache lines -----------
    alignas(SPSC_CACHE_LINE_SIZE) std::atomic<size_t> read_ix_{0};
    alignas(SPSC_CACHE_LINE_SIZE) std::atomic<size_t> write_ix_{0};
    alignas(SPSC_CACHE_LINE_SIZE) std::atomic<size_t> msg_count_{0};

    static constexpr size_t BUFFER_OFFSET = 4 * SPSC_CACHE_LINE_SIZE;

    SPSCStorage(size_t storage_size) noexcept
        : magic_(SPSC_MAGIC), //
          abi_version_(SPSC_ABI_VERSION),
          storage_size_(storage_size)
    {
        assert(
            reinterpret_cast<char*>(&write_ix_) - reinterpret_cast<char*>(&read_ix_) ==
            static_cast<ptrdiff_t>(SPSC_CACHE_LINE_SIZE)
        );
    }

    // disable copy & move
    SPSCStorage(const SPSCStorage&) = delete;
    SPSCStorage& operator=(const SPSCStorage&) = delete;
    SPSCStorage(SPSCStorage&&) = delete;
    SPSCStorage& operator=(SPSCStorage&&) = delete;

    [[nodiscard]] inline size_t storage_size() const noexcept
    {
        return storage_size_;
    }

    [[nodiscard]] inline size_t buffer_size() const noexcept
    {
        return storage_size_ - BUFFER_OFFSET;
    }

    [[nodiscard]] inline std::byte* buffer() noexcept
    {
        return reinterpret_cast<std::byte*>(this) + BUFFER_OFFSET;
    }


    /* ---------- factory ---------- */

    struct Deleter
    {
        void operator()(SPSCStorage* p) const noexcept
        {
            if (p)
            {
                p->~SPSCStorage();
                ::operator delete(p, std::align_val_t{SPSC_CACHE_LINE_SIZE}, std::nothrow_t{});
            }
        }
    };

    /**
     * Allocates a new SPSCStorage in heap memory.
     */
    [[nodiscard]] static std::expected<std::unique_ptr<SPSCStorage, Deleter>, PosixIpcError> create(
        size_t storage_size, const std::source_location& loc = std::source_location::current()
    ) noexcept
    {
        if (storage_size <= BUFFER_OFFSET)
        {
            return std::unexpected{PosixIpcError(
                PosixIpcErrorCode::spsc_storage_error,
                std::format(
                    "SPSCStorage requires a memory region greater than {} bytes, "
                    "requested size of {} bytes is too small.",
                    BUFFER_OFFSET,
                    storage_size
                ),
                loc
            )};
        }

        void* raw = ::operator new(
            storage_size, std::align_val_t{SPSC_CACHE_LINE_SIZE}, std::nothrow_t{}
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

    /**
     * Maps a SPSCStorage object from a pointer address (usually shared memory).
     */
    [[nodiscard]] static std::expected<SPSCStorage*, PosixIpcError> map(
        std::byte* ptr, size_t len
    ) noexcept
    {
        using E = PosixIpcErrorCode;

        if (!ptr)
            return std::unexpected{
                PosixIpcError(E::spsc_storage_error, "SPSCStorage mapping pointer is null")
            };

        if (len < sizeof(SPSCStorage))
            return std::unexpected{
                PosixIpcError(E::spsc_storage_error, "Mapping smaller than SPSCStorage header")
            };

        if (reinterpret_cast<uintptr_t>(ptr) % SPSC_CACHE_LINE_SIZE != 0)
            return std::unexpected{PosixIpcError(
                E::spsc_storage_error, "SPSCStorage mapping memory address not 64-byte aligned"
            )};

        auto* s = std::launder(reinterpret_cast<SPSCStorage*>(ptr));

        if (s->magic_ != SPSC_MAGIC)
            return std::unexpected{
                PosixIpcError(E::spsc_storage_error, "SPSCStorage bad SPSC_MAGIC")
            };

        if (s->abi_version_ != SPSC_ABI_VERSION)
            return std::unexpected{
                PosixIpcError(E::spsc_storage_error, "SPSCStorage ABI version mismatch")
            };

        if (s->storage_size_ != len)
            return std::unexpected{PosixIpcError(
                E::spsc_storage_error,
                std::format(
                    "SPSCStorage buffer vs. stored size mismatch: {} != {}", len, s->storage_size()
                )
            )};

        return s;
    }
};

static_assert(alignof(SPSCStorage) == SPSC_CACHE_LINE_SIZE, "SPSCStorage not cache aligned");
static_assert(sizeof(SPSCStorage) == 4 * SPSC_CACHE_LINE_SIZE, "");
static_assert(sizeof(SPSCStorage) == SPSCStorage::BUFFER_OFFSET, "");
static_assert(
    offsetof(SPSCStorage, write_ix_) - offsetof(SPSCStorage, read_ix_) == SPSC_CACHE_LINE_SIZE
);
} // namespace spsc
} // namespace queues
} // namespace posix_ipc
