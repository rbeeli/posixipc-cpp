#include <gtest/gtest.h>

#include <locale>
#include <string>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <chrono>

#include "posix_ipc/threads.hpp"
#include "posix_ipc/rdtsc.hpp"
#include "posix_ipc/queues/spsc/SPSCQueue.hpp"

using namespace std::chrono;
using namespace posix_ipc::queues;
using namespace posix_ipc::queues::spsc;
using std::byte;
using std::unique_ptr;

static inline size_t next_index(const size_t current_index, const size_t size) noexcept
{
    constexpr auto align = sizeof(size_t);
    size_t ix = current_index + size;
    // round up to the next multiple of align
    return (ix + align - 1) & ~(align - 1);
}

TEST(SPSCQueue, next_index)
{
    EXPECT_EQ(8, next_index(0, 1));
    EXPECT_EQ(8, next_index(0, 2));
    EXPECT_EQ(8, next_index(0, 3));
    EXPECT_EQ(8, next_index(0, 4));
    EXPECT_EQ(8, next_index(0, 5));
    EXPECT_EQ(8, next_index(0, 6));
    EXPECT_EQ(8, next_index(0, 7));
    EXPECT_EQ(8, next_index(0, 8));
    EXPECT_EQ(16, next_index(0, 9));

    EXPECT_EQ(16, next_index(8, 1));
    EXPECT_EQ(16, next_index(8, 2));
    EXPECT_EQ(16, next_index(8, 3));
    EXPECT_EQ(16, next_index(8, 4));
    EXPECT_EQ(16, next_index(8, 5));
    EXPECT_EQ(16, next_index(8, 6));
    EXPECT_EQ(16, next_index(8, 7));
    EXPECT_EQ(16, next_index(8, 8));
    EXPECT_EQ(24, next_index(8, 9));
}

void run_spsc(
    const int64_t iters,
    const int32_t queue_size,
    const int64_t payload_size,
    const bool slow_consumer,
    const bool slow_producer
)
{
    std::cout << std::format(
                     "iters: {:L} | queue_size: {:L} | payload_size: {:L} | slow_consumer: {} | "
                     "slow_producer: {}",
                     iters,
                     queue_size,
                     payload_size,
                     slow_consumer,
                     slow_producer
                 )
              << std::endl;
    double cycles_per_ns = posix_ipc::rdtsc::measure_cycles_per_ns();
    int cpu1 = 2;
    int cpu2 = 4;

    const uint64_t buffer_size = queue_size * sizeof(uint64_t);
    const uint64_t storage_size = buffer_size + SPSCStorage::BUFFER_OFFSET;

    auto buffer = std::make_unique_for_overwrite<std::byte[]>(storage_size);  
    SPSCStorage* storage = new (buffer.get()) SPSCStorage(storage_size);
    auto maybe_queue = SPSCQueue::create(storage);
    if (!maybe_queue)
        throw std::runtime_error(maybe_queue.error().message);
    SPSCQueue q = std::move(*maybe_queue);

    // consumer thread
    std::atomic<bool> started{false};
    nanoseconds consumerDuration(0);
    auto t = std::thread(
        [&consumerDuration, &q, &started, iters, cpu1, cycles_per_ns, slow_consumer]
        {
            [[maybe_unused]] auto res = posix_ipc::threads::pin(cpu1);

            auto t1 = high_resolution_clock::now();

            started = true;

            // uint64 counter = 0;
            for (int i = 0; i < iters; ++i)
            {
                MessageView msg = q.dequeue_begin();
                while (msg.empty())
                    msg = q.dequeue_begin();

                EXPECT_EQ(msg.payload_ptr<uint64_t>()[0], 1);

                if (slow_consumer)
                    std::this_thread::sleep_for(std::chrono::nanoseconds(1000));

                q.dequeue_commit(msg);
            }

            auto t2 = high_resolution_clock::now();
            consumerDuration = duration_cast<nanoseconds>(t2 - t1);
        }
    );

    // producer thread
    while (!started)
        ;
    [[maybe_unused]] auto res = posix_ipc::threads::pin(cpu2);
    auto buffer_payload = std::make_unique_for_overwrite<std::byte[]>(payload_size);  
    byte* payload = buffer_payload.get();
    *reinterpret_cast<uint64_t*>(payload) = 1;
    auto msg = Message::borrows(payload, payload_size);
    auto t1 = high_resolution_clock::now();
    for (int64_t i = 0; i < iters; ++i)
    {
        while (!q.enqueue(msg))
            ;

        if (slow_producer)
            std::this_thread::sleep_for(std::chrono::nanoseconds(1000));
    }

    auto t2 = high_resolution_clock::now();
    nanoseconds producerDuration = duration_cast<nanoseconds>(t2 - t1);

    // wait for consumer thread to finish
    t.join();

    auto p_ops = iters * 1'000'000'000 / producerDuration.count();
    auto c_ops = iters * 1'000'000'000 / consumerDuration.count();
    std::cout << std::format("producer {:L} op/s | ", p_ops)
              << std::format("consumer {:L} op/s", c_ops) << std::endl;
}

TEST(SPSCQueue, queue_sizes)
{
    auto iters_vec = {100};
    auto queue_size = {32,  33,  34,   35,   36,   37,   38,   39,     40,   41,    100,
                       200, 999, 1000, 1001, 1023, 1024, 1025, 10'000, 9999, 10'001};
    auto payload_sizes = {8,  9,  10, 11, 12, 13, 14, 15,  16,  17,  18,   19,
                          20, 21, 22, 23, 24, 32, 64, 128, 256, 512, 1024, 2048};
    for (auto iters : iters_vec)
    {
        for (auto queue_size : queue_size)
        {
            for (auto payload_size : payload_sizes)
            {
                if (payload_size * 2 > queue_size)
                    continue;
                run_spsc(iters, queue_size, payload_size, false, false);
            }
        }
    }
}

TEST(SPSCQueue, queue_sizes_slow_c)
{
    auto iters_vec = {100};
    auto queue_size = {32,  33,  34,   35,   36,   37,   38,   39,     40,   41,    100,
                       200, 999, 1000, 1001, 1023, 1024, 1025, 10'000, 9999, 10'001};
    auto payload_sizes = {8,  9,  10, 11, 12, 13, 14, 15,  16,  17,  18,   19,
                          20, 21, 22, 23, 24, 32, 64, 128, 256, 512, 1024, 2048};
    for (auto iters : iters_vec)
    {
        for (auto queue_size : queue_size)
        {
            for (auto payload_size : payload_sizes)
            {
                if (payload_size * 2 > queue_size)
                    continue;
                run_spsc(iters, queue_size, payload_size, true, false);
            }
        }
    }
}

TEST(SPSCQueue, queue_sizes_slow_p)
{
    auto iters_vec = {100};
    auto queue_size = {32,  33,  34,   35,   36,   37,   38,   39,     40,   41,    100,
                       200, 999, 1000, 1001, 1023, 1024, 1025, 10'000, 9999, 10'001};
    auto payload_sizes = {8,  9,  10, 11, 12, 13, 14, 15,  16,  17,  18,   19,
                          20, 21, 22, 23, 24, 32, 64, 128, 256, 512, 1024, 2048};
    for (auto iters : iters_vec)
    {
        for (auto queue_size : queue_size)
        {
            for (auto payload_size : payload_sizes)
            {
                if (payload_size * 2 > queue_size)
                    continue;
                run_spsc(iters, queue_size, payload_size, false, true);
            }
        }
    }
}

int main(int argc, char** argv)
{
    std::locale::global(std::locale("en_US.UTF-8"));
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}