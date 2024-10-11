find_package(GTest CONFIG REQUIRED)

enable_testing()

add_executable(test_SPSC SPSC.cpp)
add_executable(test_PosixIpcError PosixIpcError.cpp)

include(GoogleTest)
gtest_discover_tests(test_SPSC)
gtest_discover_tests(test_PosixIpcError)

target_include_directories(test_SPSC PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(test_SPSC PRIVATE
    # GTest::gtest_main
    GTest::gtest
)

target_include_directories(test_PosixIpcError PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(test_PosixIpcError PRIVATE
    GTest::gtest_main
    GTest::gtest
)