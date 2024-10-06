#pragma once

#include <format>
#include <iostream>
#include <string>
#include <unordered_map>

#include "enums.hpp"

namespace posix_ipc
{
namespace queues
{
namespace pubsub
{
using std::string;

struct SubscriberConfig
{
    string shm_name;
    uint64_t capacity_bytes;
    QueueFullPolicy queue_full_policy = QueueFullPolicy::DROP_NEWEST;
    bool log_message_drop = true;

    // static SubscriberConfig from_cfg_file(const string path);

    const string to_string() const
    {
        return std::format(
            "SubscriberConfig(\n\tshm_name={}\n\tcapacity_bytes={}\n\tqueue_full_policy={}\n\t"
            "log_message_drop={}\n)",
            shm_name,
            capacity_bytes,
            ::to_string(queue_full_policy),
            log_message_drop
        );
    }
};

// SubscriberConfig SubscriberConfig::from_cfg_file(const string path)
// {
//     std::unordered_map<string, string> cfg_map = cfg::read_file(path);

//     string shm_name = cfg_map["shm_name"];
//     string capacity_str = cfg_map["capacity"];
//     uint64 capacity = units::size_str_to_bytes<uint64>(capacity_str);
//     auto queue_full_policy = QueueFullPolicy_from_string(cfg_map["queue_full_policy"]);
//     bool log_message_drop = strings::as<bool>(cfg_map["log_message_drop"]);

//     return {shm_name, capacity, queue_full_policy, log_message_drop};
// }
} // namespace pubsub
} // namespace queues
} // namespace posix_ipc

std::ostream& operator<<(std::ostream& os, const posix_ipc::queues::pubsub::SubscriberConfig& cfg)
{
    os << cfg.to_string();
    return os;
}
