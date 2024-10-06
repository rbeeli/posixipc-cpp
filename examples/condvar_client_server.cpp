#include <iostream>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <pthread.h>
#include <cstring>
#include <thread>
#include <fmt/core.h>
#include "utils.hpp"

struct ClientData {
    int client_id;
    int value;
    bool processed;
};

struct SharedData {
    pthread_mutex_t mutex;
    pthread_cond_t cond_srv;     // Used to signal the server
    pthread_cond_t cond_clt;     // Used to signal back to the client
    ClientData* currentRequest;  // Current request being processed
};

void server_thread(SharedData* shared) {
    int processed = 0;
    uint64 last_time = utils::get_clock_monotonic_ns();

    while (true) {
        pthread_mutex_lock(&shared->mutex);

        // wait for a signal from any client
        while (shared->currentRequest == nullptr) {
            pthread_cond_wait(&shared->cond_srv, &shared->mutex);
        }

        // process the current request
        shared->currentRequest->value++;
        shared->currentRequest->processed = true;
        // std::cout << "Server: processed client " << shared->currentRequest->client_id << " value " << shared->currentRequest->value << std::endl;

        ++processed;
        if (processed % 10000 == 0)
        {
            uint64 current_time = utils::get_clock_monotonic_ns();
            uint64 elapsed = current_time - last_time;
            auto msgs_per_sec = (int64)((double)processed / (elapsed / 1000000000.0));
            std::cout << fmt::format("{:L} msgs/s", msgs_per_sec) << std::endl;
            last_time = current_time;
            processed = 0;
        }

        shared->currentRequest = nullptr;
        pthread_cond_signal(&shared->cond_clt); // Signal back to the client

        pthread_mutex_unlock(&shared->mutex);
    }
}

void client_thread(SharedData* shared, int client_id) {
    ClientData data;
    data.client_id = client_id;
    data.value = client_id; // Initial value

    while (true) {
        pthread_mutex_lock(&shared->mutex);

        // Wait if another request is being processed
        while (shared->currentRequest != nullptr) {
            pthread_cond_wait(&shared->cond_clt, &shared->mutex);
        }

        // Write request to shared data
        shared->currentRequest = &data;

        // Signal server for processing
        pthread_cond_signal(&shared->cond_srv);

        // Wait for server to process data
        while (shared->currentRequest != nullptr) {
            pthread_cond_wait(&shared->cond_clt, &shared->mutex);
        }

        // std::cout << "Client " << client_id << ": received " << data.value << std::endl;

        pthread_mutex_unlock(&shared->mutex);

        // std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

int main() {
    std::locale::global(std::locale("de_CH.UTF-8"));

    const int NUM_CLIENTS = 50;
    const char* shm_name = "/my_shared_memory";

    int fd = shm_open(shm_name, O_CREAT | O_RDWR, 0666);
    int a = ftruncate(fd, sizeof(SharedData));
    SharedData* shared = static_cast<SharedData*>(mmap(NULL, sizeof(SharedData), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0));

    // Initialize shared data
    std::memset(shared, 0, sizeof(SharedData));
    pthread_mutex_init(&shared->mutex, NULL);
    pthread_cond_init(&shared->cond_srv, NULL);
    pthread_cond_init(&shared->cond_clt, NULL);

    // Create server and client threads
    std::thread server(server_thread, shared);
    std::thread clients[NUM_CLIENTS];
    for (int i = 0; i < NUM_CLIENTS; ++i) {
        clients[i] = std::thread(client_thread, shared, i);
    }

    // Join threads
    server.join();
    for (int i = 0; i < NUM_CLIENTS; ++i) {
        clients[i].join();
    }

    // Cleanup
    pthread_mutex_destroy(&shared->mutex);
    pthread_cond_destroy(&shared->cond_srv);
    pthread_cond_destroy(&shared->cond_clt);
    munmap(shared, sizeof(SharedData));
    close(fd);
    shm_unlink(shm_name);

    return 0;
}
