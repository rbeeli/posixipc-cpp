#pragma once

#include <format>
#include <iostream>
#include <string>

#include "enums.hpp"

namespace posix_ipc
{
namespace queues
{
namespace pubsub
{
using std::string;

struct PubSubConfig
{
    string shm_name;
    uint64_t storage_size_bytes;
    QueueFullPolicy queue_full_policy = QueueFullPolicy::DROP_NEWEST;
    bool log_message_drop = true;

    // static PubSubConfig from_cfg_file(const string path);

    const string to_string() const
    {
        return std::format(
            "PubSubConfig(\n\tshm_name={}\n\tstorage_size_bytes={}\n\tqueue_full_policy={}\n\t"
            "log_message_drop={}\n)",
            shm_name,
            storage_size_bytes,
            ::to_string(queue_full_policy),
            log_message_drop
        );
    }
};
} // namespace pubsub
} // namespace queues
} // namespace posix_ipc

std::ostream& operator<<(std::ostream& os, const posix_ipc::queues::pubsub::PubSubConfig& cfg)
{
    os << cfg.to_string();
    return os;
}
