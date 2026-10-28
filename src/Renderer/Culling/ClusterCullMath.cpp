#include "ClusterCullMath.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace gte {

namespace {

bool IsFiniteVec(const Vec3& v) noexcept
{
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

bool IsFiniteMat(const Mat4& m) noexcept
{
    const float* data = m.Data();
    for (int i = 0; i < 16; ++i) {
        if (!std::isfinite(data[i])) {
            return false;
        }
    }
    return true;
}

Vec3 Column3(const Mat4& m, int col) noexcept
{
    return Vec3(m(0, col), m(1, col), m(2, col));
}

// mat3(world) * v, the same product the shader takes.
Vec3 Mul3x3(const Mat4& m, const Vec3& v) noexcept
{
    return Vec3(m(0, 0) * v.x + m(0, 1) * v.y + m(0, 2) * v.z, m(1, 0) * v.x + m(1, 1) * v.y + m(1, 2) * v.z,
        m(2, 0) * v.x + m(2, 1) * v.y + m(2, 2) * v.z);
}

constexpr std::uint64_t kMaxU64 = ~std::uint64_t{ 0 };

std::uint64_t SaturatingMul(std::uint64_t a, std::uint64_t b) noexcept
{
    return (a != 0 && b > kMaxU64 / a) ? kMaxU64 : a * b;
}

std::uint64_t SaturatingAdd(std::uint64_t a, std::uint64_t b) noexcept
{
    return b > kMaxU64 - a ? kMaxU64 : a + b;
}

} // namespace

ClusterInstanceGpu MakeClusterInstanceGpu(const Mat4& world, std::uint32_t flags, float maxScale) noexcept
{
    ClusterInstanceGpu out{};
    std::memcpy(out.world, world.Data(), sizeof(out.world));
    out.info[0] = flags;
    out.info[1] = std::bit_cast<std::uint32_t>(maxScale);
    return out;
}

ClusterCullParams MakeClusterCullParams(const Mat4& cullViewProjection, const Vec3& eyeOrDirection,
    bool orthographic, std::uint32_t instanceCount, std::uint32_t clusterCount, ClusterCullFlags flags) noexcept
{
    ClusterCullParams params{};
    const std::array<Plane, 6> planes = ExtractFrustumPlanes(cullViewProjection);
    for (std::size_t i = 0; i < planes.size(); ++i) {
        params.planes[i][0] = planes[i].normal.x;
        params.planes[i][1] = planes[i].normal.y;
        params.planes[i][2] = planes[i].normal.z;
        params.planes[i][3] = planes[i].distance;
    }
    params.eyeOrDirection[0] = eyeOrDirection.x;
    params.eyeOrDirection[1] = eyeOrDirection.y;
    params.eyeOrDirection[2] = eyeOrDirection.z;
    params.counts[0] = instanceCount;
    params.counts[1] = clusterCount;
    params.counts[2] = static_cast<std::uint32_t>(
        std::min<std::uint64_t>(std::uint64_t{ instanceCount } * clusterCount, 0xFFFFFFFFull));
    params.counts[3] = static_cast<std::uint32_t>(flags) | (orthographic ? kClusterCullFlagOrthographic : 0u);
    return params;
}

bool ClusterCullParamsAreFinite(const ClusterCullParams& params) noexcept
{
    for (const auto& plane : params.planes) {
        for (float value : plane) {
            if (!std::isfinite(value)) {
                return false;
            }
        }
    }
    for (int i = 0; i < 3; ++i) {
        if (!std::isfinite(params.eyeOrDirection[i])) {
            return false;
        }
    }
    return true;
}

ClusterLimits ClusterLimitsFor(const IndirectDrawCapsView& caps) noexcept
{
    ClusterLimits limits;
    limits.maxInstances = kClusterMaxBufferBytes / sizeof(ClusterInstanceGpu);
    limits.maxWorkItems = std::min({ kMaxClusterDispatchGroups * kClusterCullGroupSize,
        std::uint64_t{ caps.maxDrawIndirectCount }, kClusterMaxBufferBytes / kClusterCommandBytes,
        kClusterMaxBufferBytes / kClusterVisibleBytes });
    limits.maxClusters = kClusterMaxBufferBytes / sizeof(MeshCluster);
    return limits;
}

ClusterPartVerdict EvaluateClusterPart(
    std::uint64_t instanceCount, std::uint64_t clusterCount, const ClusterLimits& limits) noexcept
{
    if (clusterCount > limits.maxClusters) {
        return ClusterPartVerdict::TooManyClusters;
    }
    if (instanceCount > limits.maxInstances) {
        return ClusterPartVerdict::TooManyInstances;
    }
    if (SaturatingMul(instanceCount, clusterCount) > limits.maxWorkItems) {
        return ClusterPartVerdict::TooManyWorkItems;
    }
    if (instanceCount == 0 || clusterCount == 0 || (clusterCount < 2 && instanceCount < 4)) {
        return ClusterPartVerdict::NotWorthIt;
    }
    return ClusterPartVerdict::Ok;
}

std::uint64_t ClusterBufferCapacity(std::uint64_t requestedBytes) noexcept
{
    std::uint64_t capacity = kClusterMinBufferBytes;
    while (capacity < requestedBytes && capacity < kClusterMaxBufferBytes) {
        capacity <<= 1;
    }
    return capacity;
}

std::uint64_t ClusterChainBytes(std::uint64_t instanceCount, std::uint64_t workItems) noexcept
{
    std::uint64_t total = ClusterBufferCapacity(SaturatingMul(instanceCount, sizeof(ClusterInstanceGpu)));
    total = SaturatingAdd(total, ClusterBufferCapacity(SaturatingMul(workItems, kClusterVisibleBytes)));
    total = SaturatingAdd(total, ClusterBufferCapacity(SaturatingMul(workItems, kClusterCommandBytes)));
    return SaturatingAdd(total, ClusterBufferCapacity(sizeof(std::uint32_t)));
}

bool ClusterDrawLedger::TryReserve(
    std::uint64_t owner, std::uint32_t scope, std::uint64_t bytes, std::uint64_t ceiling)
{
    Entry* entry = nullptr;
    for (Entry& candidate : m_entries) {
        if (candidate.owner == owner && candidate.scope == scope) {
            entry = &candidate;
            break;
        }
    }
    const std::uint64_t existing = entry != nullptr ? entry->bytes : 0;
    if (bytes <= existing) {
        return true;
    }
    if (!FitsClusterBudget(Total() - existing, bytes, ceiling)) {
        return false;
    }
    if (entry != nullptr) {
        entry->bytes = bytes;
    } else {
        m_entries.push_back(Entry{ owner, scope, bytes });
    }
    return true;
}

void ClusterDrawLedger::Release(std::uint64_t owner, std::uint32_t scope) noexcept
{
    std::erase_if(m_entries, [&](const Entry& e) { return e.owner == owner && e.scope == scope; });
}

void ClusterDrawLedger::ReleaseOwner(std::uint64_t owner) noexcept
{
    std::erase_if(m_entries, [&](const Entry& e) { return e.owner == owner; });
}

std::uint64_t ClusterDrawLedger::HeldBy(std::uint64_t owner, std::uint32_t scope) const noexcept
{
    for (const Entry& e : m_entries) {
        if (e.owner == owner && e.scope == scope) {
            return e.bytes;
        }
    }
    return 0;
}

std::uint64_t ClusterDrawLedger::Total() const noexcept
{
    std::uint64_t total = 0;
    for (const Entry& e : m_entries) {
        total = SaturatingAdd(total, e.bytes);
    }
    return total;
}

std::optional<ClusterRef> ClusterIdToInstanceCluster(
    std::uint32_t id, std::uint32_t clusterCount, std::uint32_t workItems) noexcept
{
    if (clusterCount == 0 || id >= workItems) {
        return std::nullopt;
    }
    const std::uint32_t instance = id / clusterCount;
    return ClusterRef{ instance, id - instance * clusterCount };
}

std::uint32_t ClusterInstanceFlagsFor(const Mat4& world, float* maxScaleOut) noexcept
{
    const Vec3 c0 = Column3(world, 0);
    const Vec3 c1 = Column3(world, 1);
    const Vec3 c2 = Column3(world, 2);
    const float l0 = Length(c0);
    const float l1 = Length(c1);
    const float l2 = Length(c2);
    const float maxLen = std::max({ l0, l1, l2 });

    std::uint32_t flags = 0;
    if (maxScaleOut != nullptr) {
        *maxScaleOut = std::isfinite(maxLen) ? maxLen : 0.0f;
    }
    if (!IsFiniteMat(world) || !(maxLen > 0.0f)) {
        return 0;
    }
    const float tolerance = 1e-3f * maxLen;
    if (std::abs(l0 - maxLen) <= tolerance && std::abs(l1 - maxLen) <= tolerance
        && std::abs(l2 - maxLen) <= tolerance) {
        flags |= kClusterFlagUniformScale;
    }
    if (Dot(c0, Cross(c1, c2)) > 0.0f) {
        flags |= kClusterFlagPositiveHand;
    }
    return flags;
}

bool ClusterFrustumCulled(const MeshCluster& cluster, const Mat4& world, const std::array<Plane, 6>& planes) noexcept
{
    AABB local;
    local.min = Vec3(cluster.aabbMin[0], cluster.aabbMin[1], cluster.aabbMin[2]);
    local.max = Vec3(cluster.aabbMax[0], cluster.aabbMax[1], cluster.aabbMax[2]);
    return IsAABBOutsideFrustum(planes, TransformAABB(local, world));
}

bool ClusterConeCulled(const MeshCluster& cluster, const Mat4& world, std::uint32_t instanceFlags,
    float maxScale, const Vec3& eyeOrDirection, bool orthographic) noexcept
{
    constexpr std::uint32_t kNeeded = kClusterFlagUniformScale | kClusterFlagPositiveHand;
    const float cutoff = cluster.cone[3];
    if ((instanceFlags & kNeeded) != kNeeded || !(cutoff < 1.0f)) {
        return false;
    }
    const Vec3 axisWorld = Normalize(Mul3x3(world, Vec3(cluster.cone[0], cluster.cone[1], cluster.cone[2])));
    if (orthographic) {
        return Dot(eyeOrDirection, axisWorld) >= cutoff;
    }
    const Vec3 centreWorld = world.TransformPoint(Vec3(cluster.sphere[0], cluster.sphere[1], cluster.sphere[2]));
    const Vec3 toCluster = centreWorld - eyeOrDirection;
    const float radiusWorld = cluster.sphere[3] * maxScale;
    return Dot(toCluster, axisWorld) >= cutoff * Length(toCluster) + radiusWorld;
}

bool ClusterCulled(const MeshCluster& cluster, const Mat4& world, std::uint32_t instanceFlags, float maxScale,
    ClusterCullFlags flags, const std::array<Plane, 6>& planes, const Vec3& eyeOrDirection,
    bool orthographic) noexcept
{
    if (HasClusterCullFlag(flags, ClusterCullFlags::Frustum) && ClusterFrustumCulled(cluster, world, planes)) {
        return true;
    }
    return HasClusterCullFlag(flags, ClusterCullFlags::BackfaceCone)
        && ClusterConeCulled(cluster, world, instanceFlags, maxScale, eyeOrDirection, orthographic);
}

bool IsOrthographicProjection(const Mat4& m) noexcept
{
    constexpr float kTolerance = 1e-4f;
    return std::abs(m(3, 0)) <= kTolerance && std::abs(m(3, 1)) <= kTolerance && std::abs(m(3, 2)) <= kTolerance
        && std::abs(m(3, 3) - 1.0f) <= kTolerance;
}

Vec3 OrthographicViewDirection(const Mat4& m) noexcept
{
    return Normalize(Vec3(m(2, 0), m(2, 1), m(2, 2)));
}

bool CameraIsFinite(const Mat4& viewProjection, const Vec3& eyeOrDirection) noexcept
{
    return IsFiniteMat(viewProjection) && IsFiniteVec(eyeOrDirection);
}

HizLayout HizLayoutFor(std::uint32_t sceneWidth, std::uint32_t sceneHeight) noexcept
{
    HizLayout layout;
    if (sceneWidth == 0 || sceneHeight == 0) {
        return layout;
    }
    const auto halfUp = [](std::uint32_t v) { return (v >> 1) + (v & 1u); };
    const std::uint32_t baseWidth = halfUp(sceneWidth);
    const std::uint32_t baseHeight = halfUp(sceneHeight);
    constexpr std::uint32_t kMaxBase = 1u << (kHizMaxLevels - 1);
    if (baseWidth > kMaxBase || baseHeight > kMaxBase) {
        return layout;
    }

    // Levels 0..L-1 where the top level is 1 x 1.
    const std::uint32_t largest = std::max(baseWidth, baseHeight);
    std::uint32_t levelCount = 1;
    while ((1u << (levelCount - 1)) < largest) {
        ++levelCount;
    }

    const auto ceilShift = [](std::uint32_t v, std::uint32_t k) { return std::max(1u, (v + ((1u << k) - 1u)) >> k); };
    std::uint32_t columnHeight = 0;
    for (std::uint32_t k = 0; k < levelCount; ++k) {
        HizLevel& level = layout.levels[k];
        level.width = ceilShift(baseWidth, k);
        level.height = ceilShift(baseHeight, k);
        if (k == 0) {
            continue;
        }
        level.originX = baseWidth;
        level.originY = columnHeight;
        columnHeight += level.height;
    }
    layout.baseWidth = baseWidth;
    layout.baseHeight = baseHeight;
    layout.levelCount = levelCount;
    layout.atlasWidth = baseWidth + (levelCount > 1 ? layout.levels[1].width : 0u);
    layout.atlasHeight = std::max(baseHeight, columnHeight);
    return layout;
}

std::uint32_t HizLevelForExtent(float extent, std::uint32_t levelCount) noexcept
{
    std::uint32_t level = 0;
    while (level + 1 < levelCount && static_cast<float>(1u << level) < extent) {
        ++level;
    }
    return level;
}

std::optional<HizRect> HizChooseRect(
    float minX, float minY, float maxX, float maxY, const HizLayout& layout) noexcept
{
    if (!layout.IsValid() || !std::isfinite(minX) || !std::isfinite(minY) || !std::isfinite(maxX)
        || !std::isfinite(maxY)) {
        return std::nullopt;
    }
    const float width = static_cast<float>(layout.baseWidth);
    const float height = static_cast<float>(layout.baseHeight);
    minX = std::clamp(minX - kHizRectMarginTexels, 0.0f, width);
    minY = std::clamp(minY - kHizRectMarginTexels, 0.0f, height);
    maxX = std::clamp(maxX + kHizRectMarginTexels, 0.0f, width);
    maxY = std::clamp(maxY + kHizRectMarginTexels, 0.0f, height);
    if (maxX < minX || maxY < minY) {
        return std::nullopt;
    }

    HizRect rect;
    rect.level = HizLevelForExtent(std::max(maxX - minX, maxY - minY), layout.levelCount);
    const HizLevel& info = layout.levels[rect.level];
    const float scale = 1.0f / static_cast<float>(1u << rect.level);
    const auto texel = [](float v, std::uint32_t size) {
        return std::min(static_cast<std::uint32_t>(std::max(v, 0.0f)), size - 1u);
    };
    rect.x0 = texel(std::floor(minX * scale), info.width);
    rect.y0 = texel(std::floor(minY * scale), info.height);
    rect.x1 = texel(std::floor(maxX * scale), info.width);
    rect.y1 = texel(std::floor(maxY * scale), info.height);
    return rect;
}

float HizFarthestDepth(std::span<const float> atlas, const HizLayout& layout, const HizRect& rect) noexcept
{
    if (!layout.IsValid() || rect.level >= layout.levelCount
        || atlas.size() < std::uint64_t{ layout.atlasWidth } * layout.atlasHeight) {
        return 1.0f;
    }
    const HizLevel& level = layout.levels[rect.level];
    float farthest = 0.0f;
    for (const std::uint32_t y : { rect.y0, rect.y1 }) {
        for (const std::uint32_t x : { rect.x0, rect.x1 }) {
            const std::size_t index = std::size_t{ level.originY + y } * layout.atlasWidth + level.originX + x;
            farthest = std::max(farthest, atlas[index]);
        }
    }
    return farthest;
}

std::vector<float> BuildHizPyramidReference(std::span<const float> sceneDepth, std::uint32_t sceneWidth,
    std::uint32_t sceneHeight, const HizLayout& layout)
{
    if (!layout.IsValid() || sceneDepth.size() < std::uint64_t{ sceneWidth } * sceneHeight) {
        return {};
    }
    std::vector<float> atlas(std::size_t{ layout.atlasWidth } * layout.atlasHeight, 0.0f);
    const auto at = [&](std::uint32_t x, std::uint32_t y) -> float& {
        return atlas[std::size_t{ y } * layout.atlasWidth + x];
    };

    for (std::uint32_t y = 0; y < layout.baseHeight; ++y) {
        for (std::uint32_t x = 0; x < layout.baseWidth; ++x) {
            float farthest = 0.0f;
            for (std::uint32_t dy = 0; dy < 2; ++dy) {
                for (std::uint32_t dx = 0; dx < 2; ++dx) {
                    const std::uint32_t sx = x * 2 + dx;
                    const std::uint32_t sy = y * 2 + dy;
                    if (sx < sceneWidth && sy < sceneHeight) {
                        farthest = std::max(farthest, sceneDepth[std::size_t{ sy } * sceneWidth + sx]);
                    }
                }
            }
            at(x, y) = farthest;
        }
    }
    for (std::uint32_t k = 1; k < layout.levelCount; ++k) {
        const HizLevel& below = layout.levels[k - 1];
        const HizLevel& level = layout.levels[k];
        for (std::uint32_t y = 0; y < level.height; ++y) {
            for (std::uint32_t x = 0; x < level.width; ++x) {
                float farthest = 0.0f;
                for (std::uint32_t dy = 0; dy < 2; ++dy) {
                    for (std::uint32_t dx = 0; dx < 2; ++dx) {
                        const std::uint32_t qx = x * 2 + dx;
                        const std::uint32_t qy = y * 2 + dy;
                        if (qx < below.width && qy < below.height) {
                            farthest = std::max(farthest, at(below.originX + qx, below.originY + qy));
                        }
                    }
                }
                at(level.originX + x, level.originY + y) = farthest;
            }
        }
    }
    return atlas;
}

HizDepthMap HizDepthMapOf(const Mat4& vp) noexcept
{
    if (!IsFiniteMat(vp) || IsOrthographicProjection(vp)) {
        return {};
    }
    // For a perspective matrix row 2 is k times row 3 (xyz part): clip.z = k * clip.w + b.
    const Vec3 r2(vp(2, 0), vp(2, 1), vp(2, 2));
    const Vec3 r3(vp(3, 0), vp(3, 1), vp(3, 2));
    const float rr = Dot(r3, r3);
    if (!(rr > 0.0f)) {
        return {};
    }
    const float k = Dot(r2, r3) / rr;
    const Vec3 residual = r2 - r3 * k;
    if (!std::isfinite(k) || !(Length(residual) <= 1e-4f * Length(r2))) {
        return {};
    }
    const float b = vp(2, 3) - k * vp(3, 3);
    if (!std::isfinite(b)) {
        return {};
    }
    return HizDepthMap{ false, k, b };
}

bool HizBehind(const HizDepthMap& map, float nearestW, float nearestZ, float farthest) noexcept
{
    if (!std::isfinite(nearestW) || !std::isfinite(nearestZ) || !std::isfinite(farthest)) {
        return false;
    }
    if (!(farthest < 1.0f)) {
        return false; // Clear depth: a pixel of the block was never drawn.
    }
    if (map.raw) {
        return nearestZ > farthest + kHizRawBias;
    }
    const float denominator = (farthest + kHizDepthGuard) - map.k;
    if (!(denominator * map.b > 0.0f)) {
        return false; // A depth at or beyond the far plane never occludes.
    }
    const float farthestW = map.b / denominator;
    return nearestW > farthestW * (1.0f + kHizRelMargin) + kHizAbsMargin;
}

bool ClusterOccluded(const MeshCluster& cluster, const Mat4& world, const Mat4& prevViewProjection,
    const HizLayout& layout, std::span<const float> atlas) noexcept
{
    if (!layout.IsValid()) {
        return false;
    }
    float minX = 0.0f;
    float minY = 0.0f;
    float maxX = 0.0f;
    float maxY = 0.0f;
    float nearestZ = 0.0f;
    float nearestW = 0.0f;
    for (int i = 0; i < 8; ++i) {
        const Vec3 local((i & 1) != 0 ? cluster.aabbMax[0] : cluster.aabbMin[0],
            (i & 2) != 0 ? cluster.aabbMax[1] : cluster.aabbMin[1],
            (i & 4) != 0 ? cluster.aabbMax[2] : cluster.aabbMin[2]);
        const Vec3 p = world.TransformPoint(local);
        float clip[4];
        for (int row = 0; row < 4; ++row) {
            clip[row] = prevViewProjection(row, 0) * p.x + prevViewProjection(row, 1) * p.y
                + prevViewProjection(row, 2) * p.z + prevViewProjection(row, 3);
        }
        if (!(clip[3] > 1e-6f)) {
            return false; // Behind the eye: no usable projection.
        }
        const float x = clip[0] / clip[3];
        const float y = clip[1] / clip[3];
        const float z = clip[2] / clip[3];
        if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) {
            return false;
        }
        minX = i == 0 ? x : std::min(minX, x);
        maxX = i == 0 ? x : std::max(maxX, x);
        minY = i == 0 ? y : std::min(minY, y);
        maxY = i == 0 ? y : std::max(maxY, y);
        nearestZ = i == 0 ? z : std::min(nearestZ, z);
        nearestW = i == 0 ? clip[3] : std::min(nearestW, clip[3]);
    }
    // Depth outside the rect is unknown, so a box that leaves the screen is kept.
    if (minX < -1.0f || maxX > 1.0f || minY < -1.0f || maxY > 1.0f) {
        return false;
    }
    const float width = static_cast<float>(layout.baseWidth);
    const float height = static_cast<float>(layout.baseHeight);
    const std::optional<HizRect> rect = HizChooseRect((minX * 0.5f + 0.5f) * width, (minY * 0.5f + 0.5f) * height,
        (maxX * 0.5f + 0.5f) * width, (maxY * 0.5f + 0.5f) * height, layout);
    if (!rect.has_value()) {
        return false;
    }
    return HizBehind(HizDepthMapOf(prevViewProjection), nearestW, nearestZ, HizFarthestDepth(atlas, layout, *rect));
}

void SetClusterCullHiz(ClusterCullParams& params, const Mat4& prevViewProjection, const HizLayout& layout) noexcept
{
    std::memcpy(params.prevViewProjection, prevViewProjection.Data(), sizeof(params.prevViewProjection));
    params.hiz[0] = static_cast<float>(layout.atlasWidth);
    params.hiz[1] = static_cast<float>(layout.atlasHeight);
    params.hiz[2] = static_cast<float>(layout.baseWidth);
    params.hiz[3] = static_cast<float>(layout.baseHeight);

    const HizDepthMap map = HizDepthMapOf(prevViewProjection);
    params.depthMap[0] = map.k;
    params.depthMap[1] = map.b;
    params.depthMap[2] = static_cast<float>(layout.levelCount);
    params.depthMap[3] = 0.0f;
    params.counts[3] = map.raw ? (params.counts[3] | kClusterCullFlagRawDepth) : (params.counts[3] & ~kClusterCullFlagRawDepth);
}

bool HizCameraCut(const Mat4& prevViewProjection, const Vec3& prevEye, const Mat4& viewProjection,
    const Vec3& eye) noexcept
{
    if (!CameraIsFinite(prevViewProjection, prevEye) || !CameraIsFinite(viewProjection, eye)) {
        return true;
    }
    if (Length(eye - prevEye) > kHizCutEyeDistance) {
        return true;
    }
    // The third row of a view-projection points along the view direction for both projection kinds.
    const Vec3 prevForward(prevViewProjection(2, 0), prevViewProjection(2, 1), prevViewProjection(2, 2));
    const Vec3 forward(viewProjection(2, 0), viewProjection(2, 1), viewProjection(2, 2));
    const float lengths = Length(prevForward) * Length(forward);
    if (!(lengths > 0.0f)) {
        return true;
    }
    return Dot(prevForward, forward) / lengths < kHizCutCosAngle;
}

HizValidity AdvanceHizValidity(
    HizValidity state, std::uint64_t frameIndex, std::uint32_t width, std::uint32_t height, bool cameraCut) noexcept
{
    const bool first = !state.hasBuilt;
    const bool missedFrame = state.hasBuilt && frameIndex != state.lastFrame + 1;
    const bool resized = state.hasBuilt && (width != state.width || height != state.height);

    bool resizeEvent = resized;
    if (resized) {
        state.resizeHold = kHizResizeHoldBuilds;
    } else if (state.resizeHold > 0) {
        resizeEvent = true;
        --state.resizeHold;
    }
    state.framesSinceValid = NextFramesSinceValid(state.framesSinceValid, resizeEvent, first, !missedFrame, cameraCut);
    state.hasBuilt = true;
    state.lastFrame = frameIndex;
    state.width = width;
    state.height = height;
    return state;
}

HizReason HizUnusableReason(const HizValidity& state, std::uint64_t frameIndex, std::uint32_t width, std::uint32_t height) noexcept
{
    if (!state.hasBuilt) {
        return HizReason::NoHistory;
    }
    if (state.lastFrame + 1 != frameIndex) {
        return HizReason::BuildOff;
    }
    if (state.width != width || state.height != height) {
        return HizReason::Resized;
    }
    if (!OcclusionAllowed(state.framesSinceValid, false, false, true, false)) {
        return HizReason::WarmingUp;
    }
    return HizReason::None;
}

bool InstanceMoved(const Mat4& previous, const Mat4& current) noexcept
{
    const float* a = previous.Data();
    const float* b = current.Data();
    for (int i = 0; i < 16; ++i) {
        if (!(a[i] == b[i])) {
            return true;
        }
    }
    return false;
}

bool MeshHasCoincidentTriangles(std::span<const Vec3> positions, std::span<const std::uint32_t> indices)
{
    using Point = std::array<float, 3>;
    using Triple = std::array<Point, 3>;

    std::vector<Triple> triples;
    triples.reserve(indices.size() / 3);
    for (std::size_t i = 0; i + 2 < indices.size(); i += 3) {
        if (indices[i] >= positions.size() || indices[i + 1] >= positions.size() || indices[i + 2] >= positions.size()) {
            continue;
        }
        Triple triple;
        for (std::size_t k = 0; k < 3; ++k) {
            const Vec3& p = positions[indices[i + k]];
            // Adding 0.0f turns -0.0 into +0.0 so both compare equal.
            triple[k] = Point{ p.x + 0.0f, p.y + 0.0f, p.z + 0.0f };
        }
        std::sort(triple.begin(), triple.end());
        triples.push_back(triple);
    }
    std::sort(triples.begin(), triples.end());
    return std::adjacent_find(triples.begin(), triples.end()) != triples.end();
}

std::optional<MeshCluster> SynthesizeSingleCluster(
    const LocalBoundsView& bounds, std::size_t indexCount, std::string* why)
{
    const auto refuse = [why](const char* reason) -> std::optional<MeshCluster> {
        if (why != nullptr) {
            *why = reason;
        }
        return std::nullopt;
    };
    if (indexCount == 0 || indexCount % 3 != 0) {
        return refuse("index count is zero or not a multiple of 3");
    }
    if (indexCount > 0xFFFFFFFFull) {
        return refuse("index count does not fit 32 bits");
    }
    if (!bounds.valid) {
        return refuse("mesh has no local bounds");
    }
    for (int axis = 0; axis < 3; ++axis) {
        if (!std::isfinite(bounds.min[axis]) || !std::isfinite(bounds.max[axis]) || bounds.min[axis] > bounds.max[axis]) {
            return refuse("local bounds are not finite or inverted");
        }
    }

    MeshCluster cluster{};
    float radiusSquared = 0.0f;
    for (int axis = 0; axis < 3; ++axis) {
        cluster.aabbMin[axis] = bounds.min[axis];
        cluster.aabbMax[axis] = bounds.max[axis];
        const float centre = 0.5f * (bounds.min[axis] + bounds.max[axis]);
        const float half = 0.5f * (bounds.max[axis] - bounds.min[axis]);
        cluster.sphere[axis] = centre;
        radiusSquared += half * half;
    }
    // A touch above the half diagonal so rounding never leaves a corner outside the sphere.
    cluster.sphere[3] = std::sqrt(radiusSquared) * (1.0f + 1e-6f);
    cluster.cone[3] = 1.0f; // Axis stays zero: no cone.
    cluster.firstIndex = 0;
    cluster.indexCount = static_cast<std::uint32_t>(indexCount);
    if (!std::isfinite(cluster.sphere[3])) {
        return refuse("local bounds are too large");
    }
    return cluster;
}

SynthesisBackoff NoteSynthesisFailure(SynthesisBackoff state, std::uint64_t frame) noexcept
{
    const std::uint32_t shift = std::min<std::uint32_t>(state.failures, 6);
    const std::uint32_t wait = std::min(kSynthesisRetryBaseFrames << shift, kSynthesisRetryMaxFrames);
    if (state.failures < 0xFFFFFFFFu) {
        ++state.failures;
    }
    state.retryAtFrame = frame + wait;
    return state;
}

} // namespace gte
