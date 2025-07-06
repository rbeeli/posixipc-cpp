#pragma once

#include <cstdint>
#include <iostream>
#include <concepts>
#include <string>
#include <string_view>
#include <source_location>
#include <format>
#include <expected>
#include <system_error>

namespace posix_ipc
{
/**
 * Error codes for this POSIX IPC library.
 */
enum class PosixIpcErrorCode : int16_t
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
    pubsub_shm_size_mismatch,
    spsc_storage_error
};

static constexpr std::string_view to_string(PosixIpcErrorCode error) noexcept
{
    switch (error)
    {
        case PosixIpcErrorCode::shm_open_failed:
            return "shm_open_failed";
        case PosixIpcErrorCode::shm_fstat_failed:
            return "shm_fstat_failed";
        case PosixIpcErrorCode::shm_truncate_failed:
            return "shm_truncate_failed";
        case PosixIpcErrorCode::shm_mmap_failed:
            return "shm_mmap_failed";
        case PosixIpcErrorCode::shm_unlink_failed:
            return "shm_unlink_failed";
        case PosixIpcErrorCode::thread_set_affinity_failed:
            return "thread_set_affinity_failed";
        case PosixIpcErrorCode::thread_set_name_failed:
            return "thread_set_name_failed";
        case PosixIpcErrorCode::pubsub_invalid_queue_full_policy:
            return "pubsub_invalid_queue_full_policy";
        case PosixIpcErrorCode::pubsub_shm_size_mismatch:
            return "pubsub_shm_size_mismatch";
        case PosixIpcErrorCode::spsc_storage_error:
            return "spsc_storage_error";
        default:
            return "unknown";
    }
}

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
        return std::string(to_string(static_cast<PosixIpcErrorCode>(ev)));
    }
};

const PosixIpcErrorCategory error_category{};

inline std::error_code make_error_code(PosixIpcErrorCode e)
{
    return {static_cast<int>(e), error_category};
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
        return error_category.message(static_cast<int>(code));
    }

    inline std::string to_string() const
    {
        return std::format(
            "PosixIpcError {}: {} at {}:{}:{} in {}",
            posix_ipc::to_string(code),
            message,
            location.file_name(),
            location.line(),
            location.column(),
            location.function_name()
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
    os << error.to_string();
    return os;
}
} // namespace posix_ipc

namespace std
{
// Specialization of std::formatter for posix_ipc::PosixIpcError
template <>
struct formatter<posix_ipc::PosixIpcError>
{
    template <typename ParseContext>
    constexpr auto parse(ParseContext& ctx)
    {
        return ctx.begin();
    }

    template <typename FormatContext>
    auto format(const posix_ipc::PosixIpcError& e, FormatContext& ctx) const
    {
        return std::format_to(ctx.out(), "{}", e.to_string());
    }
};

// Specialization of std::formatter for posix_ipc::PosixIpcErrorCode
template <>
struct formatter<posix_ipc::PosixIpcErrorCode>
{
    template <typename ParseContext>
    constexpr auto parse(ParseContext& ctx)
    {
        return ctx.begin();
    }

    template <typename FormatContext>
    auto format(const posix_ipc::PosixIpcErrorCode& code, FormatContext& ctx) const
    {
        return std::format_to(ctx.out(), "{}", posix_ipc::to_string(code));
    }
};

// Specialization of std::is_error_code_enum for posix_ipc::PosixIpcErrorCode
template <>
struct is_error_code_enum<posix_ipc::PosixIpcErrorCode> : true_type
{
};
} // namespace std