find_package(PkgConfig REQUIRED)

add_executable(scratch scratch.cpp)

target_include_directories(scratch PUBLIC ${PROJECT_SOURCE_DIR}/include)
