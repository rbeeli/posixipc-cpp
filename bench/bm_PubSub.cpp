#include <atomic>
#include <cstdint>
#include <iostream>
#include <thread>
#include <chrono>
#include <vector>
#include <format>

#include <boost/atomic/detail/pause.hpp>

#include "posix_ipc/rdtsc.hpp"
#include "posix_ipc/threads.hpp"
#include "posix_ipc/SharedMemory.hpp"
#include "posix_ipc/queues/pubsub/PubSub.hpp"
#include "posix_ipc/queues/pubsub/Subscriber.hpp"
#include "posix_ipc/queues/pubsub/enums.hpp"

using namespace std::chrono;
using namespace posix_ipc::queues::spsc;
using namespace posix_ipc::queues::pubsub;
using posix_ipc::queues::Message;
using posix_ipc::queues::MessageView;

void bench(int64_t iters, uint64_t buffer_size, int cpu1, int cpu2, double cycles_per_ns)
{
    const uint64_t storage_size = buffer_size + SPSCStorage::BUFFER_OFFSET;

    SubscriberConfig sub_cfg{
        .shm_name = "bench",
        .capacity_bytes = storage_size,
        .queue_full_policy = QueueFullPolicy::DROP_NEWEST,
        .log_message_drop = true
    };

    PubSub pub_sub;
    pub_sub.subscribe(sub_cfg);

    Subscriber& sub = pub_sub.get_subscriber(sub_cfg.shm_name);

    // producer thread
    nanoseconds producerDuration;
    auto t = std::jthread(
        [&producerDuration, &pub_sub, &sub, iters, cpu1]
        {
            posix_ipc::threads::pin(cpu1);
            posix_ipc::threads::set_name("producer");

            int64_t data = posix_ipc::rdtsc::read();
            auto size = sizeof(int64_t);
            auto msg = posix_ipc::queues::Message::borrows((byte*)&data, size);

            auto t1 = high_resolution_clock::now();
            for (int64_t i = 0; i < iters; ++i)
            {
                // *&data = rdtsc::read();
                *&data = i;
                while (!pub_sub.publish(msg))
                    ;
            }
            producerDuration = duration_cast<nanoseconds>(high_resolution_clock().now() - t1);
        }
    );

    // consumer thread
    nanoseconds consumerDuration;
    auto c = std::jthread(
        [&consumerDuration, &sub, iters, cpu2]
        {
            posix_ipc::threads::pin(cpu2);
            posix_ipc::threads::set_name("consumer");

            auto t1 = high_resolution_clock::now();
            for (int i = 0; i < iters; ++i)
            {
                MessageView msg = sub.queue().dequeue_begin();
                while (msg.empty())
                {
                    msg = sub.queue().dequeue_begin();
                }

                // if (counter == 10'000'000)
                // {
                //     auto clock = rdtsc::read();
                //     auto srv = msg->payload_ptr<uint64>()[0];
                //     auto latency_ns = (clock - srv) / cycles_per_ns;
                //     std::cout << fmt::format("latency ns: {:.0f}", latency_ns) << std::endl;
                //     counter = 0;
                // }
                // counter++;

                // if (i % 100'000 == 0)
                //     std::cout << fmt::format("iteration {}", i) << std::endl;

                // if (srv != i)
                //     throw std::runtime_error("wrong value returned by consumer. Out of order?");

                sub.queue().dequeue_commit(msg);
            }
            consumerDuration = duration_cast<nanoseconds>(high_resolution_clock().now() - t1);
        }
    );

    // wait for threads to finish
    t.join();
    c.join();

    int64_t p_ops = iters * 1e9 / producerDuration.count();
    int64_t c_ops = iters * 1e9 / consumerDuration.count();
    std::cout << std::format("producer {:L} op/s | ", p_ops)
              << std::format("consumer {:L} op/s", c_ops) << std::endl;
}

int main(int argc, char* argv[])
{
    // this somehow triggers ASAN
    std::locale::global(std::locale("de_CH.UTF-8"));

    double cycles_per_ns = posix_ipc::rdtsc::measure_cycles_per_ns();
    std::cout << std::format("cycles per ns: {:.2f}", cycles_per_ns) << std::endl;

    int cpu1 = 2;
    int cpu2 = 4;

    const size_t buffer_size = 1'000'000;
    const int64_t iters = 100'000'000;

    for (int i = 0; i < 10; ++i)
    {
        bench(iters, buffer_size, cpu1, cpu2, cycles_per_ns);
        std::cout << std::endl;
    }

    return 0;
}
