#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <unordered_set>

namespace gte::rg {

// Remembers up to `Capacity` distinct keys so a message can be logged once per key.
// Never grows past the cap; callers get one overflow notice instead of silent loss.
template <std::size_t Capacity>
class BoundedOnceSet {
public:
    enum class Result { First, Repeat, Overflow, OverflowRepeat };

    // First: new key, log it. Repeat: already seen. Overflow: set is full, log the overflow once.
    Result Note(std::string_view key)
    {
        if (m_keys.find(std::string(key)) != m_keys.end()) {
            return Result::Repeat;
        }
        if (m_keys.size() >= Capacity) {
            if (m_overflowReported) {
                return Result::OverflowRepeat;
            }
            m_overflowReported = true;
            return Result::Overflow;
        }
        m_keys.emplace(key);
        return Result::First;
    }
    // True for the results that deserve one log line (a new key, or the first overflow).
    static constexpr bool ShouldLog(Result result) noexcept
    {
        return result == Result::First || result == Result::Overflow;
    }

    std::size_t Size() const noexcept { return m_keys.size(); }

private:
    std::unordered_set<std::string> m_keys;
    bool m_overflowReported = false;
};

} // namespace gte::rg
