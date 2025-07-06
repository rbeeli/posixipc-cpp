find_package(PkgConfig REQUIRED)
find_package(Boost REQUIRED COMPONENTS atomic)

add_executable(ex_SPSC SPSC.cpp)
target_include_directories(ex_SPSC PUBLIC ${PROJECT_SOURCE_DIR}/include)

target_link_libraries(ex_SPSC PRIVATE Boost::atomic)
