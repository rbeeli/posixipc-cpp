#include <atomic>
#include <chrono>
#include <cstdint>
#include <string>
#include <iostream>
#include <thread>
#include <mutex>
#include <vector>
#include <boost/atomic/detail/pause.hpp>

#include "posix_ipc/threads.hpp"

using std::string;

// https://rigtorp.se/ringbuffer/

template <typename T, typename TEl>
void bench(
    const string name, const TEl iters, const int32_t queue_size, const int cpu1, const int cpu2
)
{
    // create queue
    // T q(queue_size);
    T* q = new T(queue_size);

    // consumer thread
    std::chrono::nanoseconds consumerDuration(0);
    auto t = std::thread(
        [&consumerDuration, &q, iters, cpu1]
        {
            posix_ipc::threads::pin(cpu1);
            auto t1 = std::chrono::high_resolution_clock::now();

            TEl val;
            for (TEl i = 0; i < iters; ++i)
            {
                while (!q->pop(val))
                    ;
            }

            auto t2 = std::chrono::high_resolution_clock::now();
            consumerDuration = std::chrono::duration_cast<std::chrono::nanoseconds>(t2 - t1);
        }
    );

    // producer thread
    posix_ipc::threads::pin(cpu2);
    auto t1 = std::chrono::high_resolution_clock::now();
    for (TEl i = 0; i < iters; ++i)
    {
        while (!q->push(i))
            ;
    }

    auto t2 = std::chrono::high_resolution_clock::now();
    std::chrono::nanoseconds producerDuration =
        std::chrono::duration_cast<std::chrono::nanoseconds>(t2 - t1);

    // wait for consumer thread to finish
    t.join();

    auto p_ops = static_cast<double>(iters) * 1'000'000'000 / producerDuration.count();
    auto c_ops = static_cast<double>(iters) * 1'000'000'000 / consumerDuration.count();
    std::cout << name << std::format(": producer: {:L} op/s | ", (uint64_t)p_ops)
              << std::format("consumer: {:L} op/s", (uint64_t)c_ops) << std::endl;

    delete q;
}

struct ringbuffer0
{
    std::vector<uint32_t> data_{};
    std::atomic<uint32_t> readIdx_{0};
    std::atomic<uint32_t> writeIdx_{0};

    ringbuffer0(uint32_t capacity) : data_(capacity, 0)
    {
    }

    bool push(uint32_t val)
    {
        auto const writeIdx = writeIdx_.load(std::memory_order::relaxed);
        auto nextWriteIdx = writeIdx + 1;
        if (nextWriteIdx == data_.size())
            nextWriteIdx = 0;
        if (nextWriteIdx == readIdx_.load(std::memory_order::acquire))
            return false;
        data_[writeIdx] = val;
        writeIdx_.store(nextWriteIdx, std::memory_order::release);
        return true;
    }

    bool pop(uint32_t& val)
    {
        auto const readIdx = readIdx_.load(std::memory_order::relaxed);
        if (readIdx == writeIdx_.load(std::memory_order::acquire))
            return false;
        val = data_[readIdx];
        auto nextReadIdx = readIdx + 1;
        if (nextReadIdx == data_.size())
            nextReadIdx = 0;
        readIdx_.store(nextReadIdx, std::memory_order::release);
        return true;
    }
};

struct ringbuffer1
{
    std::vector<uint32_t> data_{};
    alignas(64) std::atomic<uint32_t> readIdx_{0};
    alignas(64) std::atomic<uint32_t> writeIdx_{0};

    ringbuffer1(uint32_t capacity) : data_(capacity, 0)
    {
    }

    bool push(uint32_t val)
    {
        auto const writeIdx = writeIdx_.load(std::memory_order::relaxed);
        auto nextWriteIdx = writeIdx + 1;
        if (nextWriteIdx == data_.size())
            nextWriteIdx = 0;
        if (nextWriteIdx == readIdx_.load(std::memory_order::acquire))
            return false;
        data_[writeIdx] = val;
        writeIdx_.store(nextWriteIdx, std::memory_order::release);
        return true;
    }

    bool pop(uint32_t& val)
    {
        auto const readIdx = readIdx_.load(std::memory_order::relaxed);
        if (readIdx == writeIdx_.load(std::memory_order::acquire))
            return false;
        val = data_[readIdx];
        auto nextReadIdx = readIdx + 1;
        if (nextReadIdx == data_.size())
            nextReadIdx = 0;
        readIdx_.store(nextReadIdx, std::memory_order::release);
        return true;
    }
};

struct ringbuffer2
{
    std::vector<uint32_t> data_{};
    alignas(64) std::atomic<uint32_t> readIdx_{0};
    alignas(64) uint32_t writeIdxCached_{0};
    alignas(64) std::atomic<uint32_t> writeIdx_{0};
    alignas(64) uint32_t readIdxCached_{0};

    ringbuffer2(uint32_t capacity) : data_(capacity, 0)
    {
    }

    bool push(uint32_t val)
    {
        auto const writeIdx = writeIdx_.load(std::memory_order::relaxed);
        auto nextWriteIdx = writeIdx + 1;
        if (nextWriteIdx == data_.size())
            nextWriteIdx = 0;
        if (nextWriteIdx == readIdxCached_)
        {
            readIdxCached_ = readIdx_.load(std::memory_order::acquire);
            if (nextWriteIdx == readIdxCached_)
                return false;
        }
        data_[writeIdx] = val;
        writeIdx_.store(nextWriteIdx, std::memory_order::release);
        return true;
    }

    bool pop(uint32_t& val)
    {
        auto const readIdx = readIdx_.load(std::memory_order::relaxed);
        if (readIdx == writeIdxCached_)
        {
            writeIdxCached_ = writeIdx_.load(std::memory_order::acquire);
            if (readIdx == writeIdxCached_)
                return false;
        }
        val = data_[readIdx];
        auto nextReadIdx = readIdx + 1;
        if (nextReadIdx == data_.size())
            nextReadIdx = 0;
        readIdx_.store(nextReadIdx, std::memory_order::release);
        return true;
    }
};

struct ringbuffer3
{
    uint64_t* data_;
    alignas(64) std::atomic<uint64_t> readIdx_{0};
    alignas(64) uint64_t writeIdxCached_{0};
    alignas(64) std::atomic<uint64_t> writeIdx_{0};
    alignas(64) uint64_t readIdxCached_{0};
    const uint64_t capacity_;

    ringbuffer3(uint64_t capacity) : capacity_(capacity)
    {
        data_ = new uint64_t[capacity];
    }

    ~ringbuffer3()
    {
        delete[] data_;
    }

    bool push(uint64_t val)
    {
        auto const writeIdx = writeIdx_.load(std::memory_order::relaxed);
        auto nextWriteIdx = writeIdx + 1;
        if (nextWriteIdx == capacity_)
            nextWriteIdx = 0;
        if (nextWriteIdx == readIdxCached_)
        {
            readIdxCached_ = readIdx_.load(std::memory_order::acquire);
            if (nextWriteIdx == readIdxCached_)
                return false;
        }
        data_[writeIdx] = val;
        writeIdx_.store(nextWriteIdx, std::memory_order::release);
        return true;
    }

    bool pop(uint64_t& val)
    {
        auto const readIdx = readIdx_.load(std::memory_order::relaxed);
        if (readIdx == writeIdxCached_)
        {
            writeIdxCached_ = writeIdx_.load(std::memory_order::acquire);
            if (readIdx == writeIdxCached_)
                return false;
        }
        val = data_[readIdx];
        auto nextReadIdx = readIdx + 1;
        if (nextReadIdx == capacity_)
            nextReadIdx = 0;
        readIdx_.store(nextReadIdx, std::memory_order::release);
        return true;
    }
};

struct ringbuffer4
{
    alignas(64) uint32_t readIdx_{0};
    alignas(64) uint32_t writeIdx_{0};
    uint32_t capacity_;
    uint32_t* data_{};

    std::mutex mutex_;

    ringbuffer4(uint32_t capacity)
    {
        data_ = new uint32_t[capacity];
        capacity_ = capacity;
    }

    ~ringbuffer4()
    {
        delete[] data_;
    }

    bool push(uint32_t val)
    {
        while (!mutex_.try_lock())
            ;

        auto nextWriteIdx = writeIdx_ + 1;
        if (nextWriteIdx == capacity_)
            nextWriteIdx = 0;
        if (nextWriteIdx == readIdx_)
        {
            mutex_.unlock();
            return false;
        }

        data_[writeIdx_] = val;
        writeIdx_ = nextWriteIdx;

        mutex_.unlock();

        return true;
    }

    bool pop(uint32_t& val)
    {
        std::lock_guard<std::mutex> lock(mutex_);

        if (readIdx_ == writeIdx_)
        {
            return false; // Buffer is empty
        }

        val = data_[readIdx_];

        auto nextReadIdx = readIdx_ + 1;
        if (nextReadIdx == capacity_)
            nextReadIdx = 0;
        readIdx_ = nextReadIdx;

        return true;
    }
};


int main(int argc, char* argv[])
{
    std::locale::global(std::locale("en_US.UTF-8"));

    //   int cpu1 = -1;
    //   int cpu2 = -1;
    int cpu1 = 2;
    int cpu2 = 4;

    if (argc == 3)
    {
        cpu1 = std::stoi(argv[1]);
        cpu2 = std::stoi(argv[2]);
    }

    const size_t queue_size = 1'000'000;
    const int64_t iters = 20'000'000;

    for (int i = 0; i < 10; ++i)
    {
        bench<ringbuffer0, uint32_t>("ringbuffer unal at        ", iters, queue_size, cpu1, cpu2);
        bench<ringbuffer1, uint32_t>("ringbuffer   al at        ", iters, queue_size, cpu1, cpu2);
        bench<ringbuffer2, uint32_t>("ringbuffer   al at chd    ", iters, queue_size, cpu1, cpu2);
        bench<ringbuffer3, uint64_t>(
            "ringbuffer   al at chd uint64", iters, queue_size, cpu1, cpu2
        );
        bench<ringbuffer4, uint32_t>("ringbuffer mutex          ", iters, queue_size, cpu1, cpu2);
        std::cout << std::endl;
    }

    return 0;
}
