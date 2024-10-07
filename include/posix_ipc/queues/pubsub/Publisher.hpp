#pragma once

#include <atomic>
#include <chrono>
#include <format>
#include <iostream>
#include <memory>

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

class Publisher
{
private:
    PubSubConfig config_;
    unique_ptr<SharedMemory> shm_;
    unique_ptr<SPSCQueue> queue_; // resides in shared memory

public:
    time_point<high_resolution_clock> last_drop_time;
    size_t drop_count = 0;

    Publisher(
        const PubSubConfig& config, unique_ptr<SharedMemory> shm, unique_ptr<SPSCQueue> queue
    ) noexcept
        : config_(config), shm_(std::move(shm)), queue_(std::move(queue))
    {
    }

    static Publisher from_config(const PubSubConfig& config)
    {
        unique_ptr<SharedMemory> shm = nullptr;

        // shared memory (try to open, if not exists or size mismatch, create new one)
        bool recreate = false;
        if (SharedMemory::exists(config.shm_name))
        {
            shm = std::make_unique<SharedMemory>(config.shm_name);

            // check if size matches
            if (config.storage_size_bytes != shm->size())
            {
                std::clog << std::format(
                                 "Shared memory [{}] size mismatch, expected {} bytes, got {} "
                                 "bytes, recreating...",
                                 config.shm_name,
                                 config.storage_size_bytes,
                                 shm->size()
                             )
                          << std::endl;
                recreate = true;
            }
        }
        else
        {
            // flag to create new shared memory if it does not exist
            recreate = true;
            std::clog << std::format(
                             "Shared memory [{}] does not exist, creating...", config.shm_name
                         )
                      << std::endl;
        }

        // recreate shared memory if necessary
        if (recreate)
            shm = std::make_unique<SharedMemory>(config.shm_name, true, config.storage_size_bytes);

        // initialize queue in shared memory
        SPSCStorage* storage;
        if (recreate)
        {
            // calls constructor
            storage = new (shm->ptr()) SPSCStorage(shm->size());
        }
        else
        {
            // already exists, just get the pointer
            storage = reinterpret_cast<SPSCStorage*>(shm->ptr());
        }

        unique_ptr<SPSCQueue> queue = std::make_unique<SPSCQueue>(storage);

        // // shared flag for mutex
        // pthread_mutex_t *native = static_cast<pthread_mutex_t*>(storage->mutex.native_handle());
        // pthread_mutexattr_t attr;
        // pthread_mutexattr_init(&attr);
        // pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
        // pthread_mutex_init(native, &attr);

        std::clog << std::format(
                         "Publisher created in shared memory [{}] with size {} bytes",
                         config.shm_name,
                         shm->size()
                     )
                  << std::endl;

        return Publisher(config, std::move(shm), std::move(queue));
    }

    // non-copyable
    Publisher(const Publisher&) = delete;
    Publisher& operator=(const Publisher&) = delete;

    // movable
    Publisher(Publisher&& other) noexcept
        : config_(other.config_), shm_(std::move(other.shm_)), queue_(std::move(other.queue_))
    {
        other.queue_ = nullptr;
    }
    Publisher& operator=(Publisher&& other) noexcept
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

    inline const PubSubConfig& config() const noexcept
    {
        return config_;
    }

    __attribute__((always_inline)) inline bool is_empty() const noexcept
    {
        return queue_->is_empty();
    }

    __attribute__((always_inline)) inline bool publish(const Message& val) noexcept
    {
        return queue_->enqueue(val);
    }
};
} // namespace pubsub
} // namespace queues
} // namespace posix_ipc