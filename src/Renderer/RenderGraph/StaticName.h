#pragma once

#include <cstddef>

namespace gte::rg {

// Pass names are stored as raw pointers, so only string literals may be used.
// A std::string or a runtime const char* does not convert.
struct StaticName {
    const char* value;

    template <std::size_t N>
    consteval StaticName(const char (&literal)[N]) noexcept
        : value(literal)
    {
        static_assert(N > 1, "pass name must not be empty");
    }
};

} // namespace gte::rg
