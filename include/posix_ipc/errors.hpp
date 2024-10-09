#pragma once

#include <cstdint>
#include <iostream>
#include <concepts>
#include <string>
#include <source_location>
#include <format>
#include <expected>
#include <system_error>

namespace posix_ipc
{
/**
 * Error codes for this POSIX IPC library.
 */
enum class PosixIpcErrorCode
{
    success = 0,
    shm_open_failed,
    shm_fstat_failed,
    shm_truncate_failed,
    shm_mmap_failed,
    shm_unlink_failed,
    thread_set_affinity_failed,
    thread_set_name_failed,
    pubsub_invalid_queue_full_policy,
    pubsub_shm_size_mismatch
};

/**
 * Error category for POSIX IPC library error codes
 * based on `std::error_category`.
 */
class PosixIpcErrorCategory : public std::error_category
{
public:
    const char* name() const noexcept override
    {
        return "PosixIpcErrorCategory";
    }

    std::string message(int ev) const override
    {
        switch (static_cast<PosixIpcErrorCode>(ev))
        {
            case PosixIpcErrorCode::shm_open_failed:
                return "Failed to open shared memory (shm_open).";
            case PosixIpcErrorCode::shm_fstat_failed:
                return "Failed to fstat shared memory (fstat).";
            case PosixIpcErrorCode::shm_truncate_failed:
                return "Failed to truncate shared memory (ftruncate).";
            case PosixIpcErrorCode::shm_mmap_failed:
                return "Failed to map shared memory (mmap).";
            case PosixIpcErrorCode::shm_unlink_failed:
                return "Failed to unlink shared memory (shm_unlink).";
            case PosixIpcErrorCode::thread_set_affinity_failed:
                return "Failed to set CPU affinity of thread.";
            case PosixIpcErrorCode::thread_set_name_failed:
                return "Failed to set name of thread.";
            case PosixIpcErrorCode::pubsub_invalid_queue_full_policy:
                return "Invalid queue full policy (enum QueueFullPolicy).";
            case PosixIpcErrorCode::pubsub_shm_size_mismatch:
                return "The configured shared memory size does not match the actual size.";
            default:
                return "Unknown POSIX IPC error code.";
        }
    }
};

const PosixIpcErrorCategory ErrorCategory{};

inline std::error_code make_error_code(PosixIpcErrorCode e)
{
    return {static_cast<int>(e), ErrorCategory};
}

/**
 * POSIX IPC library error type used for error handling with `std::expected<V, E>`.
 */
struct PosixIpcError
{
    PosixIpcErrorCode code;
    std::string message;
    std::source_location location;

    PosixIpcError(
        PosixIpcErrorCode code,
        std::string msg,
        const std::source_location& loc = std::source_location::current()
    ) noexcept
        : code(code), message(std::move(msg)), location(loc)
    {
    }

    inline std::error_code to_error_code() const noexcept
    {
        return make_error_code(code);
    }

    inline std::string error_code_message() const
    {
        return ErrorCategory.message(static_cast<int>(code));
    }

    inline std::string full_message() const
    {
        return std::format(
            "POSIX IPC error {} at {}:{}:{} in {}: {}. Details: {}",
            static_cast<int16_t>(code),
            location.file_name(),
            location.line(),
            location.column(),
            location.function_name(),
            error_code_message(),
            message
        );
    }
};

// Concept to define exception-like types
template <typename E>
concept ExceptionLike = std::is_base_of_v<std::exception, E> && requires(const E& e) {
    { e.what() } -> std::convertible_to<const char*>;
};

// support << operator for easy printing
inline std::ostream& operator<<(std::ostream& os, const posix_ipc::PosixIpcError& error)
{
    os << error.full_message();
    return os;
}
} // namespace posix_ipc

// std::format {} support for `PosixIpcError`
template <>
struct std::formatter<posix_ipc::PosixIpcError>
{
    template <class ParseContext>
    constexpr auto parse(ParseContext& ctx)
    {
        return ctx.begin();
    }

    template <typename FormatContext>
    auto format(const posix_ipc::PosixIpcError& e, FormatContext& ctx)
    {
        return format_to(
            ctx.out(),
            "POSIX IPC error {} at {}:{}:{} in {}: {}. Details: {}",
            static_cast<int16_t>(e.code),
            e.location.file_name(),
            e.location.line(),
            e.location.column(),
            e.location.function_name(),
            e.error_code_message(),
            e.message
        );
    }
};

namespace std
{
template <>
struct is_error_code_enum<posix_ipc::PosixIpcErrorCode> : true_type
{
};
} // namespace std