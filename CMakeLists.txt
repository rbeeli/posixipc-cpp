cmake_minimum_required(VERSION 3.25)

option(POSIX_IPC_BUILD_EXAMPLES "Build example programs in ./examples directory" OFF)
option(POSIX_IPC_BUILD_TESTS "Build tests in ./test directory" OFF)
option(POSIX_IPC_BUILD_BENCH "Build benchmarks programs in ./bench directory" OFF)
option(POSIX_IPC_BUILD_SCRATCH "Build scratch programs in ./scratch directory" OFF)

if(POSIX_IPC_BUILD_TESTS)
    list(APPEND VCPKG_MANIFEST_FEATURES "tests")
endif()

project(posixipc VERSION 0.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(${PROJECT_NAME} INTERFACE)

target_include_directories(${PROJECT_NAME} INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)

# ./examples
if(POSIX_IPC_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

# ./tests
if(POSIX_IPC_BUILD_TESTS)
    add_subdirectory(tests)
endif()

# ./bench
if(POSIX_IPC_BUILD_BENCH)
    add_subdirectory(bench)
endif()

# ./scratch
if(POSIX_IPC_BUILD_SCRATCH)
    add_subdirectory(scratch)
endif()

# --------------------------------------------------

# Install
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}ConfigVersion.cmake"
    COMPATIBILITY SameMajorVersion
)

export(
    TARGETS ${PROJECT_NAME}
    NAMESPACE ${PROJECT_NAME}::
    FILE "${PROJECT_NAME}Config.cmake"
)

if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
    install(
        DIRECTORY "include/"
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    )

    install(
        TARGETS ${PROJECT_NAME}
        EXPORT "${PROJECT_NAME}Config"
        INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    )

    install(
        EXPORT "${PROJECT_NAME}Config"
        NAMESPACE ${PROJECT_NAME}::
        DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/${PROJECT_NAME}"
    )

    install(
        FILES "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}ConfigVersion.cmake"
        DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/${PROJECT_NAME}"
    )
endif()
