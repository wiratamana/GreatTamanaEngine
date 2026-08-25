#pragma once

#include "../../Animation/MotionSampler.h"
#include "../../Assets/MotionData.h"
#include "../../Assets/SkeletonData.h"

#include <cstddef>
#include <functional>
#include <string>
#include <unordered_map>

namespace gte {

// A proper struct key for ResolvedAnimationBindingCache below - replaces the
// old fragile `meshGtaPath + '\x1F' + animationGtaPath` hand-concatenated
// string key (see GameInstantiationRefactorProposal.txt, Step 2.5/3.5),
// which relied on the (usually-true-but-never-guaranteed) assumption that
// neither path could legitimately contain the ASCII Unit-Separator
// character. A real struct with its own hash/equality can never be fooled
// that way - two DIFFERENT (meshPath, animationPath) pairs can never
// collide into looking like the same key, unlike a naive string join.
struct AnimationBindingKey {
    std::string meshGtaPath;
    std::string animationGtaPath;

    friend bool operator==(const AnimationBindingKey& a, const AnimationBindingKey& b) noexcept
    {
        return a.meshGtaPath == b.meshGtaPath && a.animationGtaPath == b.animationGtaPath;
    }
    friend bool operator!=(const AnimationBindingKey& a, const AnimationBindingKey& b) noexcept { return !(a == b); }
};

} // namespace gte

// The std::hash specialization must be visible BEFORE the first
// std::unordered_map<AnimationBindingKey, ...> instantiation below (the
// exact same ordering constraint AssetTypes.h's own Guid/std::hash<Guid>
// pair already follows) - hence closing/reopening the gte namespace here
// rather than defining ResolvedAnimationBindingCache first.
namespace std {
template <> struct hash<gte::AnimationBindingKey> {
    std::size_t operator()(const gte::AnimationBindingKey& key) const noexcept
    {
        const std::size_t h1 = std::hash<std::string>{}(key.meshGtaPath);
        const std::size_t h2 = std::hash<std::string>{}(key.animationGtaPath);
        return h1 ^ (h2 + 0x9e3779b97f4a7c15ull + (h1 << 6) + (h1 >> 2));
    }
};
} // namespace std

namespace gte {

// Replaces Game::m_resolvedAnimationBindingCache - caches the bone-NAME
// resolution between one specific model's skeleton and one specific
// motion's own bone tracks (Animation/MotionSampler.h's
// ResolveBoneTracksToSkeleton(), which is comparatively expensive - it walks
// every bone/track once), computed once per distinct (mesh, animation) pair
// and reused every frame afterwards by every SkeletalAnimator playing that
// same combination.
class ResolvedAnimationBindingCache {
public:
    // Returns the cached binding for `key` if one already exists; otherwise
    // computes it (via ResolveBoneTracksToSkeleton(skeleton, motion)),
    // caches it, and returns the freshly-cached value.
    const ResolvedAnimationBinding& GetOrCompute(
        const AnimationBindingKey& key, const SkeletonData& skeleton, const MotionData& motion)
    {
        auto found = m_cache.find(key);
        if (found == m_cache.end()) {
            found = m_cache.emplace(key, ResolveBoneTracksToSkeleton(skeleton, motion)).first;
        }
        return found->second;
    }

    const ResolvedAnimationBinding* TryGet(const AnimationBindingKey& key) const
    {
        const auto found = m_cache.find(key);
        return found != m_cache.end() ? &found->second : nullptr;
    }

private:
    std::unordered_map<AnimationBindingKey, ResolvedAnimationBinding> m_cache;
};

} // namespace gte
