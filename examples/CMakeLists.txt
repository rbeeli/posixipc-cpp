find_package(PkgConfig REQUIRED)

add_executable(ex_SPSC SPSC.cpp)
target_include_directories(ex_SPSC PUBLIC ${PROJECT_SOURCE_DIR}/include)
