#include <atomic>
#include <condition_variable>
#include <deque>
#include <format>
#include <cstddef>
#include <iostream>
#include <list>
#include <locale>
#include <mutex>
#include <thread>
#include <chrono>
#include <utility>

#include <benchmark/benchmark.h>
#include <boost/atomic/detail/pause.hpp>
#include <boost/lockfree/queue.hpp>
#include <boost/lockfree/spsc_queue.hpp>
#include <readerwriterqueue/readerwriterqueue.h>
#include <rigtorp/SPSCQueue.h>

#include "posix_ipc/threads.hpp"
#include "posix_ipc/queues/spsc/SPSCQueue.hpp"

// clang-format off
#define BENCH(func) \
    BENCHMARK(func)->\
    Unit(benchmark::TimeUnit::kMillisecond)->MinTime(1)
// clang-format on

using std::pair;
using std::byte;

constexpr bool PRINT_STATS = false;
const size_t msg_count = 10'000'000;
const size_t queue_capacity = 100'000;
// const size_t msg_count = 10'000;
// const size_t queue_capacity = 1000;
using element_type = int64_t;

template <typename queue_type, typename TFnProducer, typename TFnConsumer>
static void bm_runner(
    queue_type* msg_queue,
    TFnProducer producer,
    TFnConsumer consumer,
    benchmark::State& state
)
{
    posix_ipc::threads::pin(2);

    for (auto _ : state)
    {
        std::atomic<bool> ready = std::atomic<bool>{false};

        // start consumer thread
        std::thread consumer_thread(
            [&consumer, &ready]
            {
                posix_ipc::threads::pin(3);

                ready = true;

                auto t = std::chrono::high_resolution_clock::now();
                auto [sum, counter] = consumer();
                auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                      std::chrono::high_resolution_clock::now() - t
                )
                                      .count();

                // sanity checks
                if (counter != msg_count)
                {
                    std::cout << std::format(
                                     "mismatch counter={}, msg_count={}", counter, msg_count
                                 )
                              << "\n";
                    std::exit(1);
                }
                int64_t expected_sum = (msg_count - 1) * (msg_count) / 2;
                if (sum != expected_sum)
                {
                    std::cout << std::format("wrong sum, got {}, expected {}", sum, expected_sum)
                              << "\n";
                    std::exit(1);
                }

                // print stats
                if (PRINT_STATS)
                {
                    auto msgs_sec = static_cast<int>(msg_count / (elapsed_ms / 1'000'000.0));
                    std::cout << std::format("Elapsed time: {} ms [{} msg/s]", elapsed_ms, msgs_sec)
                              << "\n";
                }
            }
        );

        // wait for consumer to be ready
        while (!ready)
            boost::atomics::detail::pause();

        producer();

        consumer_thread.join();
    }
}

// https://github.com/rohitjoshi/queue-benchmark/blob/master/queue_test.h#L189


// ------------------------------------------------------------------

static void deque_mutex(benchmark::State& state)
{
    auto* msg_queue = new std::deque<element_type>();

    std::mutex mu;
    std::condition_variable cv;

    auto producer = [msg_queue, &cv, &mu]()
    {
        size_t counter = 0;
        while (counter < msg_count)
        {
            element_type item = counter;
            {
                std::unique_lock<std::mutex> ul{mu};
                msg_queue->push_back(item);
                cv.notify_one();
            }
            ++counter;
        }
    };

    auto consumer = [msg_queue, &cv, &mu]()
    {
        int64_t sum = 0;
        size_t counter = 0;
        while (counter < msg_count)
        {
            std::unique_lock<std::mutex> ul{mu};
            cv.wait(ul, [msg_queue] { return !msg_queue->empty(); });

            while (!msg_queue->empty())
            {
                sum += msg_queue->front();
                msg_queue->pop_front();
                ++counter;
            }
        }
        return std::make_pair(sum, counter);
    };

    bm_runner(msg_queue, producer, consumer, state);

    delete msg_queue;
}

// ------------------------------------------------------------------

static void moodycamel_ReaderWriterQueue(benchmark::State& state)
{
    auto* msg_queue = new moodycamel::ReaderWriterQueue<element_type>(queue_capacity);

    auto producer = [msg_queue]()
    {
        size_t counter = 0;
        while (counter < msg_count)
        {
            element_type item = counter;
            while (!msg_queue->try_enqueue(item))
                ;
            ++counter;
        }
    };

    auto consumer = [msg_queue]()
    {
        int64_t sum = 0;
        size_t counter = 0;
        element_type item;
        while (counter < msg_count)
        {
            while (!msg_queue->try_dequeue(item))
                ;
            ++counter;
            sum += item;
        }
        return std::make_pair(sum, counter);
    };

    bm_runner(msg_queue, producer, consumer, state);

    delete msg_queue;
}

// ------------------------------------------------------------------

static void moodycamel_BlockingReaderWriterQueue_wait(benchmark::State& state)
{
    std::locale::global(std::locale("de_CH.UTF-8"));

    auto* msg_queue = new moodycamel::BlockingReaderWriterQueue<element_type>(queue_capacity);

    auto producer = [msg_queue]()
    {
        size_t counter = 0;
        while (counter < msg_count)
        {
            element_type item = counter;
            while (!msg_queue->try_enqueue(item))
                ;
            ++counter;
        }
    };

    auto consumer = [msg_queue]()
    {
        int64_t sum = 0;
        size_t counter = 0;
        element_type item;
        while (counter < msg_count)
        {
            while (!msg_queue->wait_dequeue_timed(item, 1'000)) // 1ms
                ;
            ++counter;
            sum += item;
        }
        return std::make_pair(sum, counter);
    };

    bm_runner(msg_queue, producer, consumer, state);

    delete msg_queue;
}

// ------------------------------------------------------------------

// https://www.boost.org/doc/libs/1_82_0/doc/html/lockfree/examples.html#lockfree.examples.queue
static void boost_lockfree_queue_busyspin(benchmark::State& state)
{
    auto* msg_queue = new boost::lockfree::queue<element_type>(queue_capacity);

    auto producer = [msg_queue]()
    {
        size_t counter = 0;
        while (counter < msg_count)
        {
            element_type item = counter;
            while (!msg_queue->push(item))
                ;
            ++counter;
        }
    };

    auto consumer = [msg_queue]()
    {
        int64_t sum = 0;
        size_t counter = 0;
        element_type item;
        while (counter < msg_count)
        {
            while (!msg_queue->pop(item))
                ;
            ++counter;
            sum += item;
        }
        return std::make_pair(sum, counter);
    };

    bm_runner(msg_queue, producer, consumer, state);

    delete msg_queue;
}

// ------------------------------------------------------------------

// https://www.boost.org/doc/libs/1_82_0/doc/html/lockfree/examples.html#lockfree.examples.waitfree_single_producer_single_consumer_queue
static void boost_lockfree_spsc_queue_busyspin(benchmark::State& state)
{
    auto* msg_queue =
        new boost::lockfree::spsc_queue<element_type, boost::lockfree::capacity<queue_capacity>>();

    auto producer = [msg_queue]()
    {
        size_t counter = 0;
        while (counter < msg_count)
        {
            element_type item = counter;
            while (!msg_queue->push(item))
                ;
            ++counter;
        }
    };

    auto consumer = [msg_queue]()
    {
        int64_t sum = 0;
        size_t counter = 0;
        element_type item;
        while (counter < msg_count)
        {
            while (msg_queue->pop(item))
            {
                ++counter;
                sum += item;
            }
        }
        return std::make_pair(sum, counter);
    };

    bm_runner(msg_queue, producer, consumer, state);

    delete msg_queue;
}

// ------------------------------------------------------------------

// https://www.boost.org/doc/libs/1_82_0/doc/html/lockfree/examples.html#lockfree.examples.waitfree_single_producer_single_consumer_queue
static void boost_lockfree_spsc_queue_yield(benchmark::State& state)
{
    auto* msg_queue =
        new boost::lockfree::spsc_queue<element_type, boost::lockfree::capacity<queue_capacity>>();

    auto producer = [msg_queue]()
    {
        size_t counter = 0;
        while (counter < msg_count)
        {
            element_type item = counter;
            while (!msg_queue->push(item))
                std::this_thread::yield();
            ++counter;
        }
    };

    auto consumer = [msg_queue]()
    {
        int64_t sum = 0;
        size_t counter = 0;
        element_type item;
        while (counter < msg_count)
        {
            while (!msg_queue->pop(item))
                std::this_thread::yield();
            ++counter;
            sum += item;
        }
        return std::make_pair(sum, counter);
    };

    bm_runner(msg_queue, producer, consumer, state);

    delete msg_queue;
}

// ------------------------------------------------------------------

// https://github.com/rigtorp/SPSCQueue/tree/master
static void rigtorp_SPSCQueue_busyspin(benchmark::State& state)
{
    auto* msg_queue = new rigtorp::SPSCQueue<element_type>(queue_capacity);

    auto producer = [msg_queue]()
    {
        size_t counter = 0;
        while (counter < msg_count)
        {
            element_type item = counter;
            while (!msg_queue->try_push(item))
                ;
            ++counter;
        }
    };

    auto consumer = [msg_queue]()
    {
        int64_t sum = 0;
        size_t counter = 0;
        element_type item;
        while (counter < msg_count)
        {
            while (!msg_queue->front())
                ;
            item = *msg_queue->front();
            ++counter;
            sum += item;
            msg_queue->pop();
        }
        return std::make_pair(sum, counter);
    };

    bm_runner(msg_queue, producer, consumer, state);

    delete msg_queue;
}

// ------------------------------------------------------------------

// https://github.com/rigtorp/SPSCQueue/tree/master
static void rigtorp_SPSCQueue_yield(benchmark::State& state)
{
    auto* msg_queue = new rigtorp::SPSCQueue<element_type>(queue_capacity);

    auto producer = [msg_queue]()
    {
        size_t counter = 0;
        while (counter < msg_count)
        {
            element_type item = counter;
            while (!msg_queue->try_push(item))
                std::this_thread::yield();
            ++counter;
        }
    };

    auto consumer = [msg_queue]()
    {
        int64_t sum = 0;
        size_t counter = 0;
        element_type item;
        while (counter < msg_count)
        {
            while (!msg_queue->front())
                std::this_thread::yield();
            item = *msg_queue->front();
            ++counter;
            sum += item;
            msg_queue->pop();
        }
        return std::make_pair(sum, counter);
    };

    bm_runner(msg_queue, producer, consumer, state);

    delete msg_queue;
}

// ------------------------------------------------------------------

// https://github.com/rigtorp/SPSCQueue/tree/master
static void rigtorp_SPSCQueue_pause_pc(benchmark::State& state)
{
    auto* msg_queue = new rigtorp::SPSCQueue<element_type>(queue_capacity);

    auto producer = [msg_queue]()
    {
        size_t counter = 0;
        while (counter < msg_count)
        {
            element_type item = counter;
            while (!msg_queue->try_push(item))
                boost::atomics::detail::pause();
            ++counter;
        }
    };

    auto consumer = [msg_queue]()
    {
        int64_t sum = 0;
        size_t counter = 0;
        element_type item;
        while (counter < msg_count)
        {
            while (!msg_queue->front())
                boost::atomics::detail::pause();
            item = *msg_queue->front();
            ++counter;
            sum += item;
            msg_queue->pop();
        }
        return std::make_pair(sum, counter);
    };

    bm_runner(msg_queue, producer, consumer, state);

    delete msg_queue;
}

// ------------------------------------------------------------------

// https://github.com/rigtorp/SPSCQueue/tree/master
static void rigtorp_SPSCQueue_pause_p(benchmark::State& state)
{
    auto* msg_queue = new rigtorp::SPSCQueue<element_type>(queue_capacity);

    auto producer = [msg_queue]()
    {
        size_t counter = 0;
        while (counter < msg_count)
        {
            element_type item = counter;
            while (!msg_queue->try_push(item))
                boost::atomics::detail::pause();
            ++counter;
        }
    };

    auto consumer = [msg_queue]()
    {
        int64_t sum = 0;
        size_t counter = 0;
        element_type item;
        while (counter < msg_count)
        {
            while (!msg_queue->front())
                ;
            item = *msg_queue->front();
            ++counter;
            sum += item;
            msg_queue->pop();
        }
        return std::make_pair(sum, counter);
    };

    bm_runner(msg_queue, producer, consumer, state);

    delete msg_queue;
}

// ------------------------------------------------------------------

// https://github.com/rigtorp/SPSCQueue/tree/master
static void rigtorp_SPSCQueue_pause_c(benchmark::State& state)
{
    auto* msg_queue = new rigtorp::SPSCQueue<element_type>(queue_capacity);

    auto producer = [msg_queue]()
    {
        size_t counter = 0;
        while (counter < msg_count)
        {
            element_type item = counter;
            while (!msg_queue->try_push(item))
                ;
            ++counter;
        }
    };

    auto consumer = [msg_queue]()
    {
        int64_t sum = 0;
        size_t counter = 0;
        element_type item;
        while (counter < msg_count)
        {
            while (!msg_queue->front())
                boost::atomics::detail::pause();
            item = *msg_queue->front();
            ++counter;
            sum += item;
            msg_queue->pop();
        }
        return std::make_pair(sum, counter);
    };

    bm_runner(msg_queue, producer, consumer, state);

    delete msg_queue;
}

// ------------------------------------------------------------------

static void own_SPSCQueue_busyspin(benchmark::State& state)
{
    const uint64_t buffer_size = queue_capacity * sizeof(element_type);
    const uint64_t storage_size = buffer_size + posix_ipc::queues::spsc::SPSCStorage::BUFFER_OFFSET;

    // allocate memory
    char* mem_ptr = new char[storage_size]();

    // initialize queue
    auto* storage = new (mem_ptr) posix_ipc::queues::spsc::SPSCStorage(storage_size);
    auto* msg_queue = new posix_ipc::queues::spsc::SPSCQueue(storage);

    auto producer = [msg_queue]()
    {
        size_t counter = 0;
        byte* payload = new byte[sizeof(element_type)];
        auto msg = posix_ipc::queues::Message::borrows(payload, sizeof(element_type));
        while (counter < msg_count)
        {
            element_type item = counter;
            std::memcpy(payload, &item, sizeof(element_type));
            while (!msg_queue->enqueue(msg))
                ;
            ++counter;
        }
        delete[] payload;
    };

    auto consumer = [msg_queue]()
    {
        int64_t sum = 0;
        size_t counter = 0;
        posix_ipc::queues::MessageView msg;
        while (counter < msg_count)
        {
            while ((msg = msg_queue->dequeue_begin()).empty())
                ;
            ++counter;
            sum += msg.payload_ptr<element_type>()[0];
            msg_queue->dequeue_commit(msg);
        }
        return std::make_pair(sum, counter);
    };

    bm_runner(msg_queue, producer, consumer, state);

    msg_queue->~SPSCQueue();
    delete[] mem_ptr;
}

// ------------------------------------------------------------------

static void own_SPSCQueue_pause_c(benchmark::State& state)
{
    const uint64_t buffer_size = queue_capacity * sizeof(element_type);
    const uint64_t storage_size = buffer_size + posix_ipc::queues::spsc::SPSCStorage::BUFFER_OFFSET;

    // allocate memory
    char* mem_ptr = new char[storage_size]();

    // initialize queue
    auto* storage = new (mem_ptr) posix_ipc::queues::spsc::SPSCStorage(storage_size);
    auto* msg_queue = new posix_ipc::queues::spsc::SPSCQueue(storage);

    auto producer = [msg_queue]()
    {
        size_t counter = 0;
        byte* payload = new byte[sizeof(element_type)];
        auto msg = posix_ipc::queues::Message::owns(payload, sizeof(element_type));
        while (counter < msg_count)
        {
            element_type item = counter;
            std::memcpy(payload, &item, sizeof(element_type));
            // auto msg = Message::borrows(sizeof(element_type), payload);
            while (!msg_queue->enqueue(msg))
                ;
            ++counter;
        }
    };

    auto consumer = [msg_queue]()
    {
        int64_t sum = 0;
        size_t counter = 0;
        posix_ipc::queues::MessageView msg;
        while (counter < msg_count)
        {
            while ((msg = msg_queue->dequeue_begin()).empty())
                boost::atomics::detail::pause();

            ++counter;
            sum += msg.payload_ptr<element_type>()[0];
            msg_queue->dequeue_commit(msg);
        }
        return std::make_pair(sum, counter);
    };

    bm_runner(msg_queue, producer, consumer, state);

    msg_queue->~SPSCQueue();
    delete[] mem_ptr;
}

// ------------------------------------------------------------------

// static void own_SPSCQueueStatic_pause_c(benchmark::State& state)
// {
//     const size_t buffer_size = queue_capacity * sizeof(element_type);
//     const ptrdiff_t buffer_offset = sizeof(messaging::SPSCStorageStatic);
//     const size_t storage_size = buffer_offset + buffer_size;

//     // allocate memory
//     byte* mem_ptr = new byte[storage_size]();

//     // initialize queue
//     auto* storage = new (mem_ptr) messaging::SPSCStorageStatic(storage_size, buffer_size, buffer_offset);
//     auto* msg_queue = new messaging::SPSCQueueStatic<element_type>(storage);

//     function<void()> producer = [msg_queue]()
//     {
//         size_t counter = 0;
//         while (counter < msg_count)
//         {
//             element_type item = counter;
//             // std::cout << fmt::format("enqueue {}", item) << "\n";
//             while (!msg_queue->enqueue(item))
//                 ;
//             ++counter;
//         }
//     };

//     function<pair<int64_t, int64_t>()> consumer = [msg_queue]()
//     {
//        int64_t sum = 0;
//         size_t counter = 0;
//         element_type item;
//         while (counter < msg_count)
//         {
//             while (!msg_queue->dequeue(item))
//                 boost::atomics::detail::pause();
//             // std::cout << fmt::format("dequeue {}", item) << "\n";
//             ++counter;
//             sum += item;
//         }
//         return std::make_pair(sum, counter);
//     };

//     bm_runner(msg_queue, producer, consumer, state);

//     msg_queue->~SPSCQueueStatic();
//     delete[] mem_ptr;
// }

// ------------------------------------------------------------------

// BENCH(deque_mutex);
BENCH(moodycamel_ReaderWriterQueue);
// BENCH(moodycamel_BlockingReaderWriterQueue_wait);
// BENCH(boost_lockfree_queue_busyspin);
BENCH(boost_lockfree_spsc_queue_busyspin);
BENCH(boost_lockfree_spsc_queue_yield);
BENCH(rigtorp_SPSCQueue_busyspin);
BENCH(rigtorp_SPSCQueue_yield);
BENCH(rigtorp_SPSCQueue_pause_pc);
BENCH(rigtorp_SPSCQueue_pause_p);
BENCH(rigtorp_SPSCQueue_pause_c);
BENCH(own_SPSCQueue_busyspin);
BENCH(own_SPSCQueue_pause_c);
// BENCH(own_SPSCQueueStatic_pause_c);
