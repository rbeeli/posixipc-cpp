#include <sys/mman.h> // For shared memory
#include <thread>
#include <fcntl.h>  // For O_* constants
#include <unistd.h> // For ftruncate
#include <cstring>  // For strlen, strcpy
#include <iostream>
#include <chrono>
#include <format>

#include "posix_ipc/SharedMemory.hpp"
#include "posix_ipc/queues/Message.hpp"
#include "posix_ipc/queues/spsc/SPSCQueue.hpp"
#include "posix_ipc/queues/spsc/SPSCStorage.hpp"

using namespace std::chrono_literals;
using namespace posix_ipc;
using namespace posix_ipc::queues;
using namespace posix_ipc::queues::spsc;

int main()
{
    // cpptrace::register_terminate_handler();

    // const char* shm_name = "spscqueue_jl_to_cpp";
    const char* shm_name = "dummy_pubsub_signal";

    // shared memory
    auto maybe_shm = SharedMemory::open(shm_name);
    if (!maybe_shm)
    {
        std::cout << maybe_shm.error() << std::endl;
        return 1;
    }
    auto& shm = maybe_shm.value();

    std::cout << "Shared memory size: " << shm.size() << std::endl;

    // initialize queue in shared memory
    SPSCStorage* storage = reinterpret_cast<SPSCStorage*>(shm.ptr()); // doesn't call constructor
    SPSCQueue queue(storage);

    std::cout << "storage_size: " << storage->storage_size() << std::endl;
    std::cout << "buffer_size: " << storage->buffer_size() << std::endl;
    std::cout << "message count: " << queue.size() << std::endl;
    // std::cout << "read_ix: " << storage->read_ix << std::endl;
    // std::cout << "write_ix: " << storage->write_ix << std::endl;

    while (true)
    {
        if (queue.size() > 0)
        {
            auto msg = queue.dequeue_begin();
            if (!msg.empty())
            {
                std::cout << "Size: " << msg.size << std::endl;

                std::byte* ptr = msg.payload_ptr<std::byte>();

                //  * - `inst_id` [`std::uint32_t`] (fixed)
                //  * - `data_time` [`std::int64_t`] (fixed)
                //  * - `signal_time` [`std::int64_t`] (fixed)
                //  * - `recv_time` [`std::int64_t`] (fixed)
                //  * - `exe_time` [`std::int64_t`] (fixed)
                //  * - `value` [`double`]       (fixed)
                std::cout << "inst_id=" << *(uint32_t*)ptr << std::endl;
                std::cout << "data_time=" << *(int64_t*)(ptr+8) << std::endl;
                std::cout << "signal_time=" << *(int64_t*)(ptr+8+8) << std::endl;
                std::cout << "recv_time=" << *(int64_t*)(ptr+8+8+8) << std::endl;
                std::cout << "exe_time=" << *(int64_t*)(ptr+8+8+8+8) << std::endl;
                std::cout << "value=" << *(double*)(ptr+8+8+8+8+8) << std::endl;

                // uint64_t val = *ptr;
                // uint64_t val2 = *++ptr;
                // std::cout << std::format("val = {} val2 = {}\n", val, val2);

                queue.dequeue_commit(msg);
            }
            else
            {
                std::cout << "message empty" << std::endl;
            }
        }
        else
            std::this_thread::sleep_for(50ms);
    }

    return 0;
}
