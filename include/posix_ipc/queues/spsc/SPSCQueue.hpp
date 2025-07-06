#pragma once

#include <atomic>
#include <cstdint>
#include <iostream>
#include <format>
#include <cassert>

#include "posix_ipc/queues/Message.hpp"
#include "posix_ipc/queues/spsc/SPSCStorage.hpp"

static_assert(std::atomic<uint64_t>::is_always_lock_free, "Atomic of size_t is not lock-free");
static_assert(sizeof(void*) == 8, "Only supporting 64 bit builds");

namespace posix_ipc
{
namespace queues
{
namespace spsc
{
/// A lock-free, single-producer, single-consumer queue.
///
/// This queue is a circular buffer with a fixed capacity.
/// It supports variable sized messages.
/// It is a single-producer, single-consumer queue, meaning that one thread
/// can push to the queue and one thread can pop from the queue
/// without any synchronization.
///
/// A message consists of a header and a payload. The header contains
/// the size of the payload as size_t (8 bytes on 64 bit systems).
/// The payload contains the actual data.
///
/// Please note that the capacity of the queue must be at least twice
/// the maximum message size.
///
/// Implementation details
/// ----------------------
/// If (read_ix == writeIdx), the queue is considered empty.
/// If (writeIdx + message size) would cross read_ix,
/// the queue is considered full and publish will return false.
///
/// If the message crosses the end of the buffer, it is instead written
/// to the beginning if there is sufficient space available.
/// At the current write index, a header with size 0 is written to indicate
/// that the message wraps around.
class SPSCQueue
{
private:
    alignas(SPSCStorage::CACHE_LINE_SIZE) SPSCStorage* storage_;
    alignas(SPSCStorage::CACHE_LINE_SIZE) size_t cached_read_ix_;
    alignas(SPSCStorage::CACHE_LINE_SIZE) size_t cached_write_ix_;

    const size_t max_message_size_; // max size single message in bytes (including header bytes)
    const size_t buffer_size_;
    std::byte* buffer_;

public:
    SPSCQueue(SPSCStorage* storage) noexcept
        : storage_(storage),
          cached_read_ix_(storage->read_ix_.load(std::memory_order::acquire)),
          cached_write_ix_(storage->write_ix_.load(std::memory_order::acquire)),
          max_message_size_(calc_max_message_size(storage)),
          buffer_size_(storage->buffer_size()),
          buffer_(storage->buffer())
    {
    }

    [[nodiscard]] static std::expected<SPSCQueue, PosixIpcError> create(
        SPSCStorage* storage, const std::source_location& loc = std::source_location::current()
    ) noexcept
    {
        // ensure buffer size is divisible by 8
        if (storage->buffer_size() % 8 != 0)
        {
            return std::unexpected{PosixIpcError(
                PosixIpcErrorCode::spsc_storage_error,
                std::format(
                    "Storage buffer size must be divisible by 8, got value {}.",
                    storage->buffer_size()
                ),
                loc
            )};
        }

        // ensure can store at least 1 byte
        const size_t min_required = next_index(0, sizeof(uint64_t) + 8);
        if (storage->buffer_size() < min_required)
        {
            return std::unexpected{
                PosixIpcError(PosixIpcErrorCode::spsc_storage_error, "Queue buffer too small", loc)
            };
        }

        return std::expected<SPSCQueue, PosixIpcError>{std::in_place, storage};
    }

    ~SPSCQueue() = default; // nothing to free

    // non-copyable
    SPSCQueue(const SPSCQueue&) = delete;
    SPSCQueue& operator=(const SPSCQueue&) = delete;

    // movable
    SPSCQueue(SPSCQueue&&) noexcept = default;
    SPSCQueue& operator=(SPSCQueue&&) noexcept = default;

    /**
     * Returns `true` if the SPSC queue is empty.
     * Does not dequeue any messages (read-only operation).
     *
     * There is no guarantee that the queue is still empty after this function returns,
     * as the writer might have enqueued a message immediately after the check.
     */
    __attribute__((always_inline)) [[nodiscard]] inline bool is_empty() const noexcept
    {
        return storage_->read_ix_.load(std::memory_order::acquire) ==
               storage_->write_ix_.load(std::memory_order::acquire);
    }

    /**
     * Returns `false` if the SPSC queue is empty.
     * Does not dequeue any messages (read-only operation).
     *
     * To be used by consumer thread only due to memory order optimization.
     */
    __attribute__((always_inline)) [[nodiscard]] inline bool can_dequeue() const noexcept
    {
        return storage_->read_ix_.load(std::memory_order::relaxed) !=
               storage_->write_ix_.load(std::memory_order::acquire);
    }

    [[nodiscard]] inline size_t buffer_size() const noexcept
    {
        return buffer_size_;
    }

    [[nodiscard]] inline size_t max_message_size() const noexcept
    {
        return max_message_size_;
    }

    [[nodiscard]] inline size_t max_payload_size() const noexcept
    {
        return max_message_size_ - sizeof(uint64_t);
    }

    /**
     * Write the message to the queue.
     * The payload is copied onto the queue.
     */
    [[nodiscard]] bool enqueue(const Message& msg) noexcept
    {
        assert(msg.payload_size() > 0 && "Message payload size must be greater than 0 bytes");
        assert((msg.payload_size() <= max_payload_size()) && "Message payload too large");

        const size_t total_size = msg.total_size();

        size_t write_ix = storage_->write_ix_.load(std::memory_order::relaxed);
        size_t next_write_ix = next_index(write_ix, total_size);

        // check if we would cross the end of the buffer
        if (next_write_ix < buffer_size_) [[likely]]
        {
            // not crossing the end

            // check if we would cross the reader (on cache first, then update if needed)
            if ((write_ix < cached_read_ix_) && (next_write_ix >= cached_read_ix_))
            {
                cached_read_ix_ = storage_->read_ix_.load(std::memory_order::acquire);
                if ((write_ix < cached_read_ix_) && (next_write_ix >= cached_read_ix_))
                    return false; // queue is full
            }

            // copy header + payload
            std::memcpy(buffer_ + write_ix, &msg.size, sizeof(uint64_t));
            std::memcpy(buffer_ + write_ix + sizeof(uint64_t), msg.payload, msg.payload_size());

            // update writer head
            storage_->write_ix_.store(next_write_ix, std::memory_order::release);
        }
        else [[unlikely]]
        {
            // crossing the end

            // check if space for writing 8 byte sentinal value (size=0)
            const size_t sentinel_end = write_ix + sizeof(uint64_t);
            if (write_ix < cached_read_ix_ && sentinel_end >= cached_read_ix_)
            {
                cached_read_ix_ = storage_->read_ix_.load(std::memory_order::acquire);
                if (write_ix < cached_read_ix_ && sentinel_end >= cached_read_ix_)
                    return false; // reader inside sentinel
            }

            // check if space for payload (on cache first, then update if needed)
            next_write_ix = next_index(0, total_size);
            if (next_write_ix >= cached_read_ix_)
            {
                cached_read_ix_ = storage_->read_ix_.load(std::memory_order::acquire);
                if (next_write_ix >= cached_read_ix_)
                    return false; // queue full after wrap
            }

            // copy header + payload to start of buffer
            std::memcpy(buffer_, &msg.size, sizeof(uint64_t));
            std::memcpy(buffer_ + sizeof(uint64_t), msg.payload, msg.payload_size());

            // write 0 size sentinel at current write_ix to indicate wrap
            std::memset(buffer_ + write_ix, 0, sizeof(uint64_t));

            // update writer head
            storage_->write_ix_.store(next_write_ix, std::memory_order::release);
        }

        return true; // success
    }

    [[nodiscard]] MessageView dequeue_begin() noexcept
    {
        // on wrap-around, we need to recheck the read index.
        // use goto/jump instead of function call to avoid stack overhead.
    recheck_read_index:
        size_t read_ix = storage_->read_ix_.load(std::memory_order::relaxed);

        // check if queue is empty (on cache first, then update if needed)
        if (read_ix == cached_write_ix_)
        {
            cached_write_ix_ = storage_->write_ix_.load(std::memory_order::acquire);
            if (read_ix == cached_write_ix_)
                return {}; // queue is empty
        }

        // read message size
        size_t message_size;
        std::memcpy(&message_size, buffer_ + read_ix, sizeof(size_t));
        if (message_size == 0) [[unlikely]]
        {
            // message wraps around, move to beginning of queue
            storage_->read_ix_.store(0, std::memory_order::release);
            // recheck read index
            goto recheck_read_index;
        }

        // read message
        return MessageView(buffer_ + read_ix + sizeof(uint64_t), message_size, read_ix);
    }

    /**
     * Moves the reader index to the next message.
     */
    __attribute__((always_inline)) inline void dequeue_commit(const MessageView message) noexcept
    {
        const uint64_t next_read_ix = next_index(message.index, message.total_size());
        storage_->read_ix_.store(next_read_ix, std::memory_order::release);
    }

private:
    /**
     * Calculates where the next write index would be given
     * writing a message of size `size` at the current index.
     */
    __attribute__((always_inline)) [[nodiscard]] static inline size_t next_index(
        const size_t current_index, const size_t size
    ) noexcept
    {
        constexpr size_t align = 8;
        size_t ix = current_index + size;
        // round up to the next multiple of align
        return (ix + align - 1) & ~(align - 1);
    }

    /**
     * Calculates the maximum allowed message size given the overall storage buffer size.
     * 
     * At least two messages must fit in buffer, otherwise we can't
     * distinguish between empty and full, because the updated write index
     * will be equal to the read index (0) after the first message
     * has been written due to wrapping around, falsely indicating that
     * the queue is empty.
     */
    [[nodiscard]] static inline size_t calc_max_message_size(SPSCStorage* storage) noexcept
    {
        return storage->buffer_size() / 2;
    }

    // /**
    //  * Calculates the maximum allowed payload size given the overall storage buffer size.
    //  *
    //  * At least two messages must fit in buffer, otherwise we can't
    //  * distinguish between empty and full, because the updated write index
    //  * will be equal to the read index (0) after the first message
    //  * has been written due to wrapping around, falsely indicating that
    //  * the queue is empty.
    //  */
    // [[nodiscard]] static inline size_t calc_max_payload_size(SPSCStorage* storage) noexcept
    // {
    //     // max_payload = half the buffer - header - alignment‑reserve
    //     return (storage->buffer_size() / 2) - sizeof(uint64_t) - alignof(size_t);
    // }
};
} // namespace spsc
} // namespace queues
} // namespace posix_ipc
