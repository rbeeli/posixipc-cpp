#pragma once

#include <thread>
#include <pthread.h>
#include <string>

namespace posix_ipc
{
namespace threads
{
using std::string;

/**
 * Pin the passed POSIX thread to a specific CPU.
 */
void pin(pthread_t thread, int cpu)
{
    if (cpu < 0)
        return;
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(cpu, &cpuset);
    if (pthread_setaffinity_np(thread, sizeof(cpu_set_t), &cpuset) == -1)
    {
        perror("pthread_setaffinity_np");
        exit(1);
    }
}

/**
 * Pin the passed POSIX thread to a specific CPU.
 */
inline void pin(std::jthread& thread, int cpu)
{
    pin(thread.native_handle(), cpu);
}

/**
 * Pin the passed POSIX thread to a specific CPU.
 */
inline void pin(std::thread& thread, int cpu)
{
    pin(thread.native_handle(), cpu);
}

/**
 * Pin the current thread to a specific CPU.
 */
inline void pin(int cpu)
{
    pin(pthread_self(), cpu);
}

/**
 * Set the name of the passed POSIX thread.
 */
void set_name(pthread_t thread, const string name)
{
    if (name.size() > 15)
        throw std::runtime_error("Thread name too long, max 15 characters allowed: " + name);

    if (pthread_setname_np(thread, name.c_str()) == -1)
    {
        perror("pthread_setname_np");
        exit(1);
    }
}

/**
 * Set the name of the passed POSIX std::jthread.
 */
inline void set_name(std::jthread& thread, const string name)
{
    set_name(thread.native_handle(), name.c_str());
}

/**
 * Set the name of the passed POSIX std::jthread.
 */
inline void set_name(std::thread& thread, const string name)
{
    set_name(thread.native_handle(), name.c_str());
}

/**
 * Set the name of the current POSIX thread.
 */
inline void set_name(const string name)
{
    set_name(pthread_self(), name.c_str());
}
} // namespace threads
} // namespace posix_ipc