#pragma once

#include "BoundedOnceSet.h"

#include <cstddef>
#include <string_view>

#ifndef GTE_STRICT_RENDER_VALIDATION
#define GTE_STRICT_RENDER_VALIDATION 0
#endif

namespace gte::rg {

// True in a build made with GTE_STRICT_RENDER_VALIDATION: GPU tests then fail on any new validation error.
constexpr bool StrictValidationEnabled() noexcept
{
    return GTE_STRICT_RENDER_VALIDATION != 0;
}

// Logs an error once per distinct (category, text), so a bad pass declared every frame cannot flood the log.
// One instance per owner (the render graph); not thread-safe, the graph runs on one thread.
class OnceErrorLog {
public:
    void Error(std::string_view category, std::string_view text);

    // Messages that were new when reported (the first occurrence of each text, plus any past the capacity).
    std::size_t StrictViolationCount() const noexcept { return m_newMessages; }

private:
    static constexpr std::size_t kCapacity = 128;
    BoundedOnceSet<kCapacity> m_seen;
    std::size_t m_newMessages = 0;
};

} // namespace gte::rg
