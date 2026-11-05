#pragma once

#include "BoneLocalOffset.h"
#include "BonePoseMath.h"
#include "BoneWorldMatrixQuery.h"
#include "../Assets/SkeletonData.h"
#include "../Math/Mat4.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace gte {

// Memoized bone world matrices for a pose that only changes through Invalidate().
// Every entry is the same product as ComputeBoneWorldMatrix(), so results match it bit for bit.
class BoneWorldCache {
public:
    // Marks every entry stale. Call once per pose snapshot, and whenever the skeleton changes.
    void Reset(std::size_t boneCount)
    {
        m_entries.resize(boneCount);
        for (Entry& entry : m_entries) {
            entry.valid = false;
        }
    }

    // Call after the local offset of `boneIndex` changed. Its descendants refresh themselves on the next Get().
    void Invalidate(std::int32_t boneIndex) noexcept
    {
        if (boneIndex >= 0 && static_cast<std::size_t>(boneIndex) < m_entries.size()) {
            m_entries[static_cast<std::size_t>(boneIndex)].valid = false;
        }
    }

    Mat4 Get(const SkeletonData& skeleton, const std::vector<BoneLocalOffset>& pose, std::int32_t boneIndex)
    {
        const std::size_t count = skeleton.bones.size();
        if (boneIndex < 0 || static_cast<std::size_t>(boneIndex) >= count) {
            return Mat4::Identity();
        }
        if (m_entries.size() != count) {
            Reset(count);
        }

        // Walk up into a fixed array; a cycle or a very deep chain falls back to the uncached query.
        constexpr std::size_t kMaxDepth = 128;
        std::size_t path[kMaxDepth];
        std::size_t depth = 0;
        for (std::size_t index = static_cast<std::size_t>(boneIndex);;) {
            path[depth++] = index;
            const std::int32_t next = skeleton.bones[index].parentBoneIndex;
            if (!(next >= 0 && static_cast<std::size_t>(next) < count && static_cast<std::size_t>(next) != index)) {
                break;
            }
            bool cycle = false;
            for (std::size_t i = 0; i < depth; ++i) {
                cycle = cycle || path[i] == static_cast<std::size_t>(next);
            }
            if (cycle) {
                break;
            }
            if (depth == kMaxDepth) {
                return ComputeBoneWorldMatrix(skeleton, pose, boneIndex);
            }
            index = static_cast<std::size_t>(next);
        }

        // Root first: an entry is fresh while its own offset and its parent's matrix are unchanged.
        const Entry* parent = nullptr;
        for (std::size_t i = depth; i-- > 0;) {
            Entry& entry = m_entries[path[i]];
            const std::uint32_t parentVersion = parent != nullptr ? parent->version : 0;
            if (!(entry.valid && entry.parentVersion == parentVersion)) {
                const BoneLocalOffset offset = path[i] < pose.size() ? pose[path[i]] : BoneLocalOffset{};
                const Mat4 parentWorld = parent != nullptr ? parent->world : Mat4::Identity();
                entry.world = parentWorld * ComputeBoneLocalMatrix(skeleton, path[i], offset);
                entry.version = ++m_counter;
                entry.parentVersion = parentVersion;
                entry.valid = true;
            }
            parent = &entry;
        }
        return parent->world;
    }

private:
    struct Entry {
        Mat4 world;
        std::uint32_t version = 0;
        std::uint32_t parentVersion = 0;
        bool valid = false;
    };

    std::vector<Entry> m_entries;
    std::uint32_t m_counter = 0;
};

} // namespace gte
