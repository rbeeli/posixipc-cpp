#pragma once

#include <cstdint>
#include <string>
#include <format>

namespace posix_ipc
{
namespace queues
{
namespace pubsub
{
using std::string;

enum class QueueFullPolicy : uint8_t
{
    DROP_NEWEST,
    // BLOCK,
};

static QueueFullPolicy QueueFullPolicy_from_string(const string& s)
{
    if (s == "DROP_NEWEST")
        return QueueFullPolicy::DROP_NEWEST;
    // else if (s == "BLOCK")
    //     return T::BLOCK;
    else
        throw std::runtime_error(std::format("Unknown queue full policy: {}", s));
}
} // namespace pubsub
} // namespace queues
} // namespace posix_ipc

static std::string to_string(posix_ipc::queues::pubsub::QueueFullPolicy s)
{
    switch (s)
    {
        // case QueueFullPolicy::BLOCK: return "BLOCK";
        case posix_ipc::queues::pubsub::QueueFullPolicy::DROP_NEWEST:
            return "DROP_NEWEST";
        default:
            return "UNKNOWN";
    }
}
