#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <string_view>

namespace posix_ipc
{
namespace queues
{
using std::byte;
using std::string;
using std::string_view;

struct MessageView
{
    byte* payload;
    uint64_t size;
    uint64_t index;

    MessageView() noexcept
        : payload(nullptr), size(0), index(0)
    {
    }

    explicit MessageView(byte* payload, uint64_t size, uint64_t index) noexcept
        : payload(payload), size(size), index(index)
    {
    }

    ~MessageView()
    {
        // a view does not own the payload, don't free it
    }

    // enable copy
    MessageView(const MessageView&) noexcept = default;
    MessageView& operator=(const MessageView&) noexcept = default;

    // enable move
    MessageView(MessageView&&) noexcept = default;
    MessageView& operator=(MessageView&&) noexcept = default;

    __attribute__((always_inline)) [[nodiscard]] inline bool empty() const noexcept
    {
        return size == 0;
    }

    __attribute__((always_inline)) [[nodiscard]] inline size_t payload_size() const noexcept
    {
        return size;
    }

    __attribute__((always_inline)) [[nodiscard]] inline size_t total_size() const noexcept
    {
        return sizeof(uint64_t) + payload_size();
    }

    template <typename T>
    __attribute__((always_inline)) [[nodiscard]] inline T* payload_ptr() noexcept
    {
        return reinterpret_cast<T*>(payload);
    }

    template <typename T>
    __attribute__((always_inline)) inline void copy_payload_to(T* dest) const noexcept
    {
        std::memcpy(dest, payload, payload_size());
    }

    __attribute__((always_inline)) [[nodiscard]] inline string_view as_string_view() const noexcept
    {
        return string_view(reinterpret_cast<const char*>(payload), size);
    }

    __attribute__((always_inline)) [[nodiscard]] inline std::span<byte> as_span() const noexcept
    {
        return std::span<byte>(payload, payload_size());
    }
};

struct Message
{
    byte* payload;
    uint64_t size;
    bool owns_payload;

    explicit Message(byte* payload, uint64_t size, bool owns_payload) noexcept
        : payload(payload), size(size), owns_payload(owns_payload)
    {
    }

    ~Message()
    {
        if (owns_payload && payload)
        {
            delete[] payload;
        }
        payload = nullptr;
    }

    // disable copy
    Message(const Message&) = delete;
    Message& operator=(const Message&) = delete;

    // enable move
    Message(Message&& other) noexcept
        : payload(other.payload), size(other.size), owns_payload(other.owns_payload)
    {
        other.payload = nullptr;
    }
    Message& operator=(Message&& other) noexcept
    {
        if (this != &other)
        {
            payload = other.payload;
            size = other.size;
            owns_payload = other.owns_payload;
            other.payload = nullptr;
        }
        return *this;
    }

    __attribute__((always_inline)) [[nodiscard]] inline size_t payload_size() const noexcept
    {
        return size;
    }

    __attribute__((always_inline)) [[nodiscard]] inline size_t total_size() const noexcept
    {
        return sizeof(uint64_t) + payload_size();
    }

    [[nodiscard]] static inline Message owns(byte* payload, const size_t size) noexcept
    {
        return Message(payload, size, true);
    }

    [[nodiscard]] static inline Message borrows(byte* payload, const size_t size) noexcept
    {
        return Message(payload, size, false);
    }
};
} // namespace queues
} // namespace posix_ipc
