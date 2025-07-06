find_package(PkgConfig REQUIRED)
find_package(benchmark CONFIG REQUIRED)
find_package(Boost REQUIRED COMPONENTS lockfree)


add_executable(bm_spsc_direct bm_spsc_direct.cpp)
target_include_directories(bm_spsc_direct PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(bm_spsc_direct PRIVATE
    benchmark::benchmark
    benchmark::benchmark_main)

add_executable(bm_spsc_threads bm_spsc_threads.cpp)
target_include_directories(bm_spsc_threads PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(bm_spsc_threads PRIVATE
    benchmark::benchmark
    benchmark::benchmark_main
    Boost::lockfree)

add_executable(bm_thread_sync bm_thread_sync.cpp)
target_include_directories(bm_thread_sync PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(bm_thread_sync PRIVATE
    benchmark::benchmark
    benchmark::benchmark_main)

add_executable(bm_SPSCQueue bm_SPSCQueue.cpp)
target_include_directories(bm_SPSCQueue PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(bm_SPSCQueue PRIVATE
    benchmark::benchmark
    benchmark::benchmark_main)

add_executable(bm_PubSub bm_PubSub.cpp)
target_include_directories(bm_PubSub PUBLIC ${PROJECT_SOURCE_DIR}/include)
