#include "ClusterNamePool.h"

namespace gte::fx {

const char* ClusterNamePool::Intern(std::string_view name)
{
    if (name.empty()) {
        return nullptr;
    }
    const std::lock_guard<std::mutex> lock(m_mutex);
    if (const auto it = m_index.find(name); it != m_index.end()) {
        return it->second;
    }
    if (m_storage.size() >= m_capacity) {
        return nullptr;
    }
    const std::string& stored = m_storage.emplace_back(name);
    m_index.emplace(std::string_view(stored), stored.c_str());
    return stored.c_str();
}

std::size_t ClusterNamePool::Size() const
{
    const std::lock_guard<std::mutex> lock(m_mutex);
    return m_storage.size();
}

const char* InternClusterName(std::string_view name)
{
    static ClusterNamePool pool(kMaxInternedClusterNames);
    return pool.Intern(name);
}

} // namespace gte::fx
