#pragma once

// Pure, Vulkan-free math of the cluster cull path: GPU record layouts, limits, budgets and the CPU oracle
// of ClusterCull.comp. When the shader and this file disagree, this file is right.

#include "../../Assets/MeshCluster.h"
#include "../../Math/Mat4.h"
#include "../../Math/Vec3.h"
#include "ClusterPassCounts.h"
#include "CullingTypes.h"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace gte {

// ---------------------------------------------------------------------------------------------
// Device capabilities
// ---------------------------------------------------------------------------------------------

// Indirect draw capabilities as plain data (no Vulkan types).
struct IndirectDrawCapsView {
    bool drawIndirectCount = false;
    bool multiDrawIndirect = false;
    bool drawIndirectFirstInstance = false;
    std::uint32_t maxDrawIndirectCount = 0;
};

// The cluster path needs all three features and a non-zero draw count limit.
constexpr bool ClusterPathSupported(const IndirectDrawCapsView& caps) noexcept
{
    return caps.drawIndirectCount && caps.multiDrawIndirect && caps.drawIndirectFirstInstance
        && caps.maxDrawIndirectCount > 0;
}

// ---------------------------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------------------------

// Bits of ClusterInstanceGpu::info[0]. Mirrored by name in ClusterCull.comp.
inline constexpr std::uint32_t kClusterFlagUniformScale = 1u << 0;
inline constexpr std::uint32_t kClusterFlagPositiveHand = 1u << 1;
inline constexpr std::uint32_t kClusterFlagMoved = 1u << 2;

// Bits of ClusterCullParams::counts[3]. Mirrored by name in ClusterCull.comp.
inline constexpr std::uint32_t kClusterCullFlagFrustum = 1u << 0;
inline constexpr std::uint32_t kClusterCullFlagBackfaceCone = 1u << 1;
inline constexpr std::uint32_t kClusterCullFlagOcclusion = 1u << 2;
inline constexpr std::uint32_t kClusterCullFlagOrthographic = 1u << 3; // Set from the camera, never by a caller.
inline constexpr std::uint32_t kClusterCullFlagRawDepth = 1u << 8; // Set by SetClusterCullHiz when no depth map exists.

inline constexpr std::uint32_t kClusterCullGroupSize = 64;
inline constexpr std::uint64_t kMaxClusterDispatchGroups = 65535;

// One storage binding must always be legal: the smallest maxStorageBufferRange a device may report.
inline constexpr std::uint64_t kClusterMaxBufferBytes = std::uint64_t{ 128 } << 20;
inline constexpr std::uint64_t kClusterMinBufferBytes = 256;

inline constexpr std::uint64_t kClusterDrawBudgetBytes = std::uint64_t{ 256 } << 20;
inline constexpr std::uint64_t kClusterDrawProcessBudgetBytes = std::uint64_t{ 512 } << 20;
inline constexpr std::uint32_t kClusterIdleReleaseFrames = 600;

// Lane, slot and pass-name counts live in ClusterPassCounts.h.
// Bytes per work item: one indirect command (5 uints) and one visible record (uvec2).
inline constexpr std::uint64_t kClusterCommandBytes = 20;
inline constexpr std::uint64_t kClusterVisibleBytes = 8;

// Which clusters are tested.
enum class ClusterCullFlags : std::uint32_t {
    None = 0,
    Frustum = kClusterCullFlagFrustum,
    BackfaceCone = kClusterCullFlagBackfaceCone,
    Occlusion = kClusterCullFlagOcclusion,
};

constexpr ClusterCullFlags operator|(ClusterCullFlags a, ClusterCullFlags b) noexcept
{
    return static_cast<ClusterCullFlags>(static_cast<std::uint32_t>(a) | static_cast<std::uint32_t>(b));
}

constexpr bool HasClusterCullFlag(ClusterCullFlags flags, ClusterCullFlags bit) noexcept
{
    return (static_cast<std::uint32_t>(flags) & static_cast<std::uint32_t>(bit)) != 0;
}

// ---------------------------------------------------------------------------------------------
// GPU record layouts (std430)
// ---------------------------------------------------------------------------------------------

// One instance of a part dispatch. info[0] = flags, info[1] = float bits of the largest axis scale.
struct ClusterInstanceGpu {
    float world[16];
    std::uint32_t info[4];
};
static_assert(sizeof(ClusterInstanceGpu) == 80);
static_assert(offsetof(ClusterInstanceGpu, info) == 64);

// Parameter block of one cull dispatch. counts = {instanceCount, clusterCount, workItems, cull flags}.
struct ClusterCullParams {
    float planes[6][4]; // xyz = normal, w = distance; inside when dot + w >= 0.
    float eyeOrDirection[4]; // xyz = eye point, or view direction when orthographic.
    float prevViewProjection[16];
    std::uint32_t counts[4];
    float hiz[4]; // x,y = atlas size, z,w = level 0 size (Hi-Z occlusion only).
    float depthMap[4]; // x = k, y = b of d = k + b / w, z = pyramid level count (Hi-Z occlusion only).
};
static_assert(sizeof(ClusterCullParams) == 224);
static_assert(sizeof(ClusterCullParams) % 16 == 0);
static_assert(offsetof(ClusterCullParams, eyeOrDirection) == 96);
static_assert(offsetof(ClusterCullParams, prevViewProjection) == 112);
static_assert(offsetof(ClusterCullParams, counts) == 176);
static_assert(offsetof(ClusterCullParams, hiz) == 192);
static_assert(offsetof(ClusterCullParams, depthMap) == 208);

// Packs a world matrix and its flags into the GPU record.
ClusterInstanceGpu MakeClusterInstanceGpu(const Mat4& world, std::uint32_t flags, float maxScale) noexcept;

// Fills the parameter block. Planes come from `cullViewProjection`; the Hi-Z fields stay zero until SetClusterCullHiz.
ClusterCullParams MakeClusterCullParams(const Mat4& cullViewProjection, const Vec3& eyeOrDirection,
    bool orthographic, std::uint32_t instanceCount, std::uint32_t clusterCount, ClusterCullFlags flags) noexcept;

// False when any plane or the eye holds a non-finite value.
bool ClusterCullParamsAreFinite(const ClusterCullParams& params) noexcept;

// ---------------------------------------------------------------------------------------------
// Limits and budgets (all 64-bit)
// ---------------------------------------------------------------------------------------------

struct ClusterLimits {
    std::uint64_t maxInstances = 0;
    std::uint64_t maxWorkItems = 0; // instanceCount * clusterCount of one part dispatch.
    std::uint64_t maxClusters = 0;
};

ClusterLimits ClusterLimitsFor(const IndirectDrawCapsView& caps) noexcept;

enum class ClusterPartVerdict : std::uint8_t {
    Ok,
    TooManyInstances,
    TooManyWorkItems,
    TooManyClusters,
    NotWorthIt, // Fewer than 2 clusters and fewer than 4 instances: the CPU draw is cheaper.
};

ClusterPartVerdict EvaluateClusterPart(
    std::uint64_t instanceCount, std::uint64_t clusterCount, const ClusterLimits& limits) noexcept;

// Buffer capacity for a request: next power of two, at least the minimum, at most the maximum.
std::uint64_t ClusterBufferCapacity(std::uint64_t requestedBytes) noexcept;

// Capacity bytes of one chain: instances, visible, commands and count buffers, each rounded up.
std::uint64_t ClusterChainBytes(std::uint64_t instanceCount, std::uint64_t workItems) noexcept;

// True when `held + add` stays within `ceiling`. Never overflows.
constexpr bool FitsClusterBudget(std::uint64_t held, std::uint64_t add, std::uint64_t ceiling) noexcept
{
    return held <= ceiling && add <= ceiling - held;
}

// True when a scope with no chain for this many frames should drop its buffers.
constexpr bool ShouldReleaseIdleScope(std::uint32_t framesSinceLastChain) noexcept
{
    return framesSinceLastChain >= kClusterIdleReleaseFrames;
}

// Process-wide record of capacity bytes held per (owner, scope). Main thread only.
class ClusterDrawLedger {
public:
    // Raises the entry of (owner, scope) to `bytes` unless the total would pass `ceiling`.
    bool TryReserve(std::uint64_t owner, std::uint32_t scope, std::uint64_t bytes, std::uint64_t ceiling);
    void Release(std::uint64_t owner, std::uint32_t scope) noexcept;
    void ReleaseOwner(std::uint64_t owner) noexcept;
    std::uint64_t HeldBy(std::uint64_t owner, std::uint32_t scope) const noexcept;
    std::uint64_t Total() const noexcept;

private:
    struct Entry {
        std::uint64_t owner;
        std::uint32_t scope;
        std::uint64_t bytes;
    };
    std::vector<Entry> m_entries;
};

// ---------------------------------------------------------------------------------------------
// CPU oracle of ClusterCull.comp
// ---------------------------------------------------------------------------------------------

struct ClusterRef {
    std::uint32_t instance;
    std::uint32_t cluster;
};

// Maps a global work id to (instance, cluster). Empty when the id is past `workItems`.
std::optional<ClusterRef> ClusterIdToInstanceCluster(
    std::uint32_t id, std::uint32_t clusterCount, std::uint32_t workItems) noexcept;

// Per-instance flags and largest axis scale of a world matrix. A non-finite matrix gives flags 0.
std::uint32_t ClusterInstanceFlagsFor(const Mat4& world, float* maxScaleOut) noexcept;

// Frustum test of the cluster box moved to world space. A non-finite box is kept.
bool ClusterFrustumCulled(const MeshCluster& cluster, const Mat4& world, const std::array<Plane, 6>& planes) noexcept;

// Back-face cone test. Cutoff is the sine of the cone half angle. Off for cutoff 1 and for instances
// that are not uniformly scaled and right handed.
bool ClusterConeCulled(const MeshCluster& cluster, const Mat4& world, std::uint32_t instanceFlags,
    float maxScale, const Vec3& eyeOrDirection, bool orthographic) noexcept;

// True when the cluster is rejected by any test enabled in `flags`.
bool ClusterCulled(const MeshCluster& cluster, const Mat4& world, std::uint32_t instanceFlags, float maxScale,
    ClusterCullFlags flags, const std::array<Plane, 6>& planes, const Vec3& eyeOrDirection,
    bool orthographic) noexcept;

// Orthographic when the last row is (0, 0, 0, 1) within 1e-4.
bool IsOrthographicProjection(const Mat4& viewProjection) noexcept;

// View direction of an orthographic view-projection: its normalised third row.
Vec3 OrthographicViewDirection(const Mat4& viewProjection) noexcept;

// False when the matrix or the eye/direction holds a non-finite value.
bool CameraIsFinite(const Mat4& viewProjection, const Vec3& eyeOrDirection) noexcept;

// ---------------------------------------------------------------------------------------------
// Hi-Z occlusion
// ---------------------------------------------------------------------------------------------

// The pyramid is one R32_Float atlas of farthest-depth levels. Level 0 is half the scene depth size and every
// level halves the one below, down to a 1 x 1 top level. Texels outside the scene hold 0 (neutral for max).

// Clean builds in a row needed before the pyramid may cull (first frame, resize, cut and a missed build reset it).
inline constexpr std::uint32_t kHizValidFramesRequired = 2;

// The projected rect grows by this many level-0 texels on every side.
inline constexpr float kHizRectMarginTexels = 1.0f;

// Occlusion margins on view depth (perspective) and on raw depth (no depth map). Mirrored in ClusterCull.comp.
inline constexpr float kHizRelMargin = 0.002f;
inline constexpr float kHizAbsMargin = 0.01f;
inline constexpr float kHizDepthGuard = 4.8e-7f; // About eight depth steps near 1.0.
inline constexpr float kHizRawBias = 0.001f;

// A view that jumps farther than this, or turns more than acos(kHizCutCosAngle), is a camera cut.
inline constexpr float kHizCutEyeDistance = 50.0f;
inline constexpr float kHizCutCosAngle = 0.9f;

// Largest pyramid: level 0 up to 8192 texels per side (scene depth up to 16384), levels 0..13.
inline constexpr std::uint32_t kHizMaxLevels = 14;

struct HizLevel {
    std::uint32_t originX = 0;
    std::uint32_t originY = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
};

struct HizLayout {
    std::uint32_t baseWidth = 0; // Level 0 size.
    std::uint32_t baseHeight = 0;
    std::uint32_t atlasWidth = 0;
    std::uint32_t atlasHeight = 0;
    std::uint32_t levelCount = 0;
    std::array<HizLevel, kHizMaxLevels> levels{};

    bool IsValid() const noexcept { return levelCount != 0 && atlasWidth != 0 && atlasHeight != 0; }
};

// Level 0 at the origin, levels 1.. stacked in the column to its right. The top level is 1 x 1.
// A zero scene size or a scene side above 16384 gives an invalid layout.
HizLayout HizLayoutFor(std::uint32_t sceneWidth, std::uint32_t sceneHeight) noexcept;

// Smallest level whose texel size 2^level covers `extent` level-0 texels, capped at the top level.
std::uint32_t HizLevelForExtent(float extent, std::uint32_t levelCount) noexcept;

// Chosen level and inclusive texel rectangle of a screen rect.
struct HizRect {
    std::uint32_t level = 0;
    std::uint32_t x0 = 0;
    std::uint32_t y0 = 0;
    std::uint32_t x1 = 0;
    std::uint32_t y1 = 0;
};

// The rect is given in level-0 texels. The margin is added here. The rect spans at most two texels per axis.
// Empty for an invalid layout or a non-finite rect.
std::optional<HizRect> HizChooseRect(
    float minX, float minY, float maxX, float maxY, const HizLayout& layout) noexcept;

// Farthest depth of the texels of `rect`. `atlas` is row major, atlasWidth * atlasHeight floats.
// An atlas that is too small gives 1.0 (never occludes).
float HizFarthestDepth(std::span<const float> atlas, const HizLayout& layout, const HizRect& rect) noexcept;

// CPU reference of the whole pyramid: farthest depth per level texel, 0 outside the scene.
// `sceneDepth` is row major, sceneWidth * sceneHeight floats. Empty for an invalid layout or a short input.
std::vector<float> BuildHizPyramidReference(std::span<const float> sceneDepth, std::uint32_t sceneWidth,
    std::uint32_t sceneHeight, const HizLayout& layout);

// Depth d = k + b / w for a perspective view-projection, where w is the view distance. Raw when the
// matrix is orthographic or skewed: depth is then compared directly.
struct HizDepthMap {
    bool raw = true;
    float k = 0.0f;
    float b = 0.0f;
};

HizDepthMap HizDepthMapOf(const Mat4& viewProjection) noexcept;

// True when a box (nearest view distance `nearestW`, nearest depth `nearestZ`) lies behind a pyramid texel of
// farthest depth `farthest`. Never true for clear depth (1.0), depth beyond the far plane or non-finite input.
bool HizBehind(const HizDepthMap& map, float nearestW, float nearestZ, float farthest) noexcept;

// CPU oracle of the occlusion test in ClusterCull.comp. The box is projected with last frame's matrix.
// Kept (false) when any corner is behind the eye or the rect leaves the screen.
bool ClusterOccluded(const MeshCluster& cluster, const Mat4& world, const Mat4& prevViewProjection,
    const HizLayout& layout, std::span<const float> atlas) noexcept;

// Writes the previous view-projection, the pyramid size, the depth map and the raw-depth flag into the block.
void SetClusterCullHiz(ClusterCullParams& params, const Mat4& prevViewProjection, const HizLayout& layout) noexcept;

// Occlusion may cull only after enough clean builds and never on a frame with a reset event.
constexpr bool OcclusionAllowed(std::uint32_t framesSinceValid, bool resized, bool firstFrame, bool buildPassEnabled,
    bool cameraCut) noexcept
{
    return buildPassEnabled && !resized && !firstFrame && !cameraCut && framesSinceValid >= kHizValidFramesRequired;
}

// Clean-build counter after one pyramid build. Any reset event restarts it at zero.
constexpr std::uint32_t NextFramesSinceValid(std::uint32_t framesSinceValid, bool resized, bool firstFrame,
    bool buildPassEnabled, bool cameraCut) noexcept
{
    if (!buildPassEnabled || resized || firstFrame || cameraCut) {
        return 0;
    }
    return framesSinceValid < 0xFFFFFFFFu ? framesSinceValid + 1 : framesSinceValid;
}

// A persistent target resizes one frame after the request, so the build after a resize is held back as well.
inline constexpr std::uint32_t kHizResizeHoldBuilds = 1;

// Per-view bookkeeping of the pyramid builds. Plain data, advanced once per build.
struct HizValidity {
    bool hasBuilt = false;
    std::uint64_t lastFrame = 0; // Frame index of the last build.
    std::uint32_t width = 0;     // Scene depth size of the last build.
    std::uint32_t height = 0;
    std::uint32_t framesSinceValid = 0;
    std::uint32_t resizeHold = 0;
};

// State after a build at `frameIndex` for a scene depth of `width` x `height`. The first build, a resize, a
// camera cut and a missed frame between two builds all restart the clean count.
HizValidity AdvanceHizValidity(
    HizValidity state, std::uint64_t frameIndex, std::uint32_t width, std::uint32_t height, bool cameraCut) noexcept;

// Why last frame's pyramid may not cull. None means usable.
enum class HizReason : std::uint8_t { None, NoHistory, Resized, CameraCut, WarmingUp, BuildOff, Unavailable, TooLarge };

// Reason the build history cannot cull in `frameIndex` for a view of `width` x `height`; None when it can.
// A missing build in the previous frame reports BuildOff. Camera cuts are judged by the caller (HizCameraCut).
HizReason HizUnusableReason(const HizValidity& state, std::uint64_t frameIndex, std::uint32_t width, std::uint32_t height) noexcept;

// True when the view moved or turned too far for last frame's depth to say anything about this frame.
bool HizCameraCut(const Mat4& prevViewProjection, const Vec3& prevEye, const Mat4& viewProjection,
    const Vec3& eye) noexcept;

// True when any entry of the two world matrices differs (a NaN entry counts as different).
bool InstanceMoved(const Mat4& previous, const Mat4& current) noexcept;

// ---------------------------------------------------------------------------------------------
// Pipeline key
// ---------------------------------------------------------------------------------------------

enum class ClusterPipelineVariant : std::uint8_t { Lit, IdTint, DepthOnly };

struct ClusterPipelineKey {
    ClusterPipelineVariant variant = ClusterPipelineVariant::Lit;
    bool cullBackFaces = false;

    friend constexpr bool operator==(const ClusterPipelineKey&, const ClusterPipelineKey&) noexcept = default;
};

// Number of distinct pipeline keys (3 variants x 2 cull modes).
inline constexpr std::size_t kClusterPipelineKeyCount = 6;

// Dense index of a key in [0, kClusterPipelineKeyCount), for a fixed-size pipeline cache.
constexpr std::size_t ClusterPipelineCacheIndex(const ClusterPipelineKey& key) noexcept
{
    return static_cast<std::size_t>(key.variant) * 2 + (key.cullBackFaces ? 1 : 0);
}

// The cone test is dropped for a depth-only lane, which draws both sides.
constexpr ClusterCullFlags EffectiveClusterCullFlags(ClusterCullFlags flags, bool depthOnly) noexcept
{
    const std::uint32_t bits = static_cast<std::uint32_t>(flags);
    return static_cast<ClusterCullFlags>(depthOnly ? (bits & ~kClusterCullFlagBackfaceCone) : bits);
}

// The cone test and the back-face-culling pipeline are one decision.
constexpr ClusterPipelineKey ClusterPipelineKeyFor(ClusterCullFlags flags, bool depthOnly, bool idTint) noexcept
{
    ClusterPipelineKey key;
    key.variant = depthOnly ? ClusterPipelineVariant::DepthOnly
                            : (idTint ? ClusterPipelineVariant::IdTint : ClusterPipelineVariant::Lit);
    key.cullBackFaces = HasClusterCullFlag(EffectiveClusterCullFlags(flags, depthOnly), ClusterCullFlags::BackfaceCone);
    return key;
}

// ---------------------------------------------------------------------------------------------
// Mesh checks
// ---------------------------------------------------------------------------------------------

// True when two triangles have the same three positions (any order). Such triangles can swap winners
// between frames in the cluster path. Triangles with an out-of-range index are ignored.
bool MeshHasCoincidentTriangles(std::span<const Vec3> positions, std::span<const std::uint32_t> indices);

// ---------------------------------------------------------------------------------------------
// Single-cluster tables for meshes without clusters
// ---------------------------------------------------------------------------------------------

// A mesh without a cluster table joins the cluster path only with at least this many instances in a frame.
inline constexpr std::size_t kMinInstancesForSynthesizedClusters = 4;

// Local-space box of a mesh as plain data.
struct LocalBoundsView {
    bool valid = false;
    float min[3] = {};
    float max[3] = {};
};

// One cluster that covers the whole index range: sphere around the box, no cone. Empty (with `why`) when the
// index count is zero, not a multiple of 3 or above 32 bits, or the box is missing or non-finite.
std::optional<MeshCluster> SynthesizeSingleCluster(
    const LocalBoundsView& bounds, std::size_t indexCount, std::string* why);

// Back-off before a failed synthesis is tried again: 120 frames, doubling per failure up to a cap.
inline constexpr std::uint32_t kSynthesisRetryBaseFrames = 120;
inline constexpr std::uint32_t kSynthesisRetryMaxFrames = 120u << 6;

// Failure bookkeeping of one mesh's synthesis.
struct SynthesisBackoff {
    std::uint32_t failures = 0;
    std::uint64_t retryAtFrame = 0;
};

// True when a synthesis may run in `frame` (never failed, or the back-off has elapsed).
constexpr bool SynthesisDue(const SynthesisBackoff& state, std::uint64_t frame) noexcept
{
    return state.failures == 0 || frame >= state.retryAtFrame;
}

// State after one more failure at `frame`.
SynthesisBackoff NoteSynthesisFailure(SynthesisBackoff state, std::uint64_t frame) noexcept;

} // namespace gte
