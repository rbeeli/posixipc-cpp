#include <iostream>
#include <thread>
#include <fmt/core.h>
#include <boost/atomic/detail/pause.hpp>
#include <quill/Quill.h>

#include "quant_base/usings.hpp"
#include "quant_base/threads.hpp"
#include "quant_base/rdtsc.hpp"
#include "quant_base/DirectoryWatcher.hpp"
#include "quant_base/SharedMemory.hpp"
#include "quant_base/messaging/Message.hpp"
#include "quant_base/messaging/spsc/SPSCQueue.hpp"
#include "quant_base/messaging/pubsub/Subscriber.hpp"
#include "quant_base/messaging/pubsub/PubSub.hpp"

int main(int argc, char *argv[])
{
    std::locale::global(std::locale("de_CH.UTF-8"));

    quill::Config cfg;
    quill::configure(cfg);
    quill::start();
    quill::Logger* logger = quill::get_logger();

    threads::pin(2);

    messaging::PubSub queue(logger);

    // register subscribers
    auto cfgs_path = std::filesystem::absolute("subscribers").string();

    // watch for config changes in separate thread
    auto cfg_change_handler =
        [&](const fs::path &path, bool is_directory, InotifyEvent mask)
    {
        LOG_INFO(logger, "Syncing subscribers from: {}", path);

        // get *.cfg files sorted
        vector<std::filesystem::directory_entry> files;
        for (const auto &entry : std::filesystem::directory_iterator(cfgs_path))
            if (entry.path().extension() == ".cfg")
                files.push_back(entry);
        std::sort(files.begin(), files.end());

        vector<messaging::SubscriberConfig> cfgs;
        for (const auto &entry : files)
        {
            std::cout << fmt::format("  - {}", entry.path().filename().string()) << std::endl;

            try
            {
                cfgs.push_back(messaging::SubscriberConfig::from_cfg_file(entry.path().string()));
            }
            catch (const std::exception &e)
            {
                std::cerr << "Error parsing config file: " << entry.path().string() << std::endl;
                std::cerr << e.what() << '\n';
            }
        }

        // sync with PubSub queue
        queue.sync_with_subscriber_configs(cfgs);

        LOG_INFO(logger, "Synced subscribers");
    };

    // initial config sync
    cfg_change_handler(cfgs_path, true, InotifyEvent::NONE);

    // watch for changes in subscribers directory
    auto mask = InotifyEvent::MODIFY | InotifyEvent::CREATE | InotifyEvent::DELETE |
                InotifyEvent::DELETE_SELF | InotifyEvent::MOVED_TO | InotifyEvent::MOVED_FROM |
                InotifyEvent::MOVE_SELF;
    DirectoryWatcher watcher(logger, cfgs_path, mask, true);
    watcher.set_callback(cfg_change_handler);
    watcher.start_thread();

    auto size = sizeof(uint64);
    byte *arr = new byte[size];
    // uint64 last_time = utils::get_clock_monotonic_ns();
    for (uint64 i = 1; i < UINT64_MAX; ++i)
    {
        // // Hello, world!
        // std::string str = "Hello, world!";
        // auto const size = str.size() + 1; // +1 for null terminator
        // auto arr = std::make_unique<char8_t[]>(size);
        // memcpy(arr.get(), str.c_str(), size);

        uint64 const clock = rdtsc::get();
        // uint64 const clock = utils::get_clock_monotonic_ns();
        std::memcpy(arr, &clock, size);
        // memcpy(arr, &i, size);
        auto msg = messaging::Message::borrows(arr, size);
        queue.publish(msg);

        // boost::atomics::detail::pause();

        // auto t = utils::get_clock_monotonic_ns();
        // if (t - last_time > 1e9)
        // {
        //     std::cout << "Published " << i << " messages" << std::endl;
        //     last_time = t;
        // }

        // std::this_thread::sleep_for(std::chrono::nanoseconds(10));
        // std::this_thread::sleep_for(std::chrono::milliseconds(10));
        // if (i % 1'000 == 0)
        // std::cout << "Published " << i << std::endl;
    }

    return 0;
}
