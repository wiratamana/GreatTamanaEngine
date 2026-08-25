#include "AnimationClipCache.h"

#include "../../Assets/AssetTypes.h"
#include "../../Assets/GtaFile.h"
#include "../../Assets/MotionFile.h"

#include <filesystem>
#include <optional>

namespace gte {

namespace {

// Same std::u8string round-trip Game.cpp's own Utf8PathFromGamePath() uses -
// see that function's doc comment for the full "why" (src/Game/ can't
// depend on src/Editor/'s ProjectPanelData.h, which has the canonical
// version of this helper).
std::filesystem::path Utf8PathFromGamePath(const std::string& utf8)
{
    return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(utf8.data()), utf8.size()));
}

} // namespace

const MotionData* AnimationClipCache::GetOrLoad(const std::string& absoluteAnimationGtaPath)
{
    if (const auto found = m_cache.find(absoluteAnimationGtaPath); found != m_cache.end()) {
        return &found->second;
    }

    const std::optional<GtaFileData> gta = ReadGtaFile(Utf8PathFromGamePath(absoluteAnimationGtaPath));
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
