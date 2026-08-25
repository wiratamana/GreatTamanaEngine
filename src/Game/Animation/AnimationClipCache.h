#pragma once

#include "../../Assets/MotionData.h"

#include <string>
#include <unordered_map>

namespace gte {

// Replaces Game::EnsureAnimationClip()/m_animationClipCache - decodes (once
// per distinct absolute *.gta AssetType::Animation path, then cached) a
// motion clip's MotionData payload. Its own small class now (see
// GameInstantiationRefactorProposal.txt, Step 3.5), instead of a Game-owned
// private method/map pair.
class AnimationClipCache {
public:
    // Decodes/caches (if not already cached) and returns `absoluteAnimationGtaPath`'s
    // MotionData. Returns nullptr (never throws) if the file doesn't resolve
    // to a valid, non-empty *.gta AssetType::Animation file - missing file,
    // bad magic, or a corrupt/truncated payload - same failure contract as
    // the function this replaces.
    const MotionData* GetOrLoad(const std::string& absoluteAnimationGtaPath);

    // Read-only lookup that never attempts to load - returns nullptr if
    // `absoluteAnimationGtaPath` hasn't already been resolved via
    // GetOrLoad().
    const MotionData* TryGet(const std::string& absoluteAnimationGtaPath) const;

private:
    std::unordered_map<std::string, MotionData> m_cache;
};

} // namespace gte
