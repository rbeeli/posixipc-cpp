#pragma once

#include <iostream>

template <typename T>
void try_or_fail(const T& result)
{
    if (!result.has_value())
    {
        std::cerr << "Fatal error: " << result.error() << std::endl;
        std::exit(1);
    }
}
