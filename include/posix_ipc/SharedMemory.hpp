#pragma once

#include <cstring>
#include <cstddef>
#include <cstdint>
#include <sys/mman.h>  // shared memory
#include <unistd.h>    // ftruncate
#include <fcntl.h>     // O_* constants
#include <sys/types.h> // fstat
#include <sys/stat.h>  // fstat
#include <unistd.h>    // fstat
#include <iostream>
#include <string>
#include <format>
#include <expected>

#include "posix_ipc/errors.hpp"

namespace posix_ipc
{
class SharedMemory
{
private:
    std::string shm_name_;
    uint64_t size_;
    int shm_fd_;
    std::byte* shm_ptr_;
    bool created_;

private:
    SharedMemory(std::string name, const size_t size, int fd, std::byte* ptr, bool created)
        : shm_name_(name), size_(size), shm_fd_(fd), shm_ptr_(ptr), created_(created)
    {
    }

public:
    inline std::string name() const
    {
        return shm_name_;
    }

    inline std::byte* ptr() const
    {
        return shm_ptr_;
    }

    inline uint64_t size() const
    {
        return size_;
    }

    [[nodiscard]] static std::expected<SharedMemory, PosixIpcError> open(std::string name)
    {
        return open_or_create(name, 0, false);
    }

    [[nodiscard]] static std::expected<SharedMemory, PosixIpcError> open_or_create(
        std::string name, size_t size, bool create = true
    )
    {
        auto created_ = false;
        std::byte* shm_ptr_ = nullptr;

        auto shm_mode = 0666;
        auto shm_flags = create ? O_CREAT | O_EXCL | O_RDWR | O_TRUNC : O_RDWR;
        auto shm_fd_ = ::shm_open(name.c_str(), shm_flags, shm_mode);
        if (shm_fd_ == -1)
        {
            if (errno == EEXIST)
            {
                // open it without O_EXCL, already exists
                shm_flags = shm_flags & ~O_EXCL;
                shm_fd_ = ::shm_open(name.c_str(), shm_flags, shm_mode);
                if (shm_fd_ == -1)
                {
                    auto msg = std::format(
                        "name={}, create={}, size={}: strerror={}, errno={}",
                        name,
                        create,
                        size,
                        std::strerror(errno),
                        errno
                    );
                    return std::unexpected{PosixIpcError(PosixIpcErrorCode::shm_open_failed, msg)};
                }
                created_ = false;
            }
            else
            {
                auto msg = std::format(
                    "name={}, create={}, size={}, strerror={}, errno={}",
                    name,
                    create,
                    size,
                    std::strerror(errno),
                    errno
                );
                return std::unexpected{PosixIpcError(PosixIpcErrorCode::shm_open_failed, msg)};
            }
        }

        if (create)
        {
            // create shared memory region
            if (size <= 0)
            {
                auto msg = std::format(
                    "Shared memory [{}] size parameter must be greater than 0, got {}.", name, size
                );
                return std::unexpected{PosixIpcError(PosixIpcErrorCode::shm_open_failed, msg)};
            }

            std::clog << std::format("Creating shared memory [{}] of size {} bytes", name, size)
                      << std::endl;

            if (::ftruncate(shm_fd_, size) == -1)
            {
                ::close(shm_fd_);
                auto msg = std::format(
                    "name={}, strerror={}, errno={}", name, std::strerror(errno), errno
                );
                return std::unexpected{PosixIpcError(PosixIpcErrorCode::shm_truncate_failed, msg)};
            }
        }
        else
        {
            // read size of existing shared memory region
            struct stat s;
            if (::fstat(shm_fd_, &s) == -1)
            {
                ::close(shm_fd_);
                auto msg = std::format(
                    "name={}, strerror={}, errno={}", name, std::strerror(errno), errno
                );
                return std::unexpected{PosixIpcError(PosixIpcErrorCode::shm_fstat_failed, msg)};
            }
            size = s.st_size;

            // std::clog << std::format("Opened shared memory [{}] of size {} bytes", name, size_)
            //           << std::endl;
        }

        // map the shared memory region into the address space of the process
        //  | MAP_HUGETLB
        shm_ptr_ = reinterpret_cast<std::byte*>(
            ::mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED_VALIDATE, shm_fd_, 0)
        );
        if (shm_ptr_ == MAP_FAILED)
        {
            ::close(shm_fd_);
            auto msg = std::format(
                "name={}, strerror={}, errno={}", name, std::strerror(errno), errno
            );
            return std::unexpected{PosixIpcError(PosixIpcErrorCode::shm_mmap_failed, msg)};
        }

        return SharedMemory(name, size, shm_fd_, shm_ptr_, created_);
    }

    ~SharedMemory()
    {
        ::munmap(shm_ptr_, size_);
        if (created_)
        {
            // destroy shared memory if created (owner)
            ::shm_unlink(shm_name_.c_str());
        }
        ::close(shm_fd_);
    }

    // disable copy
    SharedMemory(const SharedMemory&) = delete;
    SharedMemory& operator=(const SharedMemory&) = delete;

    // enable move
    SharedMemory(SharedMemory&& other)
        : shm_name_(std::move(other.shm_name_)),
          size_(other.size_),
          shm_fd_(other.shm_fd_),
          shm_ptr_(other.shm_ptr_),
          created_(other.created_)
    {
        other.shm_fd_ = -1;
        other.shm_ptr_ = nullptr;
    }
    SharedMemory& operator=(SharedMemory&& other)
    {
        if (this != &other)
        {
            shm_name_ = std::move(other.shm_name_);
            size_ = other.size_;
            shm_fd_ = other.shm_fd_;
            shm_ptr_ = other.shm_ptr_;
            created_ = other.created_;
            other.shm_fd_ = -1;
            other.shm_ptr_ = nullptr;
        }
        return *this;
    }

    static bool exists(const std::string& name) noexcept
    {
        errno = 0;

        // attempt to create the shared memory object exclusively
        auto shm_fd = ::shm_open(name.c_str(), O_RDWR | O_CREAT | O_EXCL, 0666);
        if (shm_fd > 0)
        {
            // does not exist yet, close and unlink
            close(shm_fd);
            ::shm_unlink(name.c_str());
            return false;
        }
        else
        {
            if (errno == EEXIST)
            {
                // shared memory object already exists
                return true;
            }
        }

        return false;
    }
};
} // namespace posix_ipc