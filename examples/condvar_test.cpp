#include <iostream>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include "quant_base/usings.hpp"
#include "utils.hpp"

const int NUM_ITERATIONS = 1'000'000; // Number of ping-pong iterations

int main()
{
    utils::pin_thread(4);

    while (true)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));

    float64 cycles_per_ns;
    for (int i = 0; i < 5; ++i)
    {
        int64_t start_c = utils::rdtsc();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        int64_t end_c = utils::rdtsc();
        cycles_per_ns = (end_c - start_c) / 100'000'000.0;
        std::cout << "Cycles per ns: " << cycles_per_ns << std::endl;
    }

    std::mutex mtx;
    std::condition_variable cv;
    bool turn = true; // true: ping's turn, false: pong's turn

    int64 send_time;
    float64 avg_latency_ns;
    std::chrono::high_resolution_clock::time_point start;
    std::chrono::high_resolution_clock::time_point end;

    std::thread ping_thread(
        [&cv, &mtx, &turn, &start, &send_time]
        {
            utils::pin_thread(2);

            start = utils::clock();
            for (int i = 0; i < NUM_ITERATIONS; ++i)
            {
                send_time = utils::rdtsc();
                std::unique_lock<std::mutex> lock(mtx);
                cv.wait(lock, [&]
                        { return turn; });
                turn = false;
                cv.notify_one();

                // std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        });

    std::thread pong_thread(
        [&cv, &mtx, &turn, &end, &send_time, &avg_latency_ns, cycles_per_ns]
        {
            utils::pin_thread(3);

            for (int i = 0; i < NUM_ITERATIONS; ++i)
            {
                std::unique_lock<std::mutex> lock(mtx);
                cv.wait(lock, [&]
                        { return !turn; });
                turn = true;
                auto recv_time = utils::rdtsc();
                auto latency_ns = (recv_time - send_time) / cycles_per_ns;
                avg_latency_ns += latency_ns;
                // std::cout << "RTT: " << latency_ns << " ns" << std::endl;
                cv.notify_one();
            }
            end = utils::clock();
        });

    ping_thread.join();
    pong_thread.join();

    std::chrono::duration<double, std::micro> duration = end - start;

    std::cout << "Avg. latency " << avg_latency_ns / NUM_ITERATIONS << " ns" << std::endl;
    std::cout << "Avg. time per iteration: "
              << duration.count() / NUM_ITERATIONS << " us" << std::endl;

    return 0;
}
