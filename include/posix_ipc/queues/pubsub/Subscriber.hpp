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
using namespace std::chrono;
using namespace posix_ipc::queues::spsc;
using std::unique_ptr;
using std::expected;
using std::unexpected;

class Subscriber
{
private:
    PubSubConfig config_;
    unique_ptr<SharedMemory> shm_;
    unique_ptr<SPSCQueue> queue_; // resides in shared memory

    Subscriber(
        const PubSubConfig& config, unique_ptr<SharedMemory> shm, unique_ptr<SPSCQueue> queue
    ) noexcept
        : config_(config), shm_(std::move(shm)), queue_(std::move(queue))
    {
    }

public:
    time_point<high_resolution_clock> last_drop_time;
    size_t drop_count = 0;

    [[nodiscard]] static expected<Subscriber, PosixIpcError> from_config(
        const PubSubConfig& config
    ) noexcept
    {
        if (!SharedMemory::exists(config.shm_name))
        {
            auto msg = std::format(
                "Cannot create PubSub subscriber, shared memory [{}] does not exist.",
                config.shm_name
            );
            return unexpected{PosixIpcError(PosixIpcErrorCode::SHM_OPEN_FAILED, msg)};
        }

        // open shared memory
        auto shm_res = SharedMemory::open(config.shm_name);
        if (!shm_res.has_value())
            return unexpected{shm_res.error()};
        unique_ptr<SharedMemory> shm = std::make_unique<SharedMemory>(std::move(shm_res.value()));

        // check if size matches
        if (config.storage_size_bytes != shm->size())
        {
            auto msg = std::format(
                "Shared memory [{}] size mismatch, expected {}, got {}.",
                config.shm_name,
                config.storage_size_bytes,
                shm->size()
            );
            return unexpected{PosixIpcError(PosixIpcErrorCode::PUBSUB_SHM_SIZE_MISMATCH, msg)};
        }

        // initialize queue in shared memory
        SPSCStorage* storage = reinterpret_cast<SPSCStorage*>(shm->ptr());

        unique_ptr<SPSCQueue> queue = std::make_unique<SPSCQueue>(storage);

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

    // non-copyable
    Subscriber(const Subscriber&) = delete;
    Subscriber& operator=(const Subscriber&) = delete;

    // movable
    Subscriber(Subscriber&& other) noexcept
        : config_(other.config_), shm_(std::move(other.shm_)), queue_(std::move(other.queue_))
    {
        other.queue_ = nullptr;
    }
    Subscriber& operator=(Subscriber&& other) noexcept
    {
        if (this != &other)
        {
            shm_ = std::move(other.shm_);
            queue_ = std::move(other.queue_);
            config_ = other.config_;
            other.queue_ = nullptr;
        }
        return *this;
    }

    [[nodiscard]] inline const PubSubConfig& config() const noexcept
    {
        return config_;
    }

    // inline SPSCQueue& queue() const noexcept
    // {
    //     return *queue_;
    // }

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
};
} // namespace pubsub
} // namespace queues
} // namespace posix_ipc