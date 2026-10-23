#include "ClusterViewerTruth.h"

#include "../Encoding/CapturedPixelConversion.h"

#include <bit>
#include <cstring>

namespace gte {
namespace {

void HashFloat(std::uint64_t& hash, float value) noexcept
{
    value += 0.0f; // turns -0 into +0
    hash ^= std::bit_cast<std::uint32_t>(value);
    hash *= 1099511628211ull;
}

bool SameMatrix(const Mat4& a, const Mat4& b) noexcept
{
    return std::memcmp(a.Data(), b.Data(), sizeof(float) * 16) == 0;
}

} // namespace

ClusterIdDecode DecodeIdImage(const CapturedPixels& image, std::size_t clusterCount)
{
    ClusterIdDecode out;
    if (image.width <= 0 || image.height <= 0) {
        out.error = "id image is empty";
        return out;
    }
    const bool bgra = Encoding::IsBgraFormat(image.format);
    if (!bgra && image.format != PixelFormat::RGBA8_UNorm) {
        out.error = std::string("id image has an unsupported format: ") + ToString(image.format);
        return out;
    }
    const std::uint64_t pixelTotal = static_cast<std::uint64_t>(image.width) * static_cast<std::uint64_t>(image.height);
    if (image.pixels.size() < pixelTotal * 4) {
        out.error = "id image has fewer bytes than width * height * 4";
        return out;
    }

    const std::size_t redOffset = bgra ? 2 : 0;
    const std::size_t blueOffset = bgra ? 0 : 2;
    out.pixelCount.assign(clusterCount, 0);
    const std::uint8_t* p = image.pixels.data();
    for (std::uint64_t i = 0; i < pixelTotal; ++i, p += 4) {
        const std::uint32_t id = static_cast<std::uint32_t>(p[redOffset])
            | (static_cast<std::uint32_t>(p[1]) << 8) | (static_cast<std::uint32_t>(p[blueOffset]) << 16);
        if (id == 0) {
            continue; // background
        }
        if (id - 1 < clusterCount) {
            ++out.pixelCount[id - 1];
        } else {
            ++out.invalidIds;
        }
    }
    out.pixelsChecked = pixelTotal;
    out.ok = true;
    return out;
}

ClusterTruthResult CompareClusterTruth(std::span<const std::uint32_t> survivors, std::span<const std::uint32_t> pixelCount)
{
    ClusterTruthResult out;
    const std::size_t count = pixelCount.size();
    out.clusterCount = static_cast<std::uint32_t>(count);

    std::vector<std::uint8_t> kept(count, 0);
    for (const std::uint32_t id : survivors) {
        if (id >= count) {
            out.error = "survivor id " + std::to_string(id) + " is past the cluster count " + std::to_string(count);
            return out;
        }
        kept[id] = 1;
    }

    for (std::size_t i = 0; i < count; ++i) {
        const bool isKept = kept[i] != 0;
        const bool isSeen = pixelCount[i] > 0;
        out.seen += isSeen ? 1u : 0u;
        if (!isKept && isSeen) {
            ++out.falseCulls;
            if (out.firstFalseCullIds.size() < kTruthMaxListedIds) {
                out.firstFalseCullIds.push_back(static_cast<std::uint32_t>(i));
            }
        } else if (isKept && !isSeen) {
            ++out.conservative;
        }
        if (!isSeen) {
            ++out.unseen;
            if (!isKept) {
                ++out.culledAndUnseen;
            }
        }
    }
    out.correct = out.clusterCount - out.falseCulls - out.conservative;
    out.cullEfficiency = out.unseen == 0
        ? 0.0
        : static_cast<double>(out.culledAndUnseen) / static_cast<double>(out.unseen);
    out.ok = true;
    return out;
}

std::uint64_t HashDebugPose(const DebugCameraState& camera) noexcept
{
    std::uint64_t hash = 14695981039346656037ull;
    const Transform& t = camera.transform;
    for (const float v : { t.position.x, t.position.y, t.position.z, t.rotation.x, t.rotation.y, t.rotation.z,
             t.rotation.w, camera.fovYDegrees, camera.nearZ, camera.farZ, camera.aspect }) {
        HashFloat(hash, v);
    }
    return hash;
}

VerifyKey MakeVerifyKey(const DebugCameraState& camera, ClusterCullFlags flags, std::uint64_t hizCaptureId,
    int width, int height, std::uint64_t dataGeneration) noexcept
{
    VerifyKey key;
    key.poseHash = HashDebugPose(camera);
    key.flags = static_cast<std::uint32_t>(flags);
    key.hizCaptureId = hizCaptureId;
    key.width = width;
    key.height = height;
    key.dataGeneration = dataGeneration;
    return key;
}

HizVerdictKind ClassifyFalseCulls(std::uint32_t falseCulls, bool occlusionApplied,
    const std::optional<ClusterViewHizInfo>& hiz, const Mat4& verifyViewProjection, const Vec3& verifyEye,
    int verifyWidth, int verifyHeight) noexcept
{
    if (falseCulls == 0) {
        return HizVerdictKind::Clean;
    }
    if (occlusionApplied && hiz) {
        const bool samePose = SameMatrix(hiz->viewProjection, verifyViewProjection) && hiz->eye == verifyEye;
        const bool sameSize = hiz->width == verifyWidth && hiz->height == verifyHeight;
        if (!samePose || !sameSize) {
            return HizVerdictKind::HizStale;
        }
    }
    return HizVerdictKind::Bug;
}

} // namespace gte
