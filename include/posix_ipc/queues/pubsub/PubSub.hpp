#pragma once

#include <format>
#include <optional>
#include <vector>
#include <expected>
#include <string>
// #include <mutex>

#include "posix_ipc/errors.hpp"
#include "posix_ipc/SharedMemory.hpp"
#include "posix_ipc/synchronization.hpp"
#include "posix_ipc/queues/Message.hpp"
#include "posix_ipc/queues/pubsub/enums.hpp"
#include "posix_ipc/queues/pubsub/Publisher.hpp"

namespace posix_ipc
{
namespace queues
{
namespace pubsub
{
// using lock_type = std::mutex;
// using lock_guard_type = std::lock_guard<std::mutex>;
using lock_type = SpinLock;
using lock_guard_type = SpinLockGuard;

struct PubSubChange
{
    std::optional<Publisher> addition;
    std::optional<std::string> removal;
};

class PubSub
{
private:
    std::vector<Publisher> publishers;
    std::vector<PubSubChange> change_queue;
    lock_type lock_obj;

public:
    PubSub() noexcept
    {
    }

    // non-copyable
    PubSub(const PubSub&) = delete;
    PubSub& operator=(const PubSub&) = delete;

    // movable
    PubSub(PubSub&& other) noexcept //
        : publishers(std::move(other.publishers)), change_queue(std::move(other.change_queue))
    {
    }
    PubSub& operator=(PubSub&& other) noexcept
    {
        if (this != &other)
        {
            publishers = std::move(other.publishers);
            change_queue = std::move(other.change_queue);
        }
        return *this;
    }

    [[nodiscard]] std::expected<void, PosixIpcError> sync_configs(std::vector<PubSubConfig>& configs)
    {
        lock_guard_type lock(lock_obj);

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
                change_queue.emplace_back(std::move(change));
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
                auto cfg_res = Publisher::from_config(cfg);
                if (!cfg_res.has_value())
                    return std::unexpected{cfg_res.error()};
                change.addition = std::move(cfg_res.value());
                change_queue.emplace_back(std::move(change));
            }
        }

        apply_changes();

        return {}; // success
    }

    /**
     * Publishes a message to all subscribers.
     * Only one thread and the same thread is allowed to call this function.
     */
    bool publish(const Message& val) noexcept
    {
        // TODO: Can we make this faster, e.g. by using a lock-free queue?
        lock_guard_type lock(lock_obj);

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

        return published;
    }

private:
    // expected<Publisher&, string> get_publisher(const string& shm_name)
    // {
    //     for (auto& pub : publishers)
    //     {
    //         if (pub.config().shm_name == shm_name)
    //             return pub;
    //     }
    //     return unexpected{
    //         std::format("Publisher for shared memory '{}' not found", shm_name)
    //     };
    // }

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
};
} // namespace pubsub
} // namespace queues
} // namespace posix_ipc