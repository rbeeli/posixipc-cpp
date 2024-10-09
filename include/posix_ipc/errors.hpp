#pragma once

#include <cstdint>
#include <iostream>
#include <concepts>
#include <string>
#include <source_location>
#include <format>
#include <expected>

namespace posix_ipc
{
/**
 * Error codes for this POSIX IPC library.
 */
enum class PosixIpcErrorCode
{
    SUCCESS = 0,
    SHM_OPEN_FAILED,
    SHM_FSTAT_FAILED,
    SHM_TRUNCATE_FAILED,
    SHM_MMAP_FAILED,
    SHM_UNLINK_FAILED,
    THREAD_SET_AFFINITY_FAILED,
    THREAD_SET_NAME_FAILED,
    PUBSUB_INVALID_QUEUE_FULL_POLICY,
    PUBSUB_SHM_SIZE_MISMATCH
};

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
            case PosixIpcErrorCode::SHM_OPEN_FAILED:
                return "Failed to open shared memory (shm_open)";
            case PosixIpcErrorCode::SHM_FSTAT_FAILED:
                return "Failed to fstat shared memory (fstat)";
            case PosixIpcErrorCode::SHM_TRUNCATE_FAILED:
                return "Failed to truncate shared memory (ftruncate)";
            case PosixIpcErrorCode::SHM_MMAP_FAILED:
                return "Failed to map shared memory (mmap)";
            case PosixIpcErrorCode::SHM_UNLINK_FAILED:
                return "Failed to unlink shared memory (shm_unlink)";
            case PosixIpcErrorCode::THREAD_SET_AFFINITY_FAILED:
                return "Failed to set CPU affinity of thread";
            case PosixIpcErrorCode::THREAD_SET_NAME_FAILED:
                return "Failed to set name of thread";
            case PosixIpcErrorCode::PUBSUB_INVALID_QUEUE_FULL_POLICY:
                return "Invalid queue full policy (enum QueueFullPolicy)";
            case PosixIpcErrorCode::PUBSUB_SHM_SIZE_MISMATCH:
                return "The configured shared memory size does not match the actual size";
            default:
                return "Unknown error";
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
        const std::string& msg,
        const std::source_location& loc = std::source_location::current()
    ) noexcept
        : code(code), message(msg), location(loc)
    {
    }

    PosixIpcError(
        PosixIpcErrorCode code,
        std::string&& msg,
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

    // inline PosixIpcError extend_message(const std::string& msg) const
    // {
    //     return PosixIpcError(code, message + "\n" + msg, location);
    // }
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