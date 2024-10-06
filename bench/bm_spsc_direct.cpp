#include <thread>
#include <iostream>
#include <list>
#include <cassert>
#include <cstdint>
#include <cstddef>
#include <benchmark/benchmark.h>
#include <readerwriterqueue/readerwriterqueue.h>
#include <boost/thread/thread.hpp>
#include <boost/lockfree/queue.hpp>
#include <boost/lockfree/spsc_queue.hpp>
#include <rigtorp/SPSCQueue.h>

#include "posix_ipc/threads.hpp"
#include "posix_ipc/queues/spsc/SPSCQueue.hpp"
// #include "posix_ipc/queues/spsc/SPSCQueueStatic.hpp"

using element_type = int64_t;
using std::byte;
const size_t msg_count = 5'000'000;
const size_t queue_size = 100'000;

static void boost_spsc_queue_stack(benchmark::State& state)
{
    posix_ipc::threads::pin(2);

    boost::lockfree::spsc_queue<element_type, boost::lockfree::capacity<queue_size>> queue;

    for (auto _ : state)
    {
        int64_t sum = 0;
        for (size_t i = 0; i < queue_size; ++i)
        {
            queue.push(i);
            element_type s;
            if (queue.pop(s))
                sum += s;
        }

        assert(sum == (queue_size * (queue_size - 1)) / 2);

        benchmark::DoNotOptimize(sum);
    }
}

// ------------------------------------------------------------------------

static void boost_spsc_queue_heap(benchmark::State& state)
{
    posix_ipc::threads::pin(2);

    auto queue =
        new boost::lockfree::spsc_queue<element_type, boost::lockfree::capacity<queue_size>>();

    for (auto _ : state)
    {
        int64_t sum = 0;
        for (size_t i = 0; i < queue_size; ++i)
        {
            queue->push(i);
            element_type s;
            if (queue->pop(s))
                sum += s;
        }

        assert(sum == (queue_size * (queue_size - 1)) / 2);

        benchmark::DoNotOptimize(sum);
    }

    delete queue;
}

// ------------------------------------------------------------------------

static void rigtorp_SPSCQueue_stack(benchmark::State& state)
{
    posix_ipc::threads::pin(2);

    rigtorp::SPSCQueue<element_type> spsc(queue_size);

    for (auto _ : state)
    {
        int64_t sum = 0;
        for (size_t i = 0; i < queue_size; ++i)
        {
            spsc.push(i);
            auto r = spsc.front();
            sum += *r;
            spsc.pop();
        }

        assert(sum == (queue_size * (queue_size - 1)) / 2);

        benchmark::DoNotOptimize(sum);
    }
}

// ------------------------------------------------------------------------

static void rigtorp_SPSCQueue_heap(benchmark::State& state)
{
    posix_ipc::threads::pin(2);

    auto* spsc = new rigtorp::SPSCQueue<element_type>(queue_size);

    for (auto _ : state)
    {
        int64_t sum = 0;
        for (size_t i = 0; i < queue_size; ++i)
        {
            spsc->push(i);
            auto r = spsc->front();
            sum += *r;
            spsc->pop();
        }

        assert(sum == (queue_size * (queue_size - 1)) / 2);

        benchmark::DoNotOptimize(sum);
    }

    delete spsc;
}

// ------------------------------------------------------------------------

static void moodycamel_ReaderWriterQueue_stack(benchmark::State& state)
{
    posix_ipc::threads::pin(2);

    moodycamel::ReaderWriterQueue<element_type> rwQueue(queue_size);

    for (auto _ : state)
    {
        int64_t sum = 0;
        for (size_t i = 0; i < queue_size; ++i)
        {
            rwQueue.enqueue(i);
            element_type r;
            if (rwQueue.try_dequeue(r))
                sum += r;
        }

        assert(sum == (queue_size * (queue_size - 1)) / 2);

        benchmark::DoNotOptimize(sum);
    }
}

// ------------------------------------------------------------------------

static void moodycamel_ReaderWriterQueue_heap(benchmark::State& state)
{
    posix_ipc::threads::pin(2);

    auto* rwQueue = new moodycamel::ReaderWriterQueue<element_type>(queue_size);

    for (auto _ : state)
    {
        int64_t sum = 0;
        for (size_t i = 0; i < queue_size; ++i)
        {
            rwQueue->enqueue(i);
            element_type r;
            if (rwQueue->try_dequeue(r))
                sum += r;
        }

        assert(sum == (queue_size * (queue_size - 1)) / 2);

        benchmark::DoNotOptimize(sum);
    }

    delete rwQueue;
}

// ------------------------------------------------------------------------

// static void own_SPSCQueueStatic_heap(benchmark::State& state)
// {
//     posix_ipc::threads::pin(2);

//     const uint64_t buffer_size = queue_size * sizeof(element_type);
//     const uint64_t buffer_offset = sizeof(messaging::SPSCStorageStatic);
//     const uint64_t storage_size = buffer_offset + buffer_size;

//     // initialize queue
//     char* mem_ptr = new char[storage_size]();
//     auto* storage = new (mem_ptr) messaging::SPSCStorageStatic(storage_size, buffer_size, buffer_offset);
//     auto* queue = new messaging::SPSCQueueStatic<element_type>(storage);

//     for (auto _ : state)
//     {
//         int64_t sum = 0;
//         for (size_t i = 0; i < queue_size; ++i)
//         {
//             queue->enqueue(i);
//             element_type r;
//             if (queue->dequeue(r))
//                 sum += r;
//         }

//         assert(sum == (queue_size * (queue_size - 1)) / 2);

//         benchmark::DoNotOptimize(sum);
//     }

//     queue->~SPSCQueueStatic();
//     delete[] mem_ptr;
// }

// ------------------------------------------------------------------------

static void own_SPSCQueue_heap(benchmark::State& state)
{
    posix_ipc::threads::pin(2);

    const uint64_t buffer_size = queue_size * sizeof(element_type);
    const uint64_t storage_size = buffer_size + posix_ipc::queues::spsc::SPSCStorage::BUFFER_OFFSET;

    // initialize queue
    char* mem_ptr = new char[storage_size]();
    auto* storage = new (mem_ptr) posix_ipc::queues::spsc::SPSCStorage(storage_size);
    auto* queue = new posix_ipc::queues::spsc::SPSCQueue(storage);

    for (auto _ : state)
    {
        int64_t sum = 0;
        for (size_t i = 0; i < queue_size; ++i)
        {
            posix_ipc::queues::Message msg(reinterpret_cast<byte*>(&i), sizeof(element_type), false);
            queue->enqueue(msg);
            if (auto msg2 = queue->dequeue_begin(); !msg2.empty())
            {
                auto el_ptr = msg2.payload_ptr<element_type>();
                sum += *el_ptr;
                queue->dequeue_commit(msg2);
            }
        }

        assert(sum == (queue_size * (queue_size - 1)) / 2);

        benchmark::DoNotOptimize(sum);
    }

    queue->~SPSCQueue();
    delete[] mem_ptr;
}

// ------------------------------------------------------------------------

// clang-format off
#define BENCH(func) \
    BENCHMARK(func)->Unit(benchmark::TimeUnit::kMicrosecond)->\
    MinWarmUpTime(0.1)->MinTime(1.0)
// clang-format on

// BENCH(own_SPSCQueueStatic_heap);
BENCH(own_SPSCQueue_heap);
BENCH(rigtorp_SPSCQueue_stack);
BENCH(rigtorp_SPSCQueue_heap);
BENCH(boost_spsc_queue_stack);
BENCH(boost_spsc_queue_heap);
BENCH(moodycamel_ReaderWriterQueue_stack);
BENCH(moodycamel_ReaderWriterQueue_heap);
