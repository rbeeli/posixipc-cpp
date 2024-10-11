#include <gtest/gtest.h>

#include <iostream>
#include <format>
#include <string>

#include "posix_ipc/errors.hpp"

TEST(PosixIpcError, format_PosixIpcErrorCode)
{
    auto formatted = std::format("{}", posix_ipc::PosixIpcErrorCode::shm_open_failed);
    EXPECT_EQ(formatted, "shm_open_failed");
}

TEST(PosixIpcError, format_PosixIpcError)
{
    auto error = posix_ipc::PosixIpcError(
        posix_ipc::PosixIpcErrorCode::shm_open_failed, "CUSTOM MESSAGE"
    );
    auto formatted = std::format("{}", error);
    std::cout << formatted << std::endl;
    std::string expected = "PosixIpcError shm_open_failed: CUSTOM MESSAGE at ";
    EXPECT_TRUE(formatted.substr(0, expected.size()) == expected);
}
