#pragma once

#include <atomic>
#include <cstdint>
#include <iostream>
#include <format>
#include <cassert>

#include "posix_ipc/queues/Message.hpp"
#include "posix_ipc/queues/spsc/SPSCStorage.hpp"

static_assert(std::atomic<size_t>::is_always_lock_free, "Atomic of size_t is not lock-free");
static_assert(sizeof(void*) == 8, "Only supporting 64 bit builds");

namespace posix_ipc
{
namespace queues
{
namespace spsc
{
/// @brief A lock-free, single-producer, single-consumer queue.
/// @details
/// This queue is a circular buffer with a fixed capacity.
/// It supports variable sized messages.
/// It is a single-producer, single-consumer queue, meaning that one thread
/// can push to the queue and one thread can pop from the queue
/// without any synchronization.
///
/// A message consists of a header and a payload. The header contains
/// the size of the payload as size_t (8 bytes on 64 bit systems).
/// The payload contains the actual data.
/// Messages start addresses are guaranteed to be 8 bytes aligned,
/// and the underlying storage buffer size must be divisible by 8 bytes.
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
/// At the current write index, a header with sentinel size value of 0 is written
/// to indicate that the message wraps around.
/// 
/// The storage has the following fixed, contiguous memory layout:
/// 
/// 0   	magic   		(uint32_t)
/// 4   	abi_ver 		(uint32_t)
/// 8   	storage_size 	(size_t)
/// 16-63 	pad
/// 64  	read_ix   		(atomic<size_t>)
/// 128 	write_ix  		(atomic<size_t>)
/// 192 	msg_count 		(atomic<size_t>)
/// 256 	buffer
///
/// Pad is used to align the buffer to 64 bytes (cache line size).
///
/// This memory layout ensure cache line alignment for the read and write indices,
/// and can be used inside shared memory, e.g. as IPC mechanism to other languages (processes).
class SPSCQueue final
{
private:
    alignas(SPSC_CACHE_LINE_SIZE) SPSCStorage* storage_;

    const size_t max_message_size_; // max size single message in bytes (including header bytes)
    const size_t buffer_size_;
    std::byte* buffer_;

public:
    SPSCQueue(SPSCStorage* storage) noexcept
        : storage_(storage),
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
                    "SPSCStorage buffer size must be divisible by 8, got value {}.",
                    storage->buffer_size()
                ),
                loc
            )};
        }

        // ensure can store at least 1 byte
        const size_t min_required = next_index(0, sizeof(size_t) + 8);
        if (storage->buffer_size() < min_required)
        {
            return std::unexpected{
                PosixIpcError(PosixIpcErrorCode::spsc_storage_error, "SPSCQueue buffer too small", loc)
            };
        }

        return std::expected<SPSCQueue, PosixIpcError>{std::in_place, storage};
    }

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
     * There is no guarantee that the value is still up-to-date after this function returns,
     * because reader/writer might perform an action in parallel.
     */
    __attribute__((always_inline)) [[nodiscard]] inline bool is_empty() const noexcept
    {
        return storage_->read_ix_.load(std::memory_order::acquire) ==
               storage_->write_ix_.load(std::memory_order::acquire);
    }

    /**
     * Returns the number of messages currently in the queue.
     * 
     * The returned value indicates the following:
     * - Called from reader thread: at least this many messages in the queue
     * - Called from writer thread: at most this many messages in the queue
     *
     * There is no guarantee that the value is still up-to-date after this function returns,
     * because the peer thread can perform an action in parallel.
     */
    [[nodiscard]] size_t size() const noexcept
    {
        return storage_->msg_count_.load(std::memory_order::acquire);
    }

    /**
     * Returns `true` if the SPSC queue is not empty, hence a message can be dequeued.
     * Does not dequeue any messages (read-only operation).
     *
     * To be used by CONSUMER ONLY due to memory order optimization.
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
        return max_message_size_ - sizeof(size_t);
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

        size_t read_ix = storage_->read_ix_.load(std::memory_order::acquire);
        size_t write_ix = storage_->write_ix_.load(std::memory_order::relaxed);
        size_t next_write_ix = next_index(write_ix, total_size);

        // check if we would cross the end of the buffer
        if (next_write_ix < buffer_size_) [[likely]]
        {
            // not crossing the end

            // check if we would cross the reader
            if ((write_ix < read_ix) && (next_write_ix >= read_ix))
                return false; // queue is full

            // copy payload + header
            std::memcpy(buffer_ + write_ix + sizeof(size_t), msg.payload, msg.payload_size());
            std::memcpy(buffer_ + write_ix, &msg.size, sizeof(size_t));
        }
        else [[unlikely]]
        {
            // crossing the end

            // check if space for writing 8 byte sentinal value (size=0)
            const size_t sentinel_end = write_ix + sizeof(size_t);
            if (write_ix < read_ix && sentinel_end >= read_ix)
                return false; // reader inside sentinel

            // check if space for payload (on cache first, then update if needed)
            next_write_ix = next_index(0, total_size);
            if (next_write_ix >= read_ix)
                return false; // queue full after wrap

            // copy payload + header to start of buffer
            std::memcpy(buffer_ + sizeof(size_t), msg.payload, msg.payload_size());
            std::memcpy(buffer_, &msg.size, sizeof(size_t));

            // write 0 size sentinel at current write_ix to indicate wrap
            std::memset(buffer_ + write_ix, 0, sizeof(size_t));
        }

        // update writer head
        storage_->write_ix_.store(next_write_ix, std::memory_order::release);

        // increment message counter
        storage_->msg_count_.fetch_add(1, std::memory_order::release);

        return true; // success
    }

    [[nodiscard]] MessageView dequeue_begin() noexcept
    {
        // on wrap-around, we need to recheck the read index.
        // use goto/jump instead of function call to avoid stack overhead.
    recheck_read_index:
        size_t read_ix = storage_->read_ix_.load(std::memory_order::relaxed);
        size_t write_ix = storage_->write_ix_.load(std::memory_order::acquire);

        // check if queue is empty
        if (read_ix == write_ix)
            return {}; // queue is empty

        // read message size
        size_t message_size;
        std::memcpy(&message_size, buffer_ + read_ix, sizeof(size_t));
        if (message_size == 0) [[unlikely]] // wrap-around sentinel
        {
            // message wraps around, move to beginning of queue
            storage_->read_ix_.store(0, std::memory_order::release);
            goto recheck_read_index;
        }

        // read message
        return MessageView(buffer_ + read_ix + sizeof(size_t), message_size, read_ix);
    }

    /**
     * Moves the reader index to the next message.
     * Call this after processing a message returned by `dequeue_begin`.
     * The `MessageView` object is no longer valid after this call.
     */
    __attribute__((always_inline)) inline void dequeue_commit(const MessageView message) noexcept
    {
        // advance reader index
        const size_t next_read_ix = next_index(message.index, message.total_size());
        storage_->read_ix_.store(next_read_ix, std::memory_order::release);

        // decrement message counter
        storage_->msg_count_.fetch_sub(1, std::memory_order::release);
    }

private:
    /**
     * Calculates where the next write index would be given
     * writing a message of size `size` at the current index.
     * 
     * The index is aligned to 8 bytes.
     * If the requested size is not divisible by 8, the next
     * 8 bytes divisible index is used.
     */
    __attribute__((always_inline)) [[nodiscard]] static inline size_t next_index(
        const size_t current_index, const size_t size
    ) noexcept
    {
        constexpr size_t align = 8;
        size_t ix = current_index + size;
        // round up to the next multiple of align (8 bytes)
        return (ix + align - 1) & ~(align - 1);
    }

    /**
     * Calculates the maximum allowed message size given the overall storage buffer size.
     * Message size here includes the size header value.
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
};
} // namespace spsc
} // namespace queues
} // namespace posix_ipc
