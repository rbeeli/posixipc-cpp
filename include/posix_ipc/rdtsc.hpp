#pragma once

#include <cstdint>
#include <time.h>    // for clock_gettime
#include <algorithm> // for std::nth_element

namespace posix_ipc
{
namespace rdtsc
{
#ifdef __ARM_ARCH_ISA_A64
// Adapted from: https://github.com/cloudius-systems/osv/blob/master/arch/aarch64/arm-clock.cc
__attribute__((always_inline)) inline int64_t read() noexcept
{
    // Please note we read CNTVCT cpu system register which provides
    // the accross-system consistent value of the virtual system counter.
    uint64_t cntvct;
    asm volatile("mrs %0, cntvct_el0; " : "=r"(cntvct)::"memory");
    return static_cast<int64_t>(cntvct);
}
#else
#include <x86intrin.h>

/**
 * Returns the current value of the TSC (time-stamp counter) register.
 */
__attribute__((always_inline)) inline int64_t read() noexcept
{
    return __rdtsc();
}
#endif


// inline int64_t get_clock_monotonic_ns()
// {
//     // https://github.com/roq-trading/roq-api/blob/e4e554a8af3154724bf793c086f3171361e3e383/include/roq/clock.hpp
//     struct timespec tp;
//     clock_gettime(CLOCK_MONOTONIC, &tp);
//     return tp.tv_sec * 1000000000 + tp.tv_nsec;
// }

/**
 * Returns the number of cycles per nanosecond
 * based on the TSC (time-stamp counter) register.
 * This allows to convert cycles measured with `read()` to nanoseconds.
 *
 * The result is only an approximation and may vary
 * between different CPU models, cores and system settings,
 * e.g. turbo boost, power saving, etc.
 *
 * The result is also affected by the current load on the system.
 * It is recommended to run this function in a quiet environment
 * and pin the thread to a specific core.
 *
 * Function runs for approx. 10 ms.
 */
inline double measure_cycles_per_ns() noexcept
{
    struct timespec tp;

    // measure overhead of clock_gettime
    clock_gettime(CLOCK_MONOTONIC, &tp);
    int64_t start_t = tp.tv_sec * 1000000000 + tp.tv_nsec;
    for (int i = 0; i < 10'000; ++i)
    {
        clock_gettime(CLOCK_MONOTONIC, &tp);
    }
    int64_t end_t = tp.tv_sec * 1000000000 + tp.tv_nsec;
    double overhead_per_call = static_cast<double>(end_t - start_t) / 10'000;

    // warm-up phase
    for (int i = 0; i < 1000; ++i)
    {
        read();
        clock_gettime(CLOCK_MONOTONIC, &tp);
    }

    // measure
    constexpr int loops = 1001;
    constexpr uint64_t wait_time_ns = 10'000;
    double measurements[loops];
    for (int i = 0; i < loops; ++i)
    {
        const int64_t start_cycles = read();
        clock_gettime(CLOCK_MONOTONIC, &tp);
        const int64_t start = tp.tv_sec * 1000000000 + tp.tv_nsec;
        const int64_t end = start + wait_time_ns;
        for (; tp.tv_sec * 1000000000 + tp.tv_nsec < end; clock_gettime(CLOCK_MONOTONIC, &tp))
            ;
        const uint64_t end_cycles = read();
        measurements[i] = static_cast<double>(end_cycles - start_cycles) /
                          (end - start - overhead_per_call);
    }

    // read median value
    std::nth_element(measurements, measurements + loops / 2, measurements + loops);

    return measurements[loops / 2];
}
} // namespace rdtsc
} // namespace posix_ipc