#include <atomic>
#include <cstdint>
#include <iostream>
#include <thread>
#include <chrono>
#include <vector>
#include <format>

#include "utils.hpp"
#include "posix_ipc/rdtsc.hpp"
#include "posix_ipc/threads.hpp"
#include "posix_ipc/SharedMemory.hpp"
#include "posix_ipc/queues/pubsub/PubSub.hpp"
#include "posix_ipc/queues/pubsub/Subscriber.hpp"
#include "posix_ipc/queues/pubsub/enums.hpp"

using namespace std::chrono;
using namespace posix_ipc::queues;
using namespace posix_ipc::queues::spsc;
using namespace posix_ipc::queues::pubsub;

void bench(
    PubSub& pub_sub, Subscriber& subscriber, int64_t iters, int cpu1, int cpu2, double cycles_per_ns
)
{
    // producer thread
    nanoseconds producer_duration;
    auto p = std::jthread(
        [&producer_duration, &pub_sub, iters, cpu1]
        {
            try_or_fail(posix_ipc::threads::pin(cpu1));
            try_or_fail(posix_ipc::threads::set_name("producer"));

            int64_t data = posix_ipc::rdtsc::read();
            auto size = sizeof(int64_t);
            auto msg = Message::borrows((byte*)&data, size);

            auto t1 = high_resolution_clock::now();
            for (int64_t i = 0; i < iters; ++i)
            {
                *&data = posix_ipc::rdtsc::read();
                // *&data = i;
                while (!pub_sub.publish(msg))
                    ;
            }
            producer_duration = duration_cast<nanoseconds>(high_resolution_clock().now() - t1);
        }
    );

    // consumer thread
    nanoseconds consumer_duration;
    auto c = std::jthread(
        [&consumer_duration, &subscriber, cycles_per_ns, iters, cpu2]
        {
            try_or_fail(posix_ipc::threads::pin(cpu2));
            try_or_fail(posix_ipc::threads::set_name("consumer"));

            uint64_t counter = 0;
            auto t1 = high_resolution_clock::now();
            for (int i = 0; i < iters; ++i)
            {
                MessageView msg = subscriber.dequeue_begin();
                while (msg.empty())
                    msg = subscriber.dequeue_begin();

                if (counter == 30'000'000)
                {
                    auto clock = posix_ipc::rdtsc::read();
                    auto srv = msg.payload_ptr<uint64_t>()[0];
                    auto latency_ns = (clock - srv) / cycles_per_ns;
                    std::cout << std::format("latency ns: {:.0f}", latency_ns) << std::endl;
                    counter = 0;
                }
                counter++;

                subscriber.dequeue_commit(msg);
            }
            consumer_duration = duration_cast<nanoseconds>(high_resolution_clock().now() - t1);
        }
    );

    // wait for threads to finish
    p.join();
    c.join();

    int64_t p_ops = static_cast<double>(iters) * 1e9 / producer_duration.count();
    int64_t c_ops = static_cast<double>(iters) * 1e9 / consumer_duration.count();
    std::cout << std::format("producer {:L} op/s | ", (int64_t)p_ops)
              << std::format("consumer {:L} op/s", (int64_t)c_ops) << std::endl;
}

int main(int argc, char* argv[])
{
    std::locale::global(std::locale("en_US.UTF-8"));

    double cycles_per_ns = posix_ipc::rdtsc::measure_cycles_per_ns();
    std::cout << std::format("Cycles per ns: {:.2f}", cycles_per_ns) << std::endl;

    int cpu1 = 2;
    int cpu2 = 4;

    const size_t storage_size = 1'000'000;
    const int64_t iters = 100'000'000;

    // Create shared memory region for PubSub
    PubSubConfig sub_cfg{
        .shm_name = "bench",
        .storage_size_bytes = storage_size,
        .queue_full_policy = QueueFullPolicy::DROP_NEWEST,
        .log_message_drop = true
    };
    std::vector<PubSubConfig> sub_cfgs{sub_cfg};

    // Create PubSub instance
    PubSub pub_sub;
    pub_sub.sync_configs(sub_cfgs, true);

    // Create Subscriber instance using shared memory
    auto sub_cfg_res = Subscriber::from_config(sub_cfg);
    if (!sub_cfg_res.has_value())
    {
        std::cerr << "Subscriber::from_config error: " << sub_cfg_res.error() << std::endl;
        return 1;
    }
    Subscriber& subscriber = sub_cfg_res.value();

    for (int i = 0; i < 10; ++i)
    {
        bench(pub_sub, subscriber, iters, cpu1, cpu2, cycles_per_ns);
    }

    return 0;
}
