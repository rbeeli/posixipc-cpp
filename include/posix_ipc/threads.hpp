#pragma once

#include <pthread.h>
#include <cstring>
#include <thread>
#include <string_view>
#include <expected>
#include <format>

#include "posix_ipc/errors.hpp"

#ifndef THREAD_NAME_MAX
#define THREAD_NAME_MAX 15
#endif

namespace posix_ipc
{
namespace threads
{
/**
 * Pin the passed POSIX thread to a specific CPU.
 */
[[nodiscard]] std::expected<void, PosixIpcError> pin(pthread_t thread, int cpu) noexcept
{
    if (cpu < 0)
        return {}; // no pinning

    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(cpu, &cpuset);

    if (pthread_setaffinity_np(thread, sizeof(cpu_set_t), &cpuset) == -1)
    {
        auto msg = std::format("cpu={}, strerror={}, errno={}", cpu, std::strerror(errno), errno);
        return std::unexpected{PosixIpcError(PosixIpcErrorCode::thread_set_affinity_failed, msg)};
    }

    return {}; // success
}

/**
 * Pin the passed POSIX thread to a specific CPU.
 */
[[nodiscard]] std::expected<void, PosixIpcError> pin(std::jthread& thread, int cpu) noexcept
{
    return pin(thread.native_handle(), cpu);
}

/**
 * Pin the passed POSIX thread to a specific CPU.
 */
[[nodiscard]] std::expected<void, PosixIpcError> pin(std::thread& thread, int cpu) noexcept
{
    return pin(thread.native_handle(), cpu);
}

/**
 * Pin the current thread to a specific CPU.
 */
[[nodiscard]] std::expected<void, PosixIpcError> pin(int cpu)
{
    return pin(pthread_self(), cpu);
}

/**
 * Set the name of the passed POSIX thread.
 */
[[nodiscard]] std::expected<void, PosixIpcError> set_name(pthread_t thread, std::string_view name) noexcept
{
    if (name.size() > THREAD_NAME_MAX)
    {
        auto msg = std::format(
            "Thread name too long, max. {} chars allowed, got {}.", THREAD_NAME_MAX, name
        );
        return std::unexpected{PosixIpcError(PosixIpcErrorCode::thread_set_name_failed, msg)};
    }

    if (pthread_setname_np(thread, name.data()) == -1)
    {
        auto msg = std::format(
            "Failed to set thread name={}, strerror={}, errno={}", name, std::strerror(errno), errno
        );
        return std::unexpected{PosixIpcError(PosixIpcErrorCode::thread_set_name_failed, msg)};
    }

    return {}; // success
}

/**
 * Set the name of the passed POSIX std::jthread.
 */
[[nodiscard]] inline std::expected<void, PosixIpcError> set_name(
    std::jthread& thread, std::string_view name
) noexcept
{
    return set_name(thread.native_handle(), name.data());
}

/**
 * Set the name of the passed POSIX std::jthread.
 */
[[nodiscard]] inline std::expected<void, PosixIpcError> set_name(
    std::thread& thread, std::string_view name
) noexcept
{
    return set_name(thread.native_handle(), name.data());
}

/**
 * Set the name of the current POSIX thread.
 */
[[nodiscard]] inline std::expected<void, PosixIpcError> set_name(std::string_view name) noexcept
{
    return set_name(pthread_self(), name.data());
}
} // namespace threads
} // namespace posix_ipc
