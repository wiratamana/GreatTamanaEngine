#pragma once

#include <cstddef>

namespace gte::rg {

// Pass names are stored as raw pointers, so only string literals may be used.
// A std::string or a runtime const char* does not convert; Interned() is the one explicit exception.
struct StaticName {
    const char* value;

    template <std::size_t N>
    consteval StaticName(const char (&literal)[N]) noexcept
        : value(literal)
    {
        static_assert(N > 1, "pass name must not be empty");
    }

    // For a name from a process-lifetime pool (it must never be freed or moved).
    static StaticName Interned(const char* poolName) noexcept { return StaticName(poolName, InternedTag{}); }

private:
    struct InternedTag {};
    StaticName(const char* pooled, InternedTag) noexcept
        : value(pooled)
    {
    }
};

} // namespace gte::rg
