#pragma once

#include <cstdint>
#include <string>
#include <format>
#include <expected>

#include "posix_ipc/errors.hpp"

namespace posix_ipc
{
namespace queues
{
namespace pubsub
{
enum class QueueFullPolicy : uint8_t
{
    DROP_NEWEST,
    // BLOCK,
};

[[nodiscard]] static std::expected<QueueFullPolicy, PosixIpcError> QueueFullPolicy_from_string(
    const std::string& s
) noexcept
{
    if (s == "DROP_NEWEST")
        return QueueFullPolicy::DROP_NEWEST;
    // else if (s == "BLOCK")
    //     return T::BLOCK;
    return std::unexpected{PosixIpcError(
        PosixIpcErrorCode::pubsub_invalid_queue_full_policy,
        std::format("Unknown queue full policy: {}", s)
    )};
}
} // namespace pubsub
} // namespace queues
} // namespace posix_ipc

static std::string to_string(posix_ipc::queues::pubsub::QueueFullPolicy s) noexcept
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
