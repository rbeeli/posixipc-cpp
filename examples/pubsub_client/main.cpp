#include <fmt/core.h>
#include <sys/mman.h> // For shared memory
#include <thread>
#include <fcntl.h>  // For O_* constants
#include <unistd.h> // For ftruncate
#include <cstring>  // For strlen, strcpy
#include <iostream>
#include <boost/atomic/detail/pause.hpp>
#include <quill/Quill.h>

#include "quant_base/usings.hpp"
#include "quant_base/threads.hpp"
#include "quant_base/chrono.hpp"
#include "quant_base/rdtsc.hpp"
#include "quant_base/SharedMemory.hpp"
#include "quant_base/messaging/spsc/SPSCQueue.hpp"

using namespace std::chrono;

int main()
{
    // cpptrace::register_terminate_handler();
    std::locale::global(std::locale("de_CH.UTF-8"));
    threads::pin(4);

    quill::Config cfg;
    quill::configure(cfg);
    quill::start();
    quill::Logger* logger = quill::get_logger();

    // const string shm_name = "/pubsub_test";
    // const char* shm_name = "/pubsub_test";
    const char* shm_name = "/pubsub_test_1";

    // shared memory
    SharedMemory shm(logger, shm_name, false);

    std::cout << "Shared memory size: " << shm.size() << std::endl;

    // initialize queue in shared memory
    messaging::SPSCStorage *storage = reinterpret_cast<messaging::SPSCStorage *>(shm.ptr()); // doesn't call constructor
    messaging::SPSCQueue queue(storage);

    std::cout << "storage_size: " << storage->storage_size << std::endl;
    std::cout << "buffer_size: " << storage->buffer_size() << std::endl;
    std::cout << "read_ix: " << storage->read_ix << std::endl;
    std::cout << "write_ix: " << storage->write_ix << std::endl;

    constexpr int64 log_interval = 50'000'000;
    constexpr float64 cycles_per_ns = 3.2;
    uint64 i = 0;
    auto last_t = chrono::clock();
    // int64 last_t = utils::rdtsc();
    // int64 last_val = 0;
    // uint64 counter = 0;
    uint64 avg_latency = 0;
    while (true)
    {
        messaging::MessageView msg = queue.dequeue_begin();
        if (!msg.empty())
        {
            ++i;

            uint64 val_recv = rdtsc::get();
            uint64 val_srv = *msg.payload_ptr<uint64>();
            uint64 delta_cycles = val_recv - val_srv;
            avg_latency += delta_cycles;

            // uint64 curr_val = *msg->payload<uint64>();
            // if (last_val > 0 && curr_val != last_val + 1)
            // {
            //     std::cout << fmt::format("value mismatch: received = {} expected = {} diff = {}", curr_val, last_val, curr_val - last_val) << std::endl;
            // }
            // last_val = curr_val;

            if (i % log_interval == 0)
            {
                // uint64 val_recv = curr_t;
                // watch -n 0.5 "sudo cat /proc/cpuinfo | grep MHz"
                uint64 latency_ns = (uint64)(delta_cycles / cycles_per_ns);

                auto curr_t = chrono::clock();
                auto delta_t = duration_cast<nanoseconds>(curr_t - last_t).count();
                auto msgs_s = (uint64)(i / (delta_t / 1e9));
                auto avg_latency_ns = (uint64)(avg_latency / log_interval / cycles_per_ns);
                std::cout << fmt::format("val = {}  latency = {} ns  avg. latency = {} ns  {:L} msgs/s",
                    val_srv, latency_ns, avg_latency_ns, msgs_s) << std::endl;
                // std::cout << fmt::format("{:L} msgs/s", msgs_s) << std::endl;
                last_t = curr_t;
                i = 0;
                avg_latency = 0;
            }
            queue.dequeue_commit(msg);
        }
        else
        {
            ;
            // boost::atomics::detail::pause(); // faster
        }
    }

    return 0;
}
