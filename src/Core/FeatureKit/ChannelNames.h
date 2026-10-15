#pragma once

// Kit-private: the channel names one feature module has used, to catch two names with one key hash.

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace gte::fx::detail {

class ChannelNames {
public:
    static constexpr std::size_t kCapacity = 64;

    enum class Result : std::uint8_t {
        Known, // The name was seen before.
        Added,
        Clash, // Another name already uses this hash.
        Full,  // Not tracked: the registry is full.
    };

    // `hash` is the view-independent key of "<feature>.<channel>". A known name allocates nothing.
    Result Note(std::uint64_t hash, std::string_view feature, std::string_view channel)
    {
        for (const Entry& entry : m_entries) {
            if (entry.hash == hash) {
                return entry.feature == feature && entry.channel == channel ? Result::Known : Result::Clash;
            }
        }
        if (m_entries.size() >= kCapacity) {
            return Result::Full;
        }
        m_entries.push_back(Entry{ hash, std::string(feature), std::string(channel) });
        return Result::Added;
    }

    std::size_t Size() const noexcept { return m_entries.size(); }

private:
    struct Entry {
        std::uint64_t hash;
        std::string feature;
        std::string channel;
    };

    std::vector<Entry> m_entries;
};

} // namespace gte::fx::detail
