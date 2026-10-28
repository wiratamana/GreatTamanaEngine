// GPU parity: the real pyramid and cull shaders against the CPU oracle. Skipped when no device exists.

#include "Math/Mat4.h"
#include "Math/Quat.h"
#include "Math/Vec3.h"
#include "Renderer/Culling/ClusterCullMath.h"
#include "Renderer/Culling/HizBuildPlan.h"
#include "Renderer/RenderGraph/ComputePass.h"
#include "Renderer/RenderGraph/GpuBuffer.h"
#include "Renderer/RenderGraph/PassKindDefaults.h"
#include "Renderer/RenderGraph/RecordedPass.h"
#include "Renderer/RenderGraph/RenderGraphBuilder.h"
#include "Renderer/RenderGraph/Shader.h"
#include "Renderer/RenderGraph/TransferPass.h"
#include "Renderer/RenderTexture.h"
#include "Renderer/VulkanFormatInfo.h"

#include "../../Fakes/HeadlessRenderGraphFixture.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cmath>
#include <cstdint>
#include <iterator>
#include <optional>
#include <random>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace gte::gpu_parity {

using rg::GpuBuffer;
using rg::GpuBufferUse;

constexpr std::size_t kMinBufferBytes = 256;

GpuBuffer MakeBuffer(HeadlessRenderGraphFixture& fx, std::size_t bytes, const char* name, GpuBufferUse use)
{
    return GpuBuffer(fx.GetRenderer().Device(), std::max(bytes, kMinBufferBytes), name, use);
}

float Uniform(std::mt19937& rng, float lo, float hi)
{
    return std::uniform_real_distribution<float>(lo, hi)(rng);
}

// ---------------------------------------------------------------------------------------------
// Graph helpers
// ---------------------------------------------------------------------------------------------

struct Shaders {
    rg::Shader fill{ "GpuParityFill" };
    rg::Shader dump{ "GpuParityDump" };
    rg::Shader hizBase{ "HizBuildBase" };
    rg::Shader hizTop{ "HizBuildTop" };
    rg::Shader cull{ "ClusterCull" };

    bool Ready(rg::RenderGraphBuilder& builder)
    {
        return fill.IsReady(builder) && dump.IsReady(builder) && hizBase.IsReady(builder)
            && hizTop.IsReady(builder) && cull.IsReady(builder);
    }
};

struct SizePush {
    std::uint32_t width;
    std::uint32_t height;
};

// A single-channel float image that can be filled from a host-written buffer and sampled.
struct FloatImage {
    FloatImage(HeadlessRenderGraphFixture& fx, std::uint32_t w, std::uint32_t h, const char* textureName)
        : width(w)
        , height(h)
        , texture(fx.GetRenderer().CreateRenderTexture(static_cast<int>(w), static_cast<int>(h), PixelFormat::R32_Float,
              textureName, nullptr, /*allowStorageImageAccess=*/true, /*allowDepthSampledAccess=*/false,
              /*createDepthCompanion=*/false))
        , source(MakeBuffer(fx, std::size_t{ w } * h * sizeof(float), "ParityImageSource",
              GpuBufferUse::Storage | GpuBufferUse::HostWrite))
    {
    }

    bool Upload(std::span<const float> values)
    {
        return values.size() == std::size_t{ width } * height && source.Upload(values.data(), values.size_bytes());
    }

    std::uint32_t width;
    std::uint32_t height;
    RenderTexture texture;
    GpuBuffer source;
};

rg::TextureHandle ImportImage(rg::RenderGraphBuilder& builder, FloatImage& image, const char* name)
{
    RenderTexture& texture = image.texture;
    return builder.ImportTexture(name, texture.Target(), FromVkImageLayout(texture.CurrentState().layout),
        texture.SamplerObjectHandle(), SamplerHandle::Invalid, &texture);
}

bool DeclareImageFill(Shaders& shaders, rg::RenderGraphBuilder& builder, const FloatImage& image,
    rg::TextureHandle target, rg::BufferHandle source, rg::StaticName name)
{
    const SizePush push{ image.width, image.height };
    return rg::ComputePass(builder, shaders.fill, name, rg::ViewScope::Primary, rg::RenderPassEvent::Opaques)
        .Bind("source", source)
        .Bind("target", target)
        .Push(push)
        .Dispatch(Extent2D{ image.width, image.height });
}

bool DeclareImageDump(Shaders& shaders, rg::RenderGraphBuilder& builder, const FloatImage& image,
    rg::TextureHandle texture, rg::BufferHandle dump, rg::StaticName name)
{
    const SizePush push{ image.width, image.height };
    return rg::ComputePass(builder, shaders.dump, name, rg::ViewScope::Primary, rg::RenderPassEvent::Opaques)
        .Bind("source", texture)
        .Bind("dump", dump)
        .Push(push)
        .Dispatch(Extent2D{ image.width, image.height });
}

bool DeclareTransfer(rg::RenderGraphBuilder& builder, const char* name, const rg::TransferRecord& record)
{
    std::optional<rg::PassBody> body = rg::MakeTransferBody(builder, name, record);
    if (!body.has_value()) {
        return false;
    }
    builder.AddRenderPass(name, rg::DefaultPassKind(rg::DeclarativePassType::Transfer), rg::ViewScope::Primary,
        rg::RenderPassCategory::General, body->setup, body->execute,
        rg::DefaultDrawKind(rg::DeclarativePassType::Transfer), rg::RenderPassEvent::Opaques);
    return true;
}

bool DeclareReadback(rg::RenderGraphBuilder& builder, const char* name, rg::BufferHandle device, rg::BufferHandle host,
    std::uint64_t bytes)
{
    if (!DeclareTransfer(builder, name, rg::TransferRecord::Readback(device, host, bytes))) {
        return false;
    }
    builder.KeepBufferOutput(host);
    return true;
}

// ---------------------------------------------------------------------------------------------
// V2: pyramid build
// ---------------------------------------------------------------------------------------------

// Owns the images and buffers of one scene size; Run() builds the pyramid of one depth image.
class PyramidRig {
public:
    PyramidRig(HeadlessRenderGraphFixture& fx, Shaders& shaders, std::uint32_t width, std::uint32_t height)
        : m_fx(fx)
        , m_shaders(shaders)
        , m_width(width)
        , m_height(height)
        , m_layout(HizLayoutFor(width, height))
        , m_depth(fx, width, height, "ParityDepth")
        , m_atlas(fx, m_layout.atlasWidth, m_layout.atlasHeight, "ParityAtlas")
        , m_dump(MakeBuffer(fx, AtlasBytes(), "ParityDump", GpuBufferUse::Storage | GpuBufferUse::TransferSrc))
        , m_host(MakeBuffer(fx, AtlasBytes(), "ParityDumpHost", GpuBufferUse::HostRead | GpuBufferUse::TransferDst))
    {
    }

    const HizLayout& Layout() const noexcept { return m_layout; }

    // The whole atlas, row major; empty with `error` set on any failure.
    std::vector<float> Run(std::span<const float> depth, std::string& error)
    {
        if (!m_depth.Upload(depth)) {
            error = "the depth image could not be uploaded";
            return {};
        }
        bool ready = true;
        bool declared = false;
        m_fx.RunSynchronousFrame([&](rg::RenderGraphBuilder& builder) -> std::vector<rg::TextureHandle> {
            ready = m_shaders.Ready(builder);
            if (!ready) {
                return {};
            }
            const rg::TextureHandle depthTexture = ImportImage(builder, m_depth, "ParityDepth");
            const rg::TextureHandle atlasTexture = ImportImage(builder, m_atlas, "ParityAtlas");
            const rg::BufferHandle depthSource = builder.Import(m_depth.source);
            const rg::BufferHandle dump = builder.Import(m_dump);
            const rg::BufferHandle host = builder.Import(m_host);

            bool ok = DeclareImageFill(m_shaders, builder, m_depth, depthTexture, depthSource, "Parity.DepthFill");
            const HizBuildPlan plan = HizBuildPlanFor(m_layout);
            const HizBuildPush push = MakeHizBuildPush(m_width, m_height, m_layout);
            ok = rg::ComputePass(builder, m_shaders.hizBase, "Parity.HizBase", rg::ViewScope::Primary,
                     rg::RenderPassEvent::Opaques)
                     .Push(push)
                     .Bind("sceneDepth", depthTexture)
                     .Bind("pyramid", atlasTexture)
                     .Dispatch(Extent2D{ plan.baseThreadsX, plan.baseThreadsY })
                && ok;
            if (plan.runTop) {
                ok = rg::ComputePass(builder, m_shaders.hizTop, "Parity.HizTop", rg::ViewScope::Primary,
                         rg::RenderPassEvent::Opaques)
                         .Push(push)
                         .Bind("pyramid", atlasTexture)
                         .Dispatch(Extent2D{ plan.topThreadsX, 1 })
                    && ok;
            }
            ok = DeclareImageDump(m_shaders, builder, m_atlas, atlasTexture, dump, "Parity.AtlasDump") && ok;
            ok = DeclareReadback(builder, "Parity.AtlasRead", dump, host, AtlasBytes()) && ok;
            declared = ok;
            return {};
        });
        if (!ready) {
            error = "a parity shader is not ready";
            return {};
        }
        if (!declared) {
            error = "a pass could not be declared";
            return {};
        }
        std::vector<float> atlas(AtlasBytes() / sizeof(float));
        if (!m_host.Download(atlas.data(), AtlasBytes())) {
            error = "the atlas could not be read back";
            return {};
        }
        return atlas;
    }

private:
    std::size_t AtlasBytes() const noexcept
    {
        return std::size_t{ m_layout.atlasWidth } * m_layout.atlasHeight * sizeof(float);
    }

    HeadlessRenderGraphFixture& m_fx;
    Shaders& m_shaders;
    std::uint32_t m_width;
    std::uint32_t m_height;
    HizLayout m_layout;
    FloatImage m_depth;
    FloatImage m_atlas;
    GpuBuffer m_dump;
    GpuBuffer m_host;
};

// Empty when every texel of every level is equal; otherwise the first difference.
std::string ComparePyramid(const HizLayout& layout, std::span<const float> expected, std::span<const float> got)
{
    const std::size_t count = std::size_t{ layout.atlasWidth } * layout.atlasHeight;
    if (expected.size() != count || got.size() != count) {
        return "atlas size mismatch";
    }
    for (std::uint32_t k = 0; k < layout.levelCount; ++k) {
        const HizLevel& level = layout.levels[k];
        for (std::uint32_t y = 0; y < level.height; ++y) {
            for (std::uint32_t x = 0; x < level.width; ++x) {
                const std::size_t index = std::size_t{ level.originY + y } * layout.atlasWidth + level.originX + x;
                if (expected[index] != got[index]) {
                    return "level " + std::to_string(k) + " texel (" + std::to_string(x) + "," + std::to_string(y)
                        + ") expected " + std::to_string(expected[index]) + " got " + std::to_string(got[index]);
                }
            }
        }
    }
    return {};
}

// 0 = random, 1 = ramp, 2 = flat regions with edges and clear pixels.
std::vector<float> MakeDepthImage(int kind, std::uint32_t w, std::uint32_t h)
{
    std::vector<float> depth(std::size_t{ w } * h);
    std::mt19937 rng(1000u + static_cast<std::uint32_t>(kind) + w * 7u + h);
    if (kind == 0) {
        for (float& value : depth) {
            value = Uniform(rng, 0.0f, 1.0f);
        }
    } else if (kind == 1) {
        for (std::uint32_t y = 0; y < h; ++y) {
            for (std::uint32_t x = 0; x < w; ++x) {
                depth[std::size_t{ y } * w + x] = static_cast<float>(x + y + 1) / static_cast<float>(w + h);
            }
        }
    } else {
        std::fill(depth.begin(), depth.end(), 0.97f);
        for (int r = 0; r < 6; ++r) {
            const std::uint32_t x0 = static_cast<std::uint32_t>(rng() % w);
            const std::uint32_t y0 = static_cast<std::uint32_t>(rng() % h);
            const std::uint32_t x1 = std::min(w, x0 + 1u + static_cast<std::uint32_t>(rng() % std::max(1u, w / 2u)));
            const std::uint32_t y1 = std::min(h, y0 + 1u + static_cast<std::uint32_t>(rng() % std::max(1u, h / 2u)));
            const float value = Uniform(rng, 0.2f, 0.9f);
            for (std::uint32_t y = y0; y < y1; ++y) {
                for (std::uint32_t x = x0; x < x1; ++x) {
                    depth[std::size_t{ y } * w + x] = value;
                }
            }
        }
        for (std::size_t i = 0; i < depth.size(); i += 97) {
            depth[i] = 1.0f;
        }
    }
    return depth;
}

// ---------------------------------------------------------------------------------------------
// V3: cull dispatch
// ---------------------------------------------------------------------------------------------

struct CullScene {
    std::uint32_t clusterCount = 0;
    std::vector<MeshCluster> clusters;
    std::vector<Mat4> worlds;

    std::uint32_t Items() const noexcept { return clusterCount * static_cast<std::uint32_t>(worlds.size()); }
};

MeshCluster MakeCluster(std::mt19937& rng, std::uint32_t index)
{
    MeshCluster cluster{};
    float radiusSquared = 0.0f;
    for (int axis = 0; axis < 3; ++axis) {
        const float centre = Uniform(rng, -4.0f, 4.0f);
        const float half = Uniform(rng, 0.2f, 1.5f);
        cluster.aabbMin[axis] = centre - half;
        cluster.aabbMax[axis] = centre + half;
        cluster.sphere[axis] = centre;
        radiusSquared += half * half;
    }
    cluster.sphere[3] = std::sqrt(radiusSquared) * (1.0f + 1e-6f);
    Vec3 axis(Uniform(rng, -1.0f, 1.0f), Uniform(rng, -1.0f, 1.0f), Uniform(rng, -1.0f, 1.0f));
    axis = Length(axis) < 0.1f ? Vec3(0.0f, 1.0f, 0.0f) : Normalize(axis);
    cluster.cone[0] = axis.x;
    cluster.cone[1] = axis.y;
    cluster.cone[2] = axis.z;
    cluster.cone[3] = Uniform(rng, 0.0f, 1.0f) < 0.3f ? 1.0f : Uniform(rng, 0.1f, 0.9f);
    cluster.firstIndex = index * 30u;
    cluster.indexCount = 30u;
    return cluster;
}

// Mostly uniform scale; some non-uniform and some mirrored instances.
Mat4 MakeWorld(std::mt19937& rng)
{
    const Vec3 position(Uniform(rng, -50.0f, 50.0f), Uniform(rng, -30.0f, 30.0f), Uniform(rng, 10.0f, 120.0f));
    Vec3 axis(Uniform(rng, -1.0f, 1.0f), Uniform(rng, -1.0f, 1.0f), Uniform(rng, -1.0f, 1.0f));
    axis = Length(axis) < 0.1f ? Vec3(0.0f, 1.0f, 0.0f) : Normalize(axis);
    const Quat rotation = Quat::FromAxisAngle(axis, Uniform(rng, 0.0f, 6.28f));
    const float s = Uniform(rng, 0.5f, 2.0f);
    Vec3 scale(s, s, s);
    const float roll = Uniform(rng, 0.0f, 1.0f);
    if (roll < 0.1f) {
        scale = Vec3(s, s * Uniform(rng, 0.5f, 2.0f), s * Uniform(rng, 0.5f, 2.0f));
    } else if (roll < 0.2f) {
        scale.x = -scale.x;
    }
    return Mat4::TRS(position, rotation, scale);
}

CullScene MakeScene(std::mt19937& rng, std::uint32_t clusterCount, std::uint32_t instanceCount)
{
    CullScene scene;
    scene.clusterCount = clusterCount;
    for (std::uint32_t i = 0; i < clusterCount; ++i) {
        scene.clusters.push_back(MakeCluster(rng, i));
    }
    for (std::uint32_t i = 0; i < instanceCount; ++i) {
        scene.worlds.push_back(MakeWorld(rng));
    }
    return scene;
}

struct Camera {
    Mat4 viewProjection;
    Vec3 eye;
};

Camera RandomCamera(std::mt19937& rng)
{
    const Vec3 eye(Uniform(rng, -20.0f, 20.0f), Uniform(rng, -10.0f, 10.0f), Uniform(rng, -10.0f, 5.0f));
    const Vec3 forward(Uniform(rng, -0.5f, 0.5f), Uniform(rng, -0.3f, 0.3f), 1.0f);
    const Mat4 view = Mat4::LookAtLH(eye, eye + forward, Vec3::Up());
    const Mat4 projection = Mat4::PerspectiveFovLH_ZO(Uniform(rng, 0.6f, 1.4f), 4.0f / 3.0f, 0.1f, 300.0f);
    return Camera{ projection * view, eye };
}

// Depth image with a few flat occluders at random view distances; clear depth elsewhere.
std::vector<float> MakeOccluderDepth(std::mt19937& rng, std::uint32_t w, std::uint32_t h, const Mat4& viewProjection)
{
    std::vector<float> depth(std::size_t{ w } * h, 1.0f);
    const HizDepthMap map = HizDepthMapOf(viewProjection);
    for (int r = 0; r < 5; ++r) {
        const std::uint32_t x0 = static_cast<std::uint32_t>(rng() % w);
        const std::uint32_t y0 = static_cast<std::uint32_t>(rng() % h);
        const std::uint32_t x1 = std::min(w, x0 + 1u + static_cast<std::uint32_t>(rng() % (w / 2u)));
        const std::uint32_t y1 = std::min(h, y0 + 1u + static_cast<std::uint32_t>(rng() % (h / 2u)));
        const float value = map.k + map.b / Uniform(rng, 12.0f, 70.0f);
        for (std::uint32_t y = y0; y < y1; ++y) {
            for (std::uint32_t x = x0; x < x1; ++x) {
                depth[std::size_t{ y } * w + x] = value;
            }
        }
    }
    return depth;
}

struct CullResult {
    std::vector<std::uint32_t> survivors; // Sorted work ids (instance * clusterCount + cluster).
    std::vector<std::uint32_t> history;   // Empty unless requested.
    std::string error;
};

// One cull dispatch over persistent buffers, so the history survives between runs.
class CullRig {
public:
    CullRig(HeadlessRenderGraphFixture& fx, Shaders& shaders, const CullScene& scene, const HizLayout& layout)
        : m_fx(fx)
        , m_shaders(shaders)
        , m_clusterCount(scene.clusterCount)
        , m_items(scene.Items())
        , m_params(MakeBuffer(fx, sizeof(ClusterCullParams), "ParityParams", GpuBufferUse::Storage | GpuBufferUse::HostWrite))
        , m_instances(MakeBuffer(fx, scene.worlds.size() * sizeof(ClusterInstanceGpu), "ParityInstances",
              GpuBufferUse::Storage | GpuBufferUse::HostWrite))
        , m_clusters(MakeBuffer(fx, scene.clusters.size() * sizeof(MeshCluster), "ParityClusters",
              GpuBufferUse::Storage | GpuBufferUse::HostWrite))
        , m_visible(MakeBuffer(fx, std::size_t{ m_items } * 8, "ParityVisible", GpuBufferUse::Storage | GpuBufferUse::TransferSrc))
        , m_commands(MakeBuffer(fx, std::size_t{ m_items } * 20, "ParityCommands", GpuBufferUse::Storage))
        , m_count(MakeBuffer(fx, 4, "ParityCount",
              GpuBufferUse::Storage | GpuBufferUse::TransferSrc | GpuBufferUse::TransferDst))
        , m_history(MakeBuffer(fx, std::size_t{ m_items } * 4, "ParityHistory",
              GpuBufferUse::Storage | GpuBufferUse::HostWrite | GpuBufferUse::TransferSrc))
        , m_countHost(MakeBuffer(fx, 4, "ParityCountHost", GpuBufferUse::HostRead | GpuBufferUse::TransferDst))
        , m_visibleHost(MakeBuffer(fx, std::size_t{ m_items } * 8, "ParityVisibleHost",
              GpuBufferUse::HostRead | GpuBufferUse::TransferDst))
        , m_historyHost(MakeBuffer(fx, std::size_t{ m_items } * 4, "ParityHistoryHost",
              GpuBufferUse::HostRead | GpuBufferUse::TransferDst))
        , m_atlas(fx, layout.atlasWidth, layout.atlasHeight, "ParityCullAtlas")
    {
    }

    bool SetScene(const CullScene& scene)
    {
        std::vector<ClusterInstanceGpu> records;
        records.reserve(scene.worlds.size());
        for (const Mat4& world : scene.worlds) {
            float maxScale = 0.0f;
            const std::uint32_t flags = ClusterInstanceFlagsFor(world, &maxScale);
            records.push_back(MakeClusterInstanceGpu(world, flags, maxScale));
        }
        return m_clusters.Upload(scene.clusters.data(), scene.clusters.size() * sizeof(MeshCluster))
            && m_instances.Upload(records.data(), records.size() * sizeof(ClusterInstanceGpu));
    }

    bool SetHistory(std::span<const std::uint32_t> words)
    {
        return words.size() == m_items && m_history.Upload(words.data(), words.size_bytes());
    }

    // `atlas` is the whole pyramid atlas, row major.
    CullResult Run(const ClusterCullParams& params, std::span<const float> atlas, bool readHistory)
    {
        CullResult result;
        if (!m_params.Upload(&params, sizeof(params)) || !m_atlas.Upload(atlas)) {
            result.error = "an input could not be uploaded";
            return result;
        }
        bool ready = true;
        bool declared = false;
        m_fx.RunSynchronousFrame([&](rg::RenderGraphBuilder& builder) -> std::vector<rg::TextureHandle> {
            ready = m_shaders.Ready(builder);
            if (!ready) {
                return {};
            }
            const rg::TextureHandle atlasTexture = ImportImage(builder, m_atlas, "ParityCullAtlas");
            const rg::BufferHandle atlasSource = builder.Import(m_atlas.source);
            const rg::BufferHandle paramsBuffer = builder.Import(m_params);
            const rg::BufferHandle instances = builder.Import(m_instances);
            const rg::BufferHandle clusters = builder.Import(m_clusters);
            const rg::BufferHandle visible = builder.Import(m_visible);
            const rg::BufferHandle commands = builder.Import(m_commands);
            const rg::BufferHandle count = builder.Import(m_count);
            const rg::BufferHandle history = builder.Import(m_history);
            const rg::BufferHandle countHost = builder.Import(m_countHost);
            const rg::BufferHandle visibleHost = builder.Import(m_visibleHost);
            const rg::BufferHandle historyHost = builder.Import(m_historyHost);

            bool ok = DeclareImageFill(m_shaders, builder, m_atlas, atlasTexture, atlasSource, "Parity.AtlasFill");
            ok = DeclareTransfer(builder, "Parity.CountClear", rg::TransferRecord::Fill(count, 0u)) && ok;
            ok = rg::ComputePass(builder, m_shaders.cull, "Parity.Cull", rg::ViewScope::Primary,
                     rg::RenderPassEvent::Opaques)
                     .Bind("params", paramsBuffer)
                     .Bind("instances", instances)
                     .Bind("clusters", clusters)
                     .Bind("visible", visible)
                     .Bind("commands", commands)
                     .Bind("count", count)
                     .Bind("history", history)
                     .Bind("hizAtlas", atlasTexture)
                     .Dispatch(Extent2D{ m_items, 1 })
                && ok;
            ok = DeclareReadback(builder, "Parity.ReadCount", count, countHost, 4) && ok;
            ok = DeclareReadback(builder, "Parity.ReadVisible", visible, visibleHost, std::uint64_t{ m_items } * 8) && ok;
            if (readHistory) {
                ok = DeclareReadback(builder, "Parity.ReadHistory", history, historyHost, std::uint64_t{ m_items } * 4)
                    && ok;
            }
            declared = ok;
            return {};
        });
        if (!ready || !declared) {
            result.error = !ready ? "a parity shader is not ready" : "a pass could not be declared";
            return result;
        }

        std::uint32_t count = 0;
        if (!m_countHost.Download(&count, sizeof(count)) || count > m_items) {
            result.error = "the survivor count is not readable or past the work item count";
            return result;
        }
        std::vector<std::uint32_t> records(std::size_t{ count } * 2);
        if (count > 0 && !m_visibleHost.Download(records.data(), records.size() * sizeof(std::uint32_t))) {
            result.error = "the survivor list could not be read back";
            return result;
        }
        result.survivors.reserve(count);
        for (std::uint32_t i = 0; i < count; ++i) {
            result.survivors.push_back(records[i * 2] * m_clusterCount + records[i * 2 + 1]);
        }
        std::sort(result.survivors.begin(), result.survivors.end());
        if (readHistory) {
            result.history.resize(m_items);
            if (!m_historyHost.Download(result.history.data(), result.history.size() * sizeof(std::uint32_t))) {
                result.error = "the history could not be read back";
            }
        }
        return result;
    }

private:
    HeadlessRenderGraphFixture& m_fx;
    Shaders& m_shaders;
    std::uint32_t m_clusterCount;
    std::uint32_t m_items;
    GpuBuffer m_params;
    GpuBuffer m_instances;
    GpuBuffer m_clusters;
    GpuBuffer m_visible;
    GpuBuffer m_commands;
    GpuBuffer m_count;
    GpuBuffer m_history;
    GpuBuffer m_countHost;
    GpuBuffer m_visibleHost;
    GpuBuffer m_historyHost;
    FloatImage m_atlas;
};

// ---------------------------------------------------------------------------------------------
// CPU oracle of a whole dispatch
// ---------------------------------------------------------------------------------------------

enum class DispatchKind : std::uint8_t { Single, First, Second };

struct OracleResult {
    std::vector<std::uint32_t> survivors; // Sorted work ids.
    std::vector<std::uint32_t> words;     // History words after the pass; empty for a single pass.
};

OracleResult RunOracle(const CullScene& scene, const Camera& camera, ClusterCullFlags flags, DispatchKind kind,
    bool historyValid, std::span<const std::uint32_t> inWords, const HizLayout& layout, std::span<const float> atlas)
{
    const std::array<Plane, 6> planes = ExtractFrustumPlanes(camera.viewProjection);
    struct InstanceInfo {
        std::uint32_t flags;
        float maxScale;
    };
    std::vector<InstanceInfo> info(scene.worlds.size());
    for (std::size_t i = 0; i < info.size(); ++i) {
        info[i].flags = ClusterInstanceFlagsFor(scene.worlds[i], &info[i].maxScale);
    }

    OracleResult result;
    if (kind != DispatchKind::Single) {
        result.words.resize(scene.Items());
    }
    for (std::uint32_t id = 0; id < scene.Items(); ++id) {
        const std::uint32_t instance = id / scene.clusterCount;
        const MeshCluster& cluster = scene.clusters[id - instance * scene.clusterCount];
        const Mat4& world = scene.worlds[instance];
        const bool inView = !ClusterCulled(
            cluster, world, info[instance].flags, info[instance].maxScale, flags, planes, camera.eye, false);
        bool emit = false;
        if (kind == DispatchKind::First) {
            const ClusterPhaseResult r = ClusterPhaseFirst(inWords[id], historyValid, inView);
            emit = r.emit;
            result.words[id] = r.word;
        } else if (kind == DispatchKind::Second) {
            const bool occluded = inView && ClusterOccluded(cluster, world, camera.viewProjection, layout, atlas);
            const ClusterPhaseResult r = ClusterPhaseSecond(inWords[id], inView, occluded);
            emit = r.emit;
            result.words[id] = r.word;
        } else {
            emit = inView
                && !(HasClusterCullFlag(flags, ClusterCullFlags::Occlusion)
                    && ClusterOccluded(cluster, world, camera.viewProjection, layout, atlas));
        }
        if (emit) {
            result.survivors.push_back(id);
        }
    }
    return result;
}

std::size_t SymmetricDifference(const std::vector<std::uint32_t>& a, const std::vector<std::uint32_t>& b)
{
    std::vector<std::uint32_t> difference;
    std::set_symmetric_difference(a.begin(), a.end(), b.begin(), b.end(), std::back_inserter(difference));
    return difference.size();
}

std::size_t Intersection(const std::vector<std::uint32_t>& a, const std::vector<std::uint32_t>& b)
{
    std::vector<std::uint32_t> common;
    std::set_intersection(a.begin(), a.end(), b.begin(), b.end(), std::back_inserter(common));
    return common.size();
}

// Elements of `a` that are not in `b`; both sorted.
std::size_t Missing(const std::vector<std::uint32_t>& a, const std::vector<std::uint32_t>& b)
{
    std::vector<std::uint32_t> missing;
    std::set_difference(a.begin(), a.end(), b.begin(), b.end(), std::back_inserter(missing));
    return missing.size();
}

std::size_t WordMismatches(const std::vector<std::uint32_t>& a, const std::vector<std::uint32_t>& b)
{
    if (a.size() != b.size()) {
        return std::max(a.size(), b.size());
    }
    std::size_t count = 0;
    for (std::size_t i = 0; i < a.size(); ++i) {
        count += a[i] != b[i] ? 1 : 0;
    }
    return count;
}

// GPU and CPU floating point can round a borderline box differently; a handful of such items is allowed.
std::size_t Tolerance(std::uint32_t items) noexcept
{
    return std::max<std::size_t>(2, items / 2000);
}

constexpr std::uint32_t kSceneWidth = 160;
constexpr std::uint32_t kSceneHeight = 120;
constexpr std::uint32_t kClusters = 25;
constexpr std::uint32_t kInstances = 400;

ClusterCullParams MakeParams(const CullScene& scene, const Camera& camera, ClusterCullFlags flags,
    const HizLayout* layout)
{
    ClusterCullParams params = MakeClusterCullParams(camera.viewProjection, camera.eye, false,
        static_cast<std::uint32_t>(scene.worlds.size()), scene.clusterCount, flags);
    if (layout != nullptr) {
        SetClusterCullHiz(params, *layout);
    }
    return params;
}

std::vector<std::uint32_t> RandomWords(std::mt19937& rng, std::uint32_t items, std::uint32_t mask)
{
    std::vector<std::uint32_t> words(items);
    for (std::uint32_t& word : words) {
        word = static_cast<std::uint32_t>(rng()) & mask;
    }
    return words;
}

// ---------------------------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------------------------

TEST(ClusterGpuParityTest, HizPyramidMatchesTheCpuReferenceOnEveryLevel)
{
    HeadlessRenderGraphFixture fixture;
    if (!fixture.IsUsable()) {
        GTEST_SKIP() << fixture.SkipReason();
    }
    Shaders shaders;

    const std::array<std::pair<std::uint32_t, std::uint32_t>, 4> sizes{
        { { 1, 1 }, { 33, 17 }, { 350, 447 }, { 1920, 1080 } }
    };
    for (const auto& size : sizes) {
        const std::uint32_t w = size.first;
        const std::uint32_t h = size.second;
        PyramidRig rig(fixture, shaders, w, h);
        ASSERT_TRUE(rig.Layout().IsValid()) << w << "x" << h;
        for (int kind = 0; kind < 3; ++kind) {
            const std::vector<float> depth = MakeDepthImage(kind, w, h);
            const std::vector<float> expected = BuildHizPyramidReference(depth, w, h, rig.Layout());
            std::string error;
            const std::vector<float> got = rig.Run(depth, error);
            ASSERT_TRUE(error.empty()) << w << "x" << h << " image " << kind << ": " << error;
            const std::string mismatch = ComparePyramid(rig.Layout(), expected, got);
            EXPECT_TRUE(mismatch.empty()) << "size " << w << "x" << h << " image " << kind << ": " << mismatch;
        }
    }
}

TEST(ClusterGpuParityTest, SinglePhaseCullMatchesTheOracle)
{
    HeadlessRenderGraphFixture fixture;
    if (!fixture.IsUsable()) {
        GTEST_SKIP() << fixture.SkipReason();
    }
    Shaders shaders;
    std::mt19937 rng(7);
    const CullScene scene = MakeScene(rng, kClusters, kInstances);
    const HizLayout layout = HizLayoutFor(kSceneWidth, kSceneHeight);
    ASSERT_TRUE(layout.IsValid());
    CullRig rig(fixture, shaders, scene, layout);
    ASSERT_TRUE(rig.SetScene(scene));
    const std::vector<float> allFar(std::size_t{ layout.atlasWidth } * layout.atlasHeight, 1.0f);

    struct Mode {
        const char* name;
        ClusterCullFlags flags;
    };
    const std::array<Mode, 3> modes{ { { "Frustum", ClusterCullFlags::Frustum },
        { "Frustum+Cone", ClusterCullFlags::Frustum | ClusterCullFlags::BackfaceCone },
        { "Frustum+Cone+Occlusion",
            ClusterCullFlags::Frustum | ClusterCullFlags::BackfaceCone | ClusterCullFlags::Occlusion } } };

    std::size_t occludedTotal = 0;
    for (int cameraIndex = 0; cameraIndex < 3; ++cameraIndex) {
        const Camera camera = RandomCamera(rng);
        const std::vector<float> depth = MakeOccluderDepth(rng, kSceneWidth, kSceneHeight, camera.viewProjection);
        const std::vector<float> atlas = BuildHizPyramidReference(depth, kSceneWidth, kSceneHeight, layout);
        std::size_t withoutOcclusion = 0;
        for (const Mode& mode : modes) {
            const bool occlusion = HasClusterCullFlag(mode.flags, ClusterCullFlags::Occlusion);
            const ClusterCullParams params = MakeParams(scene, camera, mode.flags, occlusion ? &layout : nullptr);
            const CullResult gpu = rig.Run(params, occlusion ? std::span<const float>(atlas) : std::span<const float>(allFar), false);
            ASSERT_TRUE(gpu.error.empty()) << mode.name << ": " << gpu.error;
            const OracleResult oracle = RunOracle(scene, camera, mode.flags, DispatchKind::Single, false, {}, layout, atlas);
            EXPECT_LE(SymmetricDifference(gpu.survivors, oracle.survivors), Tolerance(scene.Items()))
                << mode.name << " camera " << cameraIndex << ": gpu " << gpu.survivors.size() << " oracle "
                << oracle.survivors.size();
            if (!occlusion) {
                withoutOcclusion = oracle.survivors.size();
            } else {
                occludedTotal += withoutOcclusion - std::min(withoutOcclusion, oracle.survivors.size());
            }
        }
    }
    EXPECT_GT(occludedTotal, 0u) << "the pyramid never occluded anything: the test input is too weak";
}

TEST(ClusterGpuParityTest, TwoPhaseCullMatchesTheOracleSurvivorsAndHistory)
{
    HeadlessRenderGraphFixture fixture;
    if (!fixture.IsUsable()) {
        GTEST_SKIP() << fixture.SkipReason();
    }
    Shaders shaders;
    std::mt19937 rng(11);
    const CullScene scene = MakeScene(rng, kClusters, kInstances);
    const HizLayout layout = HizLayoutFor(kSceneWidth, kSceneHeight);
    ASSERT_TRUE(layout.IsValid());
    CullRig rig(fixture, shaders, scene, layout);
    ASSERT_TRUE(rig.SetScene(scene));
    const std::vector<float> allFar(std::size_t{ layout.atlasWidth } * layout.atlasHeight, 1.0f);
    const ClusterCullFlags base = ClusterCullFlags::Frustum | ClusterCullFlags::BackfaceCone;
    const std::size_t tolerance = Tolerance(scene.Items());

    for (int cameraIndex = 0; cameraIndex < 3; ++cameraIndex) {
        const Camera camera = RandomCamera(rng);
        const std::vector<float> depth = MakeOccluderDepth(rng, kSceneWidth, kSceneHeight, camera.viewProjection);
        const std::vector<float> atlas = BuildHizPyramidReference(depth, kSceneWidth, kSceneHeight, layout);

        // An invalid history reads as all zero: phase 1 draws nothing, whatever the buffer holds.
        const std::vector<std::uint32_t> garbage = RandomWords(rng, scene.Items(), 3u);
        ASSERT_TRUE(rig.SetHistory(garbage));
        CullResult first = rig.Run(MakeParams(scene, camera, base | ClusterCullFlags::PhaseFirst, nullptr), allFar, true);
        ASSERT_TRUE(first.error.empty()) << first.error;
        OracleResult oracleFirst = RunOracle(scene, camera, base, DispatchKind::First, false, garbage, layout, allFar);
        EXPECT_TRUE(first.survivors.empty()) << "an invalid history drew " << first.survivors.size() << " items";
        EXPECT_LE(WordMismatches(first.history, oracleFirst.words), tolerance) << "invalid history, camera " << cameraIndex;

        // A valid history of "visible last frame" bits.
        const std::vector<std::uint32_t> valid = RandomWords(rng, scene.Items(), 1u);
        ASSERT_TRUE(rig.SetHistory(valid));
        first = rig.Run(MakeParams(scene, camera, base | ClusterCullFlags::PhaseFirst | ClusterCullFlags::HistoryValid, nullptr),
            allFar, true);
        ASSERT_TRUE(first.error.empty()) << first.error;
        oracleFirst = RunOracle(scene, camera, base, DispatchKind::First, true, valid, layout, allFar);
        EXPECT_LE(SymmetricDifference(first.survivors, oracleFirst.survivors), tolerance) << "phase 1, camera " << cameraIndex;
        EXPECT_LE(WordMismatches(first.history, oracleFirst.words), tolerance) << "phase 1 words, camera " << cameraIndex;

        // Phase 2 reads the words that phase 1 left on the GPU.
        const CullResult second = rig.Run(MakeParams(scene, camera, base | ClusterCullFlags::PhaseSecond, &layout), atlas, true);
        ASSERT_TRUE(second.error.empty()) << second.error;
        const OracleResult oracleSecond =
            RunOracle(scene, camera, base, DispatchKind::Second, true, first.history, layout, atlas);
        EXPECT_LE(SymmetricDifference(second.survivors, oracleSecond.survivors), tolerance)
            << "phase 2, camera " << cameraIndex;
        EXPECT_LE(WordMismatches(second.history, oracleSecond.words), tolerance) << "phase 2 words, camera " << cameraIndex;
        EXPECT_EQ(Intersection(first.survivors, second.survivors), 0u) << "an item was drawn in both phases";
    }
}

TEST(ClusterGpuParityTest, TwoPhasesOverAnAllFarPyramidEqualThePlainFrustumSurvivors)
{
    HeadlessRenderGraphFixture fixture;
    if (!fixture.IsUsable()) {
        GTEST_SKIP() << fixture.SkipReason();
    }
    Shaders shaders;
    std::mt19937 rng(23);
    const CullScene scene = MakeScene(rng, kClusters, kInstances);
    const HizLayout layout = HizLayoutFor(kSceneWidth, kSceneHeight);
    ASSERT_TRUE(layout.IsValid());
    CullRig rig(fixture, shaders, scene, layout);
    ASSERT_TRUE(rig.SetScene(scene));
    const std::vector<float> allFar(std::size_t{ layout.atlasWidth } * layout.atlasHeight, 1.0f);
    const ClusterCullFlags base = ClusterCullFlags::Frustum | ClusterCullFlags::BackfaceCone;

    for (int cameraIndex = 0; cameraIndex < 3; ++cameraIndex) {
        const Camera camera = RandomCamera(rng);
        ASSERT_TRUE(rig.SetHistory(RandomWords(rng, scene.Items(), 1u)));
        const CullResult first =
            rig.Run(MakeParams(scene, camera, base | ClusterCullFlags::PhaseFirst | ClusterCullFlags::HistoryValid, nullptr),
                allFar, true);
        const CullResult second = rig.Run(MakeParams(scene, camera, base | ClusterCullFlags::PhaseSecond, &layout), allFar, true);
        ASSERT_TRUE(first.error.empty()) << first.error;
        ASSERT_TRUE(second.error.empty()) << second.error;

        std::vector<std::uint32_t> merged = first.survivors;
        merged.insert(merged.end(), second.survivors.begin(), second.survivors.end());
        std::sort(merged.begin(), merged.end());
        const OracleResult plain = RunOracle(scene, camera, base, DispatchKind::Single, false, {}, layout, allFar);
        EXPECT_LE(SymmetricDifference(merged, plain.survivors), Tolerance(scene.Items())) << "camera " << cameraIndex;
        EXPECT_EQ(Intersection(first.survivors, second.survivors), 0u) << "an item was drawn in both phases";
    }
}

TEST(ClusterGpuParityTest, HundredFramesNeverDrawAnItemTwiceNorMissAVisibleOne)
{
    HeadlessRenderGraphFixture fixture;
    if (!fixture.IsUsable()) {
        GTEST_SKIP() << fixture.SkipReason();
    }
    Shaders shaders;
    std::mt19937 rng(99);
    CullScene scene = MakeScene(rng, kClusters, kInstances);
    const HizLayout layout = HizLayoutFor(kSceneWidth, kSceneHeight);
    ASSERT_TRUE(layout.IsValid());
    CullRig rig(fixture, shaders, scene, layout);
    const ClusterCullFlags base = ClusterCullFlags::Frustum | ClusterCullFlags::BackfaceCone;
    const std::size_t tolerance = Tolerance(scene.Items());

    std::vector<std::uint32_t> history(scene.Items(), 0u);
    ASSERT_TRUE(rig.SetHistory(history));
    bool historyValid = false;
    for (int frame = 0; frame < 100; ++frame) {
        // A tenth of the instances move every frame; the camera changes every frame.
        for (std::size_t i = 0; i < scene.worlds.size(); ++i) {
            if (Uniform(rng, 0.0f, 1.0f) < 0.1f) {
                scene.worlds[i] = MakeWorld(rng);
            }
        }
        ASSERT_TRUE(rig.SetScene(scene));
        const Camera camera = RandomCamera(rng);
        const std::vector<float> depth = MakeOccluderDepth(rng, kSceneWidth, kSceneHeight, camera.viewProjection);
        const std::vector<float> atlas = BuildHizPyramidReference(depth, kSceneWidth, kSceneHeight, layout);

        ClusterCullFlags firstFlags = base | ClusterCullFlags::PhaseFirst;
        if (historyValid) {
            firstFlags = firstFlags | ClusterCullFlags::HistoryValid;
        }
        const CullResult first = rig.Run(MakeParams(scene, camera, firstFlags, nullptr), atlas, true);
        ASSERT_TRUE(first.error.empty()) << "frame " << frame << ": " << first.error;
        const CullResult second = rig.Run(MakeParams(scene, camera, base | ClusterCullFlags::PhaseSecond, &layout), atlas, true);
        ASSERT_TRUE(second.error.empty()) << "frame " << frame << ": " << second.error;

        const OracleResult oracleFirst =
            RunOracle(scene, camera, base, DispatchKind::First, historyValid, history, layout, atlas);
        const OracleResult oracleSecond =
            RunOracle(scene, camera, base, DispatchKind::Second, true, first.history, layout, atlas);
        EXPECT_LE(SymmetricDifference(first.survivors, oracleFirst.survivors), tolerance) << "phase 1, frame " << frame;
        EXPECT_LE(WordMismatches(first.history, oracleFirst.words), tolerance) << "phase 1 words, frame " << frame;
        EXPECT_LE(SymmetricDifference(second.survivors, oracleSecond.survivors), tolerance) << "phase 2, frame " << frame;
        EXPECT_LE(WordMismatches(second.history, oracleSecond.words), tolerance) << "phase 2 words, frame " << frame;
        EXPECT_EQ(Intersection(first.survivors, second.survivors), 0u) << "drawn twice, frame " << frame;

        // Everything the frustum, cone and pyramid rules keep must be drawn by one of the phases.
        std::vector<std::uint32_t> drawn = first.survivors;
        drawn.insert(drawn.end(), second.survivors.begin(), second.survivors.end());
        std::sort(drawn.begin(), drawn.end());
        const OracleResult kept = RunOracle(
            scene, camera, base | ClusterCullFlags::Occlusion, DispatchKind::Single, false, {}, layout, atlas);
        EXPECT_LE(Missing(kept.survivors, drawn), tolerance) << "a kept item was not drawn, frame " << frame;

        history = second.history;
        historyValid = true;
    }
}

} // namespace gte::gpu_parity
