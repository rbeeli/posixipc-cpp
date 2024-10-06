#pragma once

#include <atomic>
#include <chrono>
#include <memory>

#include "posix_ipc/SharedMemory.hpp"
#include "posix_ipc/queues/Message.hpp"
#include "posix_ipc/queues/pubsub/SubscriberConfig.hpp"
#include "posix_ipc/queues/spsc/SPSCQueue.hpp"

namespace posix_ipc
{
namespace queues
{
namespace pubsub
{
using std::unique_ptr;
using namespace posix_ipc::queues::spsc;

class Subscriber
{
private:
    unique_ptr<SharedMemory> shm_;
    SPSCQueue* queue_; // resides in shared memory
    SubscriberConfig config_;

public:
    std::chrono::time_point<std::chrono::high_resolution_clock> last_drop_time;
    size_t drop_count = 0;

    Subscriber(const SubscriberConfig& config)
        : config_(config)
    {
        uint64_t storage_size = config.capacity_bytes;

        // shared memory (try to open, if not exists or size mismatch, create new one)
        bool recreate = false;
        if (SharedMemory::exists(config.shm_name))
        {
            shm_ = make_unique<SharedMemory>(config.shm_name, false, storage_size);

            // check if size matches
            if (storage_size != shm_->size())
            {
                recreate = true;
            }
        }
        else
        {
            // flag to create new shared memory if it does not exist
            recreate = true;
        }

        // recreate shared memory if necessary
        if (recreate)
            shm_ = make_unique<SharedMemory>(config.shm_name, true, storage_size);

        // initialize queue in shared memory
        SPSCStorage* storage;
        if (recreate)
        {
            // calls constructor
            storage = new (shm_->ptr()) SPSCStorage(storage_size);
        }
        else
        {
            // already exists, just get the pointer
            storage = reinterpret_cast<SPSCStorage*>(shm_->ptr());
        }

        queue_ = new SPSCQueue(storage);

        // // shared flag for mutex
        // pthread_mutex_t *native = static_cast<pthread_mutex_t*>(storage->mutex.native_handle());
        // pthread_mutexattr_t attr;
        // pthread_mutexattr_init(&attr);
        // pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
        // pthread_mutex_init(native, &attr);
    }

    ~Subscriber()
    {
        if (queue_ != nullptr)
        {
            delete queue_;
            queue_ = nullptr;
        }
    }

    // non-copyable
    Subscriber(const Subscriber&) = delete;
    Subscriber& operator=(const Subscriber&) = delete;

    // movable
    Subscriber(Subscriber&& other) noexcept
        : shm_(std::move(other.shm_)),
          queue_(std::move(other.queue_)),
          config_(other.config_)
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

    inline const SubscriberConfig& config() const noexcept
    {
        return config_;
    }

    inline SPSCQueue& queue() const noexcept
    {
        return *queue_;
    }

    inline bool publish(const Message& val) noexcept
    {
        return queue_->enqueue(val);
    }
};
} // namespace pubsub
} // namespace queues
} // namespace posix_ipc