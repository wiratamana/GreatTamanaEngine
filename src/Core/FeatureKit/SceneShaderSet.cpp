#include "SceneShaderSet.h"

#include "FrameContext.h"
#include "GatedBuild.h"
#include "../Core.h"
#include "../Logging.h"
#include "../../Game/Game.h"
#include "../../Game/RenderSystem.h"
#include "../../Game/SceneQuery.h"
#include "../../Renderer/Renderer.h"

#include <array>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace gte::fx {

namespace {

constexpr const char* kLogCategory = "SceneShaderSet";

struct LayoutShader {
    VertexLayout layout;
    const char* suffix;
};

// One vertex shader per supported vertex layout.
constexpr std::array<LayoutShader, 3> kLayoutShaders{ {
    { VertexLayout::PositionColor, "PositionColor" },
    { VertexLayout::PositionNormal, "PositionNormal" },
    { VertexLayout::PositionNormalUv, "PositionNormalUv" },
} };

} // namespace

struct SceneShaderSet::Impl {
    explicit Impl(const char* base)
        : baseName(base != nullptr ? base : "")
    {
    }

    ~Impl()
    {
        Unregister();
    }

    void Unregister() noexcept
    {
        if (system != nullptr) {
            for (const PipelineHandle handle : registered) {
                system->UnregisterPipeline(handle);
            }
        }
        registered.clear();
        overrides = PipelineOverrideSet{};
    }

    // Registers every pipeline; a failure part way unregisters the ones already made and rethrows.
    void Build(Core& core)
    {
        RenderSystem& renderSystem = core.GetGame().GetRenderSystem();
        Renderer& renderer = core.GetRenderer();
        const std::string fragment = "shaders/" + baseName + ".frag.spv";
        std::vector<PipelineHandle> inserted;
        PipelineOverrideSet built;
        try {
            for (const LayoutShader& entry : kLayoutShaders) {
                const std::string vertex = "shaders/" + baseName + entry.suffix + ".vert.spv";
                const std::string debugName = baseName + entry.suffix + ".vert/" + baseName + ".frag (depth-only)";
                const PipelineHandle handle = renderSystem.RegisterPipeline(renderer.CreatePipeline(
                    std::span<const PixelFormat>{}, vertex, fragment, entry.layout, /*useMaterialTexture=*/false, debugName.c_str()));
                inserted.push_back(handle);
                built.byLayout[static_cast<std::size_t>(entry.layout)] = handle;
            }
        } catch (...) {
            for (const PipelineHandle handle : inserted) {
                renderSystem.UnregisterPipeline(handle);
            }
            throw;
        }
        system = &renderSystem;
        registered = std::move(inserted);
        overrides = std::move(built);
    }

    std::string baseName;
    detail::GatedBuild build;
    RenderSystem* system = nullptr; // The system the pipelines live in; it outlives the feature.
    std::vector<PipelineHandle> registered;
    PipelineOverrideSet overrides;
};

SceneShaderSet SceneShaderSet::DepthOnly(const char* baseName)
{
    return SceneShaderSet(std::make_unique<Impl>(baseName));
}

SceneShaderSet::SceneShaderSet(std::unique_ptr<Impl> impl) noexcept
    : m_impl(std::move(impl))
{
}

SceneShaderSet::~SceneShaderSet() = default;
SceneShaderSet::SceneShaderSet(SceneShaderSet&&) noexcept = default;
SceneShaderSet& SceneShaderSet::operator=(SceneShaderSet&&) noexcept = default;

bool SceneShaderSet::Ready() const noexcept
{
    return m_impl != nullptr && m_impl->build.Ready();
}

namespace detail {

bool PrepareSceneShaders(SceneShaderSet& set, FrameContext& context)
{
    if (set.m_impl == nullptr || context.core == nullptr || context.builder == nullptr) {
        return false;
    }
    SceneShaderSet::Impl& impl = *set.m_impl;
    const GatedBuild::Result result =
        impl.build.Ensure(context.builder->FrameIndex(), [&impl, &context] { impl.Build(*context.core); });
    switch (result) {
    case GatedBuild::Result::FailedFirst:
        GTE_LOG_WARNING(kLogCategory,
            "Pipelines '" + impl.baseName + "' failed to build ('" + std::string(impl.build.LastError())
                + "'). Rolled back; retrying with back-off.");
        break;
    case GatedBuild::Result::Recovered:
        GTE_LOG_INFO(kLogCategory, "Pipelines '" + impl.baseName + "' built after an earlier failure.");
        break;
    case GatedBuild::Result::Ready:
    case GatedBuild::Result::Cooling:
    case GatedBuild::Result::FailedAgain:
        break;
    }
    return impl.build.Ready();
}

const PipelineOverrideSet& OverridesOf(const SceneShaderSet& set) noexcept
{
    return set.m_impl->overrides;
}

} // namespace detail

} // namespace gte::fx
