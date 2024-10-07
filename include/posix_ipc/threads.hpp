#pragma once

#include <pthread.h>
#include <cstring>
#include <thread>
#include <string>
#include <string_view>
#include <expected>
#include <format>

#ifndef THREAD_NAME_MAX
#define THREAD_NAME_MAX 15
#endif

namespace posix_ipc
{
namespace threads
{
using std::string;
using std::string_view;
using std::expected;
using std::unexpected;

/**
 * Pin the passed POSIX thread to a specific CPU.
 */
[[nodiscard]] expected<void, string> pin(pthread_t thread, int cpu) noexcept
{
    if (cpu < 0)
        return {}; // no pinning

    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(cpu, &cpuset);

    if (pthread_setaffinity_np(thread, sizeof(cpu_set_t), &cpuset) == -1)
    {
        auto msg = std::format("Failed to pin thread to CPU {}: {}", cpu, std::strerror(errno));
        return unexpected{msg};
    }

    return {}; // success
}

/**
 * Pin the passed POSIX thread to a specific CPU.
 */
[[nodiscard]] expected<void, string> pin(std::jthread& thread, int cpu) noexcept
{
    return pin(thread.native_handle(), cpu);
}

/**
 * Pin the passed POSIX thread to a specific CPU.
 */
[[nodiscard]] expected<void, string> pin(std::thread& thread, int cpu) noexcept
{
    return pin(thread.native_handle(), cpu);
}

/**
 * Pin the current thread to a specific CPU.
 */
[[nodiscard]] expected<void, string> pin(int cpu)
{
    return pin(pthread_self(), cpu);
}

/**
 * Set the name of the passed POSIX thread.
 */
[[nodiscard]] expected<void, string> set_name(pthread_t thread, string_view name) noexcept
{
    if (name.size() > THREAD_NAME_MAX)
    {
        return unexpected{std::format(
            "Thread name too long, max {} characters allowed: {}", THREAD_NAME_MAX, name
        )};
    }

    if (pthread_setname_np(thread, name.data()) == -1)
    {
        // perror("pthread_setname_np");
        return unexpected{
            std::format("Failed to set thread name: {}. Error: {}", name, std::strerror(errno))
        };
    }

    return {}; // success
}

/**
 * Set the name of the passed POSIX std::jthread.
 */
[[nodiscard]] inline expected<void, string> set_name(
    std::jthread& thread, string_view name
) noexcept
{
    return set_name(thread.native_handle(), name.data());
}

/**
 * Set the name of the passed POSIX std::jthread.
 */
[[nodiscard]] inline expected<void, string> set_name(std::thread& thread, string_view name) noexcept
{
    return set_name(thread.native_handle(), name.data());
}

/**
 * Set the name of the current POSIX thread.
 */
[[nodiscard]] inline expected<void, string> set_name(string_view name) noexcept
{
    return set_name(pthread_self(), name.data());
}
} // namespace threads
} // namespace posix_ipc
