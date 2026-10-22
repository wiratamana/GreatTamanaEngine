#pragma once

#include <cstddef>
#include <deque>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>

namespace gte::fx {

// Upper bound of the process-wide name table.
inline constexpr std::size_t kMaxInternedClusterNames = 4096;

// Append-only table of names with process lifetime. Strings never move, so the returned pointers stay valid.
class ClusterNamePool {
public:
    explicit ClusterNamePool(std::size_t capacity) noexcept
        : m_capacity(capacity)
    {
    }

    // The same text always returns the same pointer. Null when a new name would pass the capacity.
    const char* Intern(std::string_view name);

    std::size_t Size() const;

private:
    mutable std::mutex m_mutex;
    std::size_t m_capacity;
    std::deque<std::string> m_storage;
    std::unordered_map<std::string_view, const char*> m_index;
};

// The process-wide pool (kMaxInternedClusterNames entries).
const char* InternClusterName(std::string_view name);

} // namespace gte::fx
