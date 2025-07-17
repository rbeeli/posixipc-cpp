#pragma once

#include <atomic>
#include <chrono>
#include <format>
#include <iostream>
#include <memory>
#include <expected>

#include "posix_ipc/errors.hpp"
#include "posix_ipc/SharedMemory.hpp"
#include "posix_ipc/queues/Message.hpp"
#include "posix_ipc/queues/pubsub/PubSubConfig.hpp"
#include "posix_ipc/queues/spsc/SPSCQueue.hpp"

namespace posix_ipc
{
namespace queues
{
namespace pubsub
{
using namespace posix_ipc::queues::spsc;

class Subscriber
{
private:
    PubSubConfig config_;
    std::unique_ptr<SharedMemory> shm_;
    std::unique_ptr<SPSCQueue> queue_; // resides in shared memory

    Subscriber(
        const PubSubConfig& config,
        std::unique_ptr<SharedMemory> shm,
        std::unique_ptr<SPSCQueue> queue
    ) noexcept
        : config_(config), shm_(std::move(shm)), queue_(std::move(queue))
    {
    }

public:
    std::chrono::time_point<std::chrono::high_resolution_clock> last_drop_time;
    size_t drop_count = 0;

    // non-copyable
    Subscriber(const Subscriber&) = delete;
    Subscriber& operator=(const Subscriber&) = delete;

    // movable
    Subscriber(Subscriber&& other) noexcept = default;
    Subscriber& operator=(Subscriber&& other) = default;

    [[nodiscard]] inline const PubSubConfig& config() const noexcept
    {
        return config_;
    }

    __attribute__((always_inline)) inline void dequeue_commit(const MessageView message) noexcept
    {
        queue_->dequeue_commit(message);
    }

    [[nodiscard]] __attribute__((always_inline)) inline MessageView dequeue_begin() noexcept
    {
        return queue_->dequeue_begin();
    }

    [[nodiscard]] __attribute__((always_inline)) inline bool is_empty() const noexcept
    {
        return queue_->is_empty();
    }

    [[nodiscard]] __attribute__((always_inline)) inline bool can_dequeue() const noexcept
    {
        return queue_->can_dequeue();
    }

    [[nodiscard]] __attribute__((always_inline)) inline size_t size() const noexcept
    {
        return queue_->size();
    }

    [[nodiscard]] static std::expected<Subscriber, PosixIpcError> from_config(
        const PubSubConfig& config
    ) noexcept
    {
        if (!SharedMemory::exists(config.shm_name))
        {
            auto msg = std::format(
                "Cannot create PubSub subscriber, shared memory [{}] does not exist.",
                config.shm_name
            );
            return std::unexpected{PosixIpcError(PosixIpcErrorCode::shm_open_failed, msg)};
        }

        // open shared memory
        auto shm_res = SharedMemory::open(config.shm_name);
        if (!shm_res.has_value())
            return std::unexpected{shm_res.error()};
        std::unique_ptr<SharedMemory> shm = std::make_unique<SharedMemory>(
            std::move(shm_res.value())
        );

        // check if size matches
        if (config.storage_size_bytes != shm->size())
        {
            auto msg = std::format(
                "Shared memory [{}] size mismatch, expected {}, got {}.",
                config.shm_name,
                config.storage_size_bytes,
                shm->size()
            );
            return std::unexpected{PosixIpcError(PosixIpcErrorCode::pubsub_shm_size_mismatch, msg)};
        }

        // initialize queue in shared memory
        auto storage = SPSCStorage::map(shm->ptr(), shm->size());
        if (!storage)
            return std::unexpected{storage.error()};

        auto queue = std::make_unique<SPSCQueue>(storage.value());

        // // shared flag for mutex
        // pthread_mutex_t *native = static_cast<pthread_mutex_t*>(storage->mutex.native_handle());
        // pthread_mutexattr_t attr;
        // pthread_mutexattr_init(&attr);
        // pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
        // pthread_mutex_init(native, &attr);

        std::clog << std::format(
                         "Subscriber created using shared memory [{}] with size {} bytes.",
                         config.shm_name,
                         shm->size()
                     )
                  << std::endl;

        return Subscriber(config, std::move(shm), std::move(queue));
    }
};
} // namespace pubsub
} // namespace queues
} // namespace posix_ipc
