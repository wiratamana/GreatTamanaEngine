#include "ClusterViewRenderer.h"
#include "ClusterViewRendererImpl.h"

#include "Culling/HizBuildPlan.h"
#include "RenderGraph/ComputePass.h"
#include "RenderGraph/RenderGraphBuilder.h"

#include <span>

namespace gte {

namespace {

constexpr const char* kHizBasePassName = "ClusterView.Hiz.Base";
constexpr const char* kHizTopPassName = "ClusterView.Hiz.Top";

} // namespace

bool ClusterViewRenderer::Impl::DeclareCullAll(rg::RenderGraphBuilder& builder, const Mat4& viewProjection,
    const Vec3& eye, const char* resetName, GraphSet& set, rg::BufferHandle& instances)
{
    if (!cullShader.IsReady(builder)) {
        return false;
    }
    instances = builder.Import(*instance);
    const rg::BufferHandle tableHandle = builder.Import(*table);
    const rg::BufferHandle historyHandle = ImportHistoryStandIn(builder);
    const rg::TextureHandle standIn = ImportHizAtlas(builder, false);
    set = ImportSet(builder, SetId::Ghost);

    ClusterViewCull cull;
    cull.viewProjection = viewProjection;
    cull.eye = eye;
    const bool reset = DeclareFill(builder, resetName, set.count);
    const bool culled = DeclareCull(
        builder, set, instances, tableHandle, historyHandle, standIn, MakeParams(cull, ClusterCullFlags::None, false));
    return reset && culled;
}

std::optional<CapturedPixels> ClusterViewRenderer::RenderIdPass(
    const Mat4& viewProjection, int width, int height, bool cullBackFaces)
{
    Impl& s = *m_impl;
    try {
        std::string note;
        if (!s.mesh.has_value() || !s.table.has_value() || !s.instance.has_value() || s.tableClusters == 0) {
            s.LogOnce("the id pass needs a mesh");
            return std::nullopt;
        }
        if (!s.CheckSize(width, height, note)) {
            s.LogOnce("the id pass: " + note);
            return std::nullopt;
        }
        if (!CameraIsFinite(viewProjection, Vec3::Zero())) {
            s.LogOnce("the id pass camera holds a non-finite value");
            return std::nullopt;
        }
        const Pipeline* pipeline = s.ViewPipeline(ClusterViewFragment::Id, cullBackFaces);
        if (pipeline == nullptr || !s.EnsureSet(Impl::SetId::Ghost, false, false)) {
            s.LogOnce("the id pass could not prepare its pipeline or buffers");
            return std::nullopt;
        }

        // The id target has no UI handle: it is only read back.
        Impl::SlotTarget& id = s.idSlot;
        if (!id.texture.has_value()) {
            id.texture.emplace(
                s.renderer.CreateRenderTexture(width, height, PixelFormat::RGBA8_UNorm, "ClusterViewId", "ClusterViewIdDepth"));
        } else if (id.width != width || id.height != height) {
            s.renderer.ResizeRenderTexture(*id.texture, width, height);
        }
        id.width = width;
        id.height = height;

        std::string buildError;
        const auto build = [&](rg::RenderGraphBuilder& builder) {
            std::vector<rg::TextureHandle> outputs;
            Impl::GraphSet set;
            rg::BufferHandle instances;
            if (!s.DeclareCullAll(builder, viewProjection, Vec3::Zero(), "ClusterView.Id.Reset", set, instances)) {
                buildError = "the id pass cull could not be declared";
                return outputs;
            }
            RenderTexture& texture = *id.texture;
            const rg::TextureHandle target = builder.ImportTexture("ClusterViewId", texture.Target(),
                ImageLayout::Undefined, texture.SamplerObjectHandle(), texture.DepthSamplerObjectHandle(), &texture);
            builder.KeepTextureOutput(target);
            outputs.push_back(target);
            if (!s.DeclareDraw(builder, "ClusterView.Id.Draw", set, instances, target, false, *pipeline,
                    viewProjection, 0, std::array<float, 4>{ 0.0f, 0.0f, 0.0f, 1.0f }, 1.0f)) {
                buildError = "the id pass draw could not be declared";
            }
            return outputs;
        };

        RenderTexture* finalize[1] = { &*id.texture };
        if (!s.Execute(build, std::span<RenderTexture* const>(finalize, 1), note)) {
            return std::nullopt;
        }
        if (!buildError.empty()) {
            s.LogOnce(buildError);
            return std::nullopt;
        }
        CaptureResult captured = s.renderer.CaptureRenderTexturePixels(*id.texture);
        if (captured.status != CaptureStatus::Ok || captured.pixels.pixels.empty()) {
            s.LogOnce("the id image could not be read back");
            return std::nullopt;
        }
        return std::move(captured.pixels);
    } catch (const std::exception& e) {
        s.LogOnce(std::string("the id pass failed: ") + e.what());
        s.ReleaseSlot(s.idSlot);
        return std::nullopt;
    }
}

std::optional<ClusterViewHizInfo> ClusterViewRenderer::CaptureHiz(
    const Mat4& viewProjection, const Vec3& eye, int width, int height)
{
    Impl& s = *m_impl;
    try {
        std::string note;
        if (!s.mesh.has_value() || !s.table.has_value() || !s.instance.has_value() || s.tableClusters == 0) {
            s.LogOnce("the Hi-Z capture needs a mesh");
            return std::nullopt;
        }
        if (!s.CheckSize(width, height, note)) {
            s.LogOnce("the Hi-Z capture: " + note);
            return std::nullopt;
        }
        if (!CameraIsFinite(viewProjection, eye)) {
            s.LogOnce("the Hi-Z camera holds a non-finite value");
            return std::nullopt;
        }
        const HizLayout layout = HizLayoutFor(static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height));
        if (!layout.IsValid()) {
            s.LogOnce("the Hi-Z capture size " + std::to_string(width) + "x" + std::to_string(height) + " is outside the pyramid range");
            return std::nullopt;
        }
        // Depth-only cluster pipeline of the engine: draws both sides, needs no scene set.
        const Pipeline* depthPipeline = s.renderer.CreateClusterPipeline(
            ClusterPipelineKeyFor(ClusterCullFlags::None, true, false), DescriptorSetLayoutHandle::Invalid);
        if (depthPipeline == nullptr || !s.EnsureSet(Impl::SetId::Ghost, false, false)) {
            s.LogOnce("the Hi-Z capture could not prepare its pipeline or buffers");
            return std::nullopt;
        }

        // Targets change only while the GPU is idle.
        const bool reusable = s.hizDepth.has_value() && s.hizAtlas.has_value() && s.hizSceneWidth == width
            && s.hizSceneHeight == height;
        if (!reusable) {
            if (!s.DrainGpu("cluster viewer Hi-Z targets")) {
                s.LogOnce("the Hi-Z capture needs an idle GPU");
                return std::nullopt;
            }
            s.hizInfo.reset();
            s.hizAtlas.reset();
            s.hizDepth.reset();
            s.hizDepth.emplace(s.renderer.CreateRenderTexture(width, height, PixelFormat::Undefined,
                "ClusterViewHizDepth", "ClusterViewHizDepth", /*allowStorageImageAccess=*/false,
                /*allowDepthSampledAccess=*/true, /*createDepthCompanion=*/true, /*createColorImage=*/false));
            s.hizAtlas.emplace(s.renderer.CreateRenderTexture(static_cast<int>(layout.atlasWidth),
                static_cast<int>(layout.atlasHeight), PixelFormat::R32_Float, "ClusterViewHizAtlas", nullptr,
                /*allowStorageImageAccess=*/true, /*allowDepthSampledAccess=*/false, /*createDepthCompanion=*/false));
            s.hizSceneWidth = width;
            s.hizSceneHeight = height;
        }

        std::string buildError;
        const auto build = [&](rg::RenderGraphBuilder& builder) {
            std::vector<rg::TextureHandle> outputs;
            const HizBuildPlan plan = HizBuildPlanFor(layout);
            if (!s.hizBaseShader.IsReady(builder) || (plan.runTop && !s.hizTopShader.IsReady(builder))) {
                buildError = "the Hi-Z shader is not ready";
                return outputs;
            }
            Impl::GraphSet set;
            rg::BufferHandle instances;
            if (!s.DeclareCullAll(builder, viewProjection, eye, "ClusterView.Hiz.Reset", set, instances)) {
                buildError = "the Hi-Z cull could not be declared";
                return outputs;
            }
            RenderTexture& depth = *s.hizDepth;
            const rg::TextureHandle depthHandle = builder.ImportTexture("ClusterViewHizDepth", depth.Target(),
                ImageLayout::Undefined, SamplerHandle::Invalid, depth.DepthSamplerObjectHandle(), &depth);
            bool ok = s.DeclareDraw(builder, "ClusterView.Hiz.Draw", set, instances, depthHandle, true, *depthPipeline,
                viewProjection, 0, std::nullopt, 1.0f);

            const rg::TextureHandle atlasHandle = s.ImportHizAtlas(builder, true);
            builder.KeepTextureOutput(atlasHandle);
            outputs.push_back(atlasHandle);
            const HizBuildPush push =
                MakeHizBuildPush(static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height), layout);
            ok = rg::ComputePass(builder, s.hizBaseShader, rg::StaticName::Interned(kHizBasePassName),
                     rg::ViewScope::Shared, rg::RenderPassEvent::Opaques)
                     .Push(push)
                     .Bind("sceneDepth", rg::Depth(depthHandle))
                     .Bind("pyramid", atlasHandle)
                     .Dispatch(Extent2D{ plan.baseThreadsX, plan.baseThreadsY })
                && ok;
            if (plan.runTop) {
                ok = rg::ComputePass(builder, s.hizTopShader, rg::StaticName::Interned(kHizTopPassName),
                         rg::ViewScope::Shared, rg::RenderPassEvent::Opaques)
                         .Push(push)
                         .Bind("pyramid", atlasHandle)
                         .Dispatch(Extent2D{ plan.topThreadsX, 1 })
                    && ok;
            }
            if (!ok) {
                buildError = "a Hi-Z pass could not be declared";
            }
            return outputs;
        };

        if (!s.Execute(build, std::span<RenderTexture* const>(), note) || !buildError.empty()) {
            // The atlas content is undefined after a failed run.
            s.hizInfo.reset();
            if (!buildError.empty()) {
                s.LogOnce(buildError);
            }
            return std::nullopt;
        }

        ClusterViewHizInfo info;
        info.viewProjection = viewProjection;
        info.eye = eye;
        info.width = width;
        info.height = height;
        info.captureId = ++s.hizCaptureCounter;
        s.hizInfo = info;
        return info;
    } catch (const std::exception& e) {
        s.LogOnce(std::string("the Hi-Z capture failed: ") + e.what());
        s.hizInfo.reset();
        return std::nullopt;
    }
}

std::optional<ClusterViewHizInfo> ClusterViewRenderer::HizCapture() const noexcept
{
    return m_impl->hizInfo;
}

void ClusterViewRenderer::ReleaseHiz()
{
    Impl& s = *m_impl;
    if (!s.hizAtlas.has_value() && !s.hizDepth.has_value() && !s.hizInfo.has_value()) {
        return;
    }
    if (!s.DrainGpu("cluster viewer Hi-Z release")) {
        return; // The GPU may still read them; the destructor frees them with the device.
    }
    s.hizInfo.reset();
    s.hizAtlas.reset();
    s.hizDepth.reset();
}

} // namespace gte
