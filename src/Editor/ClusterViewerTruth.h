#pragma once

// Ground-truth check of the cull: decodes the id image and compares it with the GPU survivor set.
// Pure: no ImGui, no GPU.

#include "../Core/CapturedPixels.h"
#include "../Renderer/ClusterViewRenderer.h"
#include "ClusterViewerSettings.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace gte {

// Result of decoding an id image (id + 1 in 24 bits, background 0).
struct ClusterIdDecode {
    bool ok = false;
    std::string error;
    std::vector<std::uint32_t> pixelCount; // pixels per cluster id
    std::uint64_t invalidIds = 0;          // pixels whose id is past the cluster count
    std::uint64_t pixelsChecked = 0;
};

// Reads channels by the image format (RGBA8 or BGRA8) and ignores alpha.
// Refuses an empty image, another format or a pixel vector smaller than width * height * 4.
ClusterIdDecode DecodeIdImage(const CapturedPixels& image, std::size_t clusterCount);

inline constexpr std::size_t kTruthMaxListedIds = 20;

struct ClusterTruthResult {
    bool ok = false;
    std::string error;
    std::uint32_t clusterCount = 0;
    std::uint32_t seen = 0;           // clusters with at least one pixel
    std::uint32_t falseCulls = 0;     // not kept but seen
    std::uint32_t conservative = 0;   // kept but not seen (wasted work only)
    std::uint32_t correct = 0;
    std::uint32_t unseen = 0;
    std::uint32_t culledAndUnseen = 0;
    double cullEfficiency = 0.0;      // culledAndUnseen / unseen, 0 when nothing is unseen
    std::vector<std::uint32_t> firstFalseCullIds; // at most kTruthMaxListedIds, ascending
};

// `survivors` are cluster ids of the GPU cull, `pixelCount` has one entry per cluster.
// A survivor id past the cluster count gives ok = false.
ClusterTruthResult CompareClusterTruth(std::span<const std::uint32_t> survivors, std::span<const std::uint32_t> pixelCount);

// Everything a verify result depends on. Any change makes the stored result stale.
struct VerifyKey {
    std::uint64_t poseHash = 0;
    std::uint32_t flags = 0;
    std::uint64_t hizCaptureId = 0;
    int width = 0;
    int height = 0;
    std::uint64_t dataGeneration = 0;

    friend bool operator==(const VerifyKey&, const VerifyKey&) noexcept = default;
};

// Bit hash of position, rotation and lens (negative zero equals zero).
std::uint64_t HashDebugPose(const DebugCameraState& camera) noexcept;

VerifyKey MakeVerifyKey(const DebugCameraState& camera, ClusterCullFlags flags, std::uint64_t hizCaptureId,
    int width, int height, std::uint64_t dataGeneration) noexcept;

inline bool IsStale(const VerifyKey& made, const VerifyKey& now) noexcept
{
    return !(made == now);
}

enum class HizVerdictKind : std::uint8_t {
    Clean,    // no false culls
    Bug,      // false culls with nothing that explains them
    HizStale, // false culls, Hi-Z pose or size differs from the verify camera (expected pop-in)
};

// False culls are expected when the Hi-Z capture was made at another pose or size than the verify run.
HizVerdictKind ClassifyFalseCulls(std::uint32_t falseCulls, bool occlusionApplied,
    const std::optional<ClusterViewHizInfo>& hiz, const Mat4& verifyViewProjection, const Vec3& verifyEye,
    int verifyWidth, int verifyHeight) noexcept;

} // namespace gte
