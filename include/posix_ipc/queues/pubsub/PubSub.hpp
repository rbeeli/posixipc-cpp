#pragma once

#include <thread>
#include <format>
#include <optional>
#include <vector>
#include <unordered_map>

#include "posix_ipc/synchro.hpp"
#include "posix_ipc/SharedMemory.hpp"
#include "posix_ipc/queues/Message.hpp"
#include "posix_ipc/queues/pubsub/enums.hpp"
#include "posix_ipc/queues/pubsub/Subscriber.hpp"

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
    optional<Subscriber> addition;
    optional<string> removal;
};

class PubSub
{
private:
    vector<Subscriber> subscribers;
    posix_ipc::IdSpinLock modify_lock;

public:
    PubSub() noexcept
    {
    }

    // non-copyable
    PubSub(const PubSub&) = delete;
    PubSub& operator=(const PubSub&) = delete;

    // movable
    PubSub(PubSub&& other) noexcept
        : subscribers(std::move(other.subscribers))
    {
    }
    PubSub& operator=(PubSub&& other) noexcept
    {
        if (this != &other)
        {
            subscribers = std::move(other.subscribers);
        }
        return *this;
    }

    Subscriber& get_subscriber(string& shm_name)
    {
        auto it = std::find_if(
            subscribers.begin(),
            subscribers.end(),
            [&shm_name](const auto& sub) { return sub.config().shm_name == shm_name; }
        );
        if (it != subscribers.end())
            return *it;
        throw std::runtime_error(std::format("Subscriber '{}' not found", shm_name));
    }

    void subscribe(const SubscriberConfig cfg)
    {
        posix_ipc::IdSpinLockGuard guard{modify_lock, 1};

        // LOG_INFO(logger, "Subscribing: {}", cfg.to_string());

        PubSubChange change;

        // create new subscriber
        change.addition = std::move(Subscriber(cfg));

        apply_change(std::move(change));
    }

    void unsubscribe(const string& shm_name)
    {
        posix_ipc::IdSpinLockGuard guard{modify_lock, 2};

        PubSubChange change;

        // remove subscriber
        change.removal = shm_name;

        apply_change(std::move(change));
    }

    void sync_with_subscriber_configs(vector<SubscriberConfig>& configs)
    {
        posix_ipc::IdSpinLockGuard guard{modify_lock, 3};

        // remove existing subscribers not in config anymore
        for (auto& sub : subscribers)
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
                apply_change(std::move(change));
            }
        }

        // add new subscribers if not exist yet
        for (auto& cfg : configs)
        {
            bool exists = std::any_of(
                subscribers.begin(),
                subscribers.end(),
                [&cfg](const auto& sub) { return sub.config().shm_name == cfg.shm_name; }
            );
            if (!exists)
            {
                PubSubChange change;
                change.addition = std::move(Subscriber(cfg));
                apply_change(std::move(change));
            }
        }
    }

    void apply_change(PubSubChange&& change)
    {
        if (change.addition)
        {
            // apply additions
            subscribers.emplace_back(std::move(change.addition.value()));
        }
        else if (change.removal)
        {
            // apply removals
            auto it = std::find_if(
                subscribers.begin(),
                subscribers.end(),
                [&change](const auto& sub)
                { return sub.config().shm_name == change.removal.value(); }
            );
            if (it != subscribers.end())
            {
                // LOG_INFO(logger, "Unsubscribing: {}", change.removal.value());
                subscribers.erase(it);
            }
        }
    }

    bool publish(const Message& val) noexcept
    {
        IdSpinLockGuard guard{modify_lock, 4};

        bool published = true;

        for (auto& sub : subscribers)
        {
            published &= sub.publish(val); // drops messages if queue is full

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
};
} // namespace pubsub
} // namespace queues
} // namespace posix_ipc