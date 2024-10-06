#include <atomic>
#include <iostream>
#include <thread>
#include <vector>
#include <fmt/core.h>
#include "mpmc.hpp"
#include "quant_base/usings.hpp"

void pinThread(int cpu)
{
    if (cpu < 0)
        return;

    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(cpu, &cpuset);
    if (pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset) == -1)
    {
        perror("pthread_setaffinity_no");
        exit(1);
    }
}

template <typename T>
void bench(int cpu1, int cpu2)
{
    pinThread(cpu2);

    const size_t queueSize = 1'048'576;
    const int64_t iters = 100'000'000;

    // create queue
    T q(queueSize);

    // consumer thread
    std::chrono::nanoseconds consumerDuration(0);
    auto t = std::thread(
        [&consumerDuration, &q, cpu1]
        {
            pinThread(cpu1);
            auto t = std::chrono::high_resolution_clock::now();

            for (int i = 0; i < iters; ++i)
            {
                uint32 val;
                q.pop(val);

                if (val != i)
                {
                    throw std::runtime_error("value don't match");
                }
            }

            auto t2 = std::chrono::high_resolution_clock::now();
            consumerDuration = std::chrono::duration_cast<std::chrono::nanoseconds>(t2 - t);
        });

    // producer thread
    auto t1 = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < iters; ++i)
    {
        q.push(i);
    }

    auto t2 = std::chrono::high_resolution_clock::now();

    // wait for consumer thread to finish
    t.join();

    auto ops = iters * 1'000'000'000 /
               std::chrono::duration_cast<std::chrono::nanoseconds>(t2 - t1).count();
    std::cout << fmt::format("producer: {:L} op/s \n", ops);
    ops = iters * 1'000'000'000 / consumerDuration.count();
    std::cout << fmt::format("consumer: {:L} op/s \n", ops);
}

int main(int argc, char *argv[])
{
    std::locale::global(std::locale("de_CH.UTF-8"));

    int cpu1 = 2;
    int cpu2 = 3;

    // bench<rigtorp::MPMCQueue<uint32>>(cpu1, cpu2);

    mpmc::MPMCQueue<int> q(10);
    auto t1 = std::thread(
        [&]
        {
            int v;
            q.pop(v);
            std::cout << "t1 " << v << "\n";
        });

    auto t2 = std::thread(
        [&]
        {
            int v;
            q.pop(v);
            std::cout << "t2 " << v << "\n";
        });

    q.push(1);
    q.push(2);
    t1.join();
    t2.join();

    return 0;
}
