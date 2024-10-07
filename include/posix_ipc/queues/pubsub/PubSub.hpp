#pragma once

#include <atomic>
#include <thread>
#include <format>
#include <optional>
#include <vector>
#include <unordered_map>

#include "posix_ipc/SharedMemory.hpp"
#include "posix_ipc/queues/Message.hpp"
#include "posix_ipc/queues/pubsub/enums.hpp"
#include "posix_ipc/queues/pubsub/Publisher.hpp"

namespace posix_ipc
{
namespace queues
{
namespace pubsub
{
using std::optional;
using std::vector;

struct PubSubChange
{
    optional<Publisher> addition;
    optional<string> removal;
};

class PubSub
{
private:
    vector<Publisher> publishers;
    vector<PubSubChange> change_queue;
    std::atomic<bool> applying_changes{false};
    std::atomic<bool> changes_enqueued{false};

public:
    PubSub() noexcept
    {
    }

    // non-copyable
    PubSub(const PubSub&) = delete;
    PubSub& operator=(const PubSub&) = delete;

    // movable
    PubSub(PubSub&& other) noexcept //
        : publishers(std::move(other.publishers))
    {
    }
    PubSub& operator=(PubSub&& other) noexcept
    {
        if (this != &other)
        {
            publishers = std::move(other.publishers);
        }
        return *this;
    }

    Publisher& get_publisher(const string& shm_name)
    {
        for (auto& pub : publishers)
        {
            if (pub.config().shm_name == shm_name)
                return pub;
        }
        throw std::runtime_error(
            std::format("Publisher for shared memory '{}' not found", shm_name)
        );
    }

    void apply_change(PubSubChange&& change)
    {
        if (change.addition)
        {
            // apply additions
            publishers.emplace_back(std::move(change.addition.value()));
        }
        else if (change.removal)
        {
            // apply removals
            auto it = std::find_if(
                publishers.begin(),
                publishers.end(),
                [&change](const auto& sub)
                { return sub.config().shm_name == change.removal.value(); }
            );
            if (it != publishers.end())
            {
                std::clog << std::format("Unsubscribing: {}", change.removal.value()) << std::endl;
                publishers.erase(it);
            }
        }
    }

    void apply_changes()
    {
        for (auto& change : change_queue)
        {
            apply_change(std::move(change));
        }
        change_queue.clear();
    }

    void enqueue_change(PubSubChange&& change)
    {
        change_queue.emplace_back(std::move(change));
    }

    void sync_configs(vector<PubSubConfig>& configs, bool first_call)
    {
        if (!first_call)
        {
            // wait for previous changes to be applied
            applying_changes.store(true, std::memory_order::release);
            while (changes_enqueued.load(std::memory_order::acquire))
            {
                // previous changes not yet applied
                std::this_thread::yield();
            }
        }

        // remove existing publishers not in config anymore
        for (auto& sub : publishers)
        {
            bool found = std::any_of(
                configs.begin(),
                configs.end(),
                [&sub](const auto& cfg) { return sub.config().shm_name == cfg.shm_name; }
            );
            if (!found)
            {
                PubSubChange change;
                change.removal = sub.config().shm_name;
                enqueue_change(std::move(change));
            }
        }

        // add new subscribers if not exist yet
        for (auto& cfg : configs)
        {
            bool exists = std::any_of(
                publishers.begin(),
                publishers.end(),
                [&cfg](const auto& sub) { return sub.config().shm_name == cfg.shm_name; }
            );
            if (!exists)
            {
                PubSubChange change;
                change.addition = std::move(Publisher::from_config(cfg));
                enqueue_change(std::move(change));
            }
        }

        if (!first_call)
        {
            // signal changes enqueued
            changes_enqueued.store(true, std::memory_order::release);
        }
        else
        {
            apply_changes();
        }
    }

    /**
     * Publishes a message to all subscribers.
     * Only one thread and the same thread is allowed to call this function.
     */
    bool publish(const Message& val) noexcept
    {
        // TODO: This is not thread-safe. Use a mutex or atomic flag to protect this function.
        
        bool reset_change_flags = false;
        if (applying_changes.load(std::memory_order::acquire))
        {
            while (!changes_enqueued.load(std::memory_order::acquire))
            {
                // wait for changes to be enqueued from other thread
                std::this_thread::yield();
            }

            apply_changes();

            reset_change_flags = true;
        }

        bool published = true;

        for (auto& publisher : publishers)
        {
            published &= publisher.publish(val); // drops messages if queue is full

            // bool success = sub->publish(val);
            // if (!success)
            // {
            //     auto& cfg = sub->config();

            //     // check for policy DROP_NEWEST
            //     assert(cfg.queue_full_policy == QueueFullPolicy::DROP_NEWEST && "Not implemented");
            //     ++sub->drop_count;

            //     // queue full - drop newest incoming messages
            //     if (cfg.log_message_drop)
            //     {
            //         auto drop_time = chrono::clock();
            //         if (drop_time - sub->last_drop_time > std::chrono::seconds(1))
            //         {
            //             std::cout << fmt::format(
            //                              "PubSub: drop msg for '{}', queue full. This is the {}-th "
            //                              "message dropped",
            //                              cfg.shm_name,
            //                              sub->drop_count
            //                          )
            //                       << "\n";
            //             sub->last_drop_time = drop_time;
            //         }
            //     }

            //     // consider unsubscribing clients as alternative to dropping messages
            // }
        }

        if (reset_change_flags)
        {
            changes_enqueued.store(false, std::memory_order::release);
            applying_changes.store(false, std::memory_order::release);
        }

        return published;
    }
};
} // namespace pubsub
} // namespace queues
} // namespace posix_ipc