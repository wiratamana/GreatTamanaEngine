#include "AnimationClipCache.h"

#include "../../Assets/AssetTypes.h"
#include "../../Assets/GtaFile.h"
#include "../../Assets/MotionFile.h"
#include "../../Encoding/Utf8Path.h"

#include <filesystem>
#include <optional>

namespace gte {

const MotionData* AnimationClipCache::GetOrLoad(const std::string& absoluteAnimationGtaPath)
{
    if (const auto found = m_cache.find(absoluteAnimationGtaPath); found != m_cache.end()) {
        return &found->second;
    }

    const std::optional<GtaFileData> gta = ReadGtaFile(PathFromUtf8(absoluteAnimationGtaPath));
    if (!gta.has_value() || gta->header.Type() != AssetType::Animation) {
        return nullptr; // Missing file, bad magic, or not an Animation asset.
    }

    std::optional<MotionData> motion = DecodeMotionDataFromBytes(gta->payload);
    if (!motion.has_value()) {
        return nullptr; // Corrupt/truncated payload.
    }

    const auto inserted = m_cache.emplace(absoluteAnimationGtaPath, std::move(*motion));
    return &inserted.first->second;
}

const MotionData* AnimationClipCache::TryGet(const std::string& absoluteAnimationGtaPath) const
{
    const auto found = m_cache.find(absoluteAnimationGtaPath);
    return found != m_cache.end() ? &found->second : nullptr;
}

} // namespace gte
