#include <iostream>
#include <atomic>
#include <thread>
#include <vector>
#include <fmt/core.h>
#include <boost/atomic/detail/pause.hpp>
#include "utils.hpp"

struct Request {
    int clientId;
    int value;
    std::atomic<bool> processed;
};

struct SharedData {
    std::atomic<Request*> request;
};

void server_thread(SharedData* shared) {
    // utils::pin_thread(2);

    int processed = 0;
    uint64 last_time = utils::get_clock_monotonic_ns();

    while (true) {
        // Check if there's a pending request
        Request* req = shared->request.load(std::memory_order_acquire);
        if (req != nullptr) {
            // Process the request
            req->value++;  // Simulate processing
            req->processed.store(true, std::memory_order_release);
            // std::cout << "Server processed request from client " << req->clientId << ", value: " << req->value << std::endl;

            // Reset the request pointer
            shared->request.store(nullptr, std::memory_order_release);

            ++processed;
            if (processed % 500'000 == 0)
            {
                uint64 current_time = utils::get_clock_monotonic_ns();
                uint64 elapsed = current_time - last_time;
                auto msgs_per_sec = (int64)((double)processed / (elapsed / 1000000000.0));
                std::cout << fmt::format("{:L} msgs/s", msgs_per_sec) << std::endl;
                last_time = current_time;
                processed = 0;
            }
        }
        else
        {
            // boost::atomics::detail::pause();
            std::this_thread::sleep_for(std::chrono::milliseconds(5)); // Prevent busy-waiting
            // std::this_thread::yield(); // Prevent busy-waiting
        }
    }
}

void client_thread(SharedData* shared, int clientId) {
    Request req;
    req.clientId = clientId;
    req.value = 0;

    while (true) {
        req.processed.store(false, std::memory_order_release);

        // Attempt to set the current request
        Request* expected = nullptr;
        if (shared->request.compare_exchange_strong(expected, &req, std::memory_order_acq_rel)) {
            // Wait for the request to be processed
            while (!req.processed.load(std::memory_order_acquire)) {
                // std::this_thread::yield(); // Prevent busy-waiting
                boost::atomics::detail::pause();
                // std::this_thread::sleep_for(std::chrono::milliseconds(1)); // Prevent busy-waiting
            }

            // std::cout << "Client " << clientId << " received value: " << req.value << std::endl;
            // break; // Exit the loop after processing
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

int main() {
    std::locale::global(std::locale("de_CH.UTF-8"));

    SharedData shared;
    
    std::thread server(server_thread, &shared);

    std::vector<std::thread> clients;
    for (int i = 0; i < 1; ++i) {
        clients.emplace_back(client_thread, &shared, i);
    }

    for (auto& client : clients) {
        client.join();
    }

    // Stop the server thread (for example purposes, you might use a different stopping condition)
    server.detach();

    return 0;
}
