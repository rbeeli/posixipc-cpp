add_executable(pubsub_client main.cpp)

target_link_libraries(pubsub_client PRIVATE quant-base-cpp)

# --------------------
# External Libraries
# --------------------

# fmt library
find_package(fmt CONFIG REQUIRED)
target_link_libraries(pubsub_client PRIVATE fmt::fmt)

# quill
find_package(quill CONFIG REQUIRED)
target_link_libraries(pubsub_client PRIVATE quill::quill)
