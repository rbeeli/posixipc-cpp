#pragma once

#include <atomic>
#include <cstdint>
#include <iostream>
#include <format>
#include <cassert>

#include "posix_ipc/queues/Message.hpp"
#include "posix_ipc/queues/spsc/SPSCStorage.hpp"

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
/// the size of the payload as size_t (8 bytes on 64 bit systems). The payload
/// contains the actual data.
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
    alignas(SPSCStorage::CACHE_LINE_SIZE) size_t next_read_ix_ = 0;
    alignas(SPSCStorage::CACHE_LINE_SIZE) size_t cached_read_ix;
    alignas(SPSCStorage::CACHE_LINE_SIZE) size_t cached_write_ix;

    const size_t max_message_size_; // max size of a single message in bytes
    const size_t buffer_size_;
    byte* buffer_;

public:
    SPSCQueue(SPSCStorage* storage) noexcept
        : storage_(storage),
          cached_read_ix(storage->read_ix.load(std::memory_order::acquire)),
          cached_write_ix(storage->write_ix.load(std::memory_order::acquire)),
          // at least two messages must fit in buffer, otherwise we can't
          // distinguish between empty and full, because the updated write index
          // will be equal to the read index (0) after the first message
          // has been written due to wrapping around, falsely indicating that
          // the queue is empty.
          max_message_size_(storage->buffer_size() / 2),
          buffer_size_(storage->buffer_size()),
          buffer_(storage->buffer())
    {
        // ensure can store at least 1 byte
        assert(
            storage->buffer_size() > next_index(0, sizeof(uint64_t) + 1) && "Queue buffer too small"
        );
    }

    ~SPSCQueue()
    {
        // NOTE: destructor not needed since memory is allocated in shared memory for whole object
        // delete[] buffer_;
    }

    // non-copyable & non-movable
    SPSCQueue(const SPSCQueue&) = delete;
    SPSCQueue& operator=(const SPSCQueue&) = delete;
    SPSCQueue(SPSCQueue&&) = delete;
    SPSCQueue& operator=(SPSCQueue&&) = delete;

    /**
     * Returns `true` if the SPSC queue is empty.
     * Does not dequeue any messages (read-only operation).
     *
     * There is no guarantee that the queue is still empty after this function returns,
     * as the writer might have enqueued a message immediately after the check.
     */
    __attribute__((always_inline)) inline bool is_empty() const noexcept
    {
        return storage_->read_ix.load(std::memory_order::acquire) ==
               storage_->write_ix.load(std::memory_order::acquire);
    }

    /**
     * Returns `false` if the SPSC queue is empty.
     * Does not dequeue any messages (read-only operation).
     *
     * To be used by consumer thread only due to memory order optimization.
     */
    __attribute__((always_inline)) inline bool can_dequeue() const noexcept
    {
        return storage_->read_ix.load(std::memory_order::relaxed) !=
               storage_->write_ix.load(std::memory_order::acquire);
    }

    inline size_t buffer_size() const noexcept
    {
        return buffer_size_;
    }

    inline size_t max_message_size() const noexcept
    {
        return max_message_size_;
    }

    /**
     * Write the message to the queue.
     * The payload is copied onto the queue.
     */
    bool enqueue(const Message& val) noexcept
    {
        assert(val.payload_size() > 0 && "Message size must be > 0");
        assert(
            (val.payload_size() <= max_message_size_ - sizeof(uint64_t)) &&
            "Message payload too large"
        );

        const size_t total_size = val.total_size();
        size_t write_ix = storage_->write_ix.load(std::memory_order::relaxed);
        size_t next_write_ix = next_index(write_ix, total_size);

        // check if we would cross the end of the buffer
        if (next_write_ix < buffer_size_) [[likely]]
        {
            // not crossing the end

            // check if we would cross the reader (on cache first, then update if needed)
            if ((write_ix < cached_read_ix) && (next_write_ix >= cached_read_ix))
            {
                cached_read_ix = storage_->read_ix.load(std::memory_order::acquire);
                if ((write_ix < cached_read_ix) && (next_write_ix >= cached_read_ix))
                    return false; // queue is full
            }
        }
        else [[unlikely]]
        {
            // crossing the end -> wrap around

            // check if we would cross the reader (on cache first, then update if needed)
            next_write_ix = next_index(0, total_size);
            if (next_write_ix >= cached_read_ix)
            {
                cached_read_ix = storage_->read_ix.load(std::memory_order::acquire);
                if (next_write_ix >= cached_read_ix)
                    return false; // queue is full
            }

            // write 0 size at current write_ix to indicate wrap around
            std::memset(buffer_ + write_ix, 0, sizeof(uint64_t));

            write_ix = 0;
        }

        // write to buffer
        std::memcpy(buffer_ + write_ix, &val.size, sizeof(uint64_t));
        std::memcpy(buffer_ + write_ix + sizeof(uint64_t), val.payload, val.payload_size());

        // update write index
        storage_->write_ix.store(next_write_ix, std::memory_order::release);

        return true; // success
    }

    MessageView dequeue_begin() noexcept
    {
        // on wrap-around, we need to recheck the read index.
        // use goto/jump instead of function call to avoid stack overhead.
    recheck_read_index:
        size_t read_ix = storage_->read_ix.load(std::memory_order::relaxed);

        // check if queue is empty (on cache first, then update if needed)
        if (read_ix == cached_write_ix)
        {
            cached_write_ix = storage_->write_ix.load(std::memory_order::acquire);
            if (read_ix == cached_write_ix)
                return {}; // queue is empty
        }

        // read message size
        size_t message_size = reinterpret_cast<size_t*>(buffer_ + read_ix)[0];
        if (message_size == 0) [[unlikely]]
        {
            // message wraps around, move to beginning of queue
            storage_->read_ix.store(0, std::memory_order::release);
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
        storage_->read_ix.store(next_read_ix, std::memory_order::release);
    }

private:
    /**
     * Calculates where the next write index would be given
     * writing a message of size `size` at the current index.
     */
    __attribute__((always_inline)) static inline size_t next_index(const size_t current_index, const size_t size) noexcept
    {
        constexpr auto align = sizeof(size_t);
        size_t ix = current_index + size;
        // round up to the next multiple of align
        return (ix + align - 1) & ~(align - 1);
    }
};
} // namespace spsc
} // namespace queues
} // namespace posix_ipc
