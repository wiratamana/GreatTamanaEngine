#pragma once

#include "BoundedOnceSet.h"

#include <cstddef>
#include <string_view>

namespace gte::rg {

// Logs a message once per distinct (level, category, text), so a bad pass declared every frame cannot flood the log.
// One instance per owner (the render graph); not thread-safe, the graph runs on one thread.
class OnceErrorLog {
public:
    void Error(std::string_view category, std::string_view text) { Log(false, category, text); }
    void Warning(std::string_view category, std::string_view text) { Log(true, category, text); }

private:
    void Log(bool warning, std::string_view category, std::string_view text);

    static constexpr std::size_t kCapacity = 128;
    BoundedOnceSet<kCapacity> m_seen;
};

} // namespace gte::rg
