#include <atomic>
#include <iostream>
#include <thread>
#include <mutex>
#include <vector>
#include <format>
#include <cstddef>
#include <boost/atomic/detail/pause.hpp>
#include <memory>

#include "utils.hpp"
#include "posix_ipc/rdtsc.hpp"
#include "posix_ipc/threads.hpp"
#include "posix_ipc/queues/spsc/SPSCQueue.hpp"

using namespace posix_ipc::queues;
using namespace posix_ipc::queues::spsc;
using namespace std::chrono;
using std::byte;
using std::unique_ptr;

void bench(int64_t iters, uint64_t buffer_size, int cpu1, int cpu2, double cycles_per_ns)
{
    const uint64_t storage_size = buffer_size + SPSCStorage::BUFFER_OFFSET;

    // // shared memory
    // auto shm = make_unique<SharedMemory>("spsc_bench", true, storage_size);
    // auto mem_ptr = shm->ptr();

    // in-process memory
    auto mem_ptr = new byte[storage_size];

    SPSCStorage* storage = new (mem_ptr) SPSCStorage(storage_size);
    unique_ptr<SPSCQueue> q = std::make_unique<SPSCQueue>(storage);

    // consumer thread
    nanoseconds producerDuration;
    auto t = std::thread(
        [&producerDuration, &q, iters, cpu1]
        {
            try_or_fail(posix_ipc::threads::pin(cpu1));

            int64_t data = posix_ipc::rdtsc::read();
            auto size = sizeof(int64_t);
            auto msg = Message::borrows((byte*)&data, size);

            auto t1 = high_resolution_clock::now();
            for (int64_t i = 0; i < iters; ++i)
            {
                *&data = posix_ipc::rdtsc::read();
                // *&data = i;
                while (!q->enqueue(msg))
                    ;
            }
            producerDuration = duration_cast<nanoseconds>(high_resolution_clock::now() - t1);
        }
    );

    // consumer thread
    try_or_fail(posix_ipc::threads::pin(cpu2));


    uint64_t counter = 0;
    auto t1 = high_resolution_clock::now();
    for (int i = 0; i < iters; ++i)
    {
        posix_ipc::queues::MessageView msg = q->dequeue_begin();
        while (msg.empty())
            msg = q->dequeue_begin();

        if (counter == 30'000'000)
        {
            auto clock = posix_ipc::rdtsc::read();
            auto srv = msg.payload_ptr<uint64_t>()[0];
            auto latency_ns = (clock - srv) / cycles_per_ns;
            std::cout << std::format("latency ns: {:.0f}", latency_ns) << std::endl;
            counter = 0;
        }
        counter++;

        q->dequeue_commit(msg);
    }
    auto consumerDuration = duration_cast<nanoseconds>(
        std::chrono::high_resolution_clock::now() - t1
    );

    // wait for consumer thread to finish
    t.join();

    int64_t p_ops = iters * 1e9 / producerDuration.count();
    int64_t c_ops = iters * 1e9 / consumerDuration.count();
    std::cout << std::format("producer {:L} op/s | ", p_ops)
              << std::format("consumer {:L} op/s", c_ops) << std::endl;
}

int main(int argc, char* argv[])
{
    // this somehow triggers ASAN
    std::locale::global(std::locale("en_US.UTF-8"));

    double cycles_per_ns = posix_ipc::rdtsc::measure_cycles_per_ns();
    std::cout << std::format("RDTSC cycles per ns: {:.2f}", cycles_per_ns) << std::endl;

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
