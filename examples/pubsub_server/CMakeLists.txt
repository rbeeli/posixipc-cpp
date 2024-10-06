add_executable(pubsub_server main.cpp)

target_link_libraries(pubsub_server PRIVATE quant-base-cpp)

# --------------------
# copy files
# --------------------
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/subscribers/pubsub_test_1.cfg
    ${CMAKE_CURRENT_BINARY_DIR}/subscribers/pubsub_test_1.cfg
    COPYONLY)


# --------------------
# External Libraries
# --------------------

# fmt library
find_package(fmt CONFIG REQUIRED)
target_link_libraries(pubsub_server PRIVATE fmt::fmt)

# quill
find_package(quill CONFIG REQUIRED)
target_link_libraries(pubsub_server PRIVATE quill::quill)
