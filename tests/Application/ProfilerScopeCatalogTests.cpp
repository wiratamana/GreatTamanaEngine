// Tier 1: plain-English text of profiler rows.
#include "Application/ProfilerScopeCatalog.h"

#include <gtest/gtest.h>

#include <set>
#include <string>
#include <string_view>

namespace gte {
namespace {

using Profiling::SampleStatus;
using Profiling::ScopeLane;
using Profiling::ScopeOrigin;
using Profiling::VerdictKind;

struct ExactCase {
    std::string_view name;
    ScopeCategory category;
    bool waiting;
};

constexpr ExactCase kExactCases[] = {
    { "EditorHost::PollEvents", ScopeCategory::Input, false },
    { "EditorHost::ExecuteEngineCommand", ScopeCategory::LoadingAndTools, false },
    { "EditorHost::ExecuteAssetImportCommand", ScopeCategory::LoadingAndTools, false },
    { "EditorHost::ExecuteEditorUiCommand", ScopeCategory::LoadingAndTools, false },
    { "EditorHost::ExecuteFeatureDebugCommand", ScopeCategory::LoadingAndTools, false },
    { "EditorHost::ExecuteFrameDebuggerCommand", ScopeCategory::LoadingAndTools, false },
    { "EditorHost::ExecuteHizDemoMenuAction", ScopeCategory::LoadingAndTools, false },
    { "EditorHost::ExecuteProjectLifecycleLoad", ScopeCategory::LoadingAndTools, false },
    { "EditorHost::ExecuteRenderGraphControlCommand", ScopeCategory::LoadingAndTools, false },
    { "EditorHost::PerformProjectAssemblyHotReload", ScopeCategory::LoadingAndTools, false },
    { "Game::Update", ScopeCategory::Simulation, false },
    { "AnimationSystem::EvaluatePoses", ScopeCategory::Simulation, false },
    { "PhysicsSystem::Update", ScopeCategory::Simulation, false },
    { "AnimationSystem::SkinAndUpload", ScopeCategory::Simulation, false },
    { "Render.Prepare", ScopeCategory::RenderPrepare, false },
    { "Offscreen", ScopeCategory::RenderPrepare, false },
    { "Offscreen.Begin", ScopeCategory::RenderPrepare, false },
    { "Offscreen.Declare", ScopeCategory::RenderPrepare, false },
    { "Offscreen.Collect", ScopeCategory::RenderPrepare, false },
    { "Offscreen.Compile", ScopeCategory::RenderPrepare, false },
    { "Offscreen.Record", ScopeCategory::RenderRecord, false },
    { "Offscreen.Finish", ScopeCategory::RenderPrepare, false },
    { "Offscreen.FinalizeViews", ScopeCategory::RenderPrepare, false },
    { "Offscreen.Submit", ScopeCategory::Waiting, false },
    { "Offscreen.Finalize", ScopeCategory::RenderPrepare, false },
    { "Present", ScopeCategory::RenderPrepare, false },
    { "Present.Declare", ScopeCategory::RenderPrepare, false },
    { "Present.Compile", ScopeCategory::RenderPrepare, false },
    { "Present.Record", ScopeCategory::RenderRecord, false },
    { "Present.Finish", ScopeCategory::RenderPrepare, false },
    { "RenderSystem::CollectRenderables", ScopeCategory::RenderPrepare, false },
    { "RenderSystem::Draw", ScopeCategory::RenderRecord, false },
    { "ClusterPart.Bounds", ScopeCategory::RenderPrepare, false },
    { "ClusterPart.Skip", ScopeCategory::RenderPrepare, false },
    { "ClusterPart.Claim", ScopeCategory::RenderPrepare, false },
    { "IEditorLayer::BuildUI", ScopeCategory::EditorUi, false },
    { "Panel.DockspaceAndMenuBar", ScopeCategory::EditorUi, false },
    { "Panel.ProjectWindows", ScopeCategory::EditorUi, false },
    { "Panel.ViewDescriptors", ScopeCategory::EditorUi, false },
    { "Panel.SceneCameraAdapter", ScopeCategory::EditorUi, false },
    { "Project.StartScan", ScopeCategory::LoadingAndTools, false },
    { "Project.ApplyScan", ScopeCategory::LoadingAndTools, false },
    { "Scene.Load", ScopeCategory::LoadingAndTools, false },
    { "Scene.Save", ScopeCategory::LoadingAndTools, false },
    { "Scene.Destroy", ScopeCategory::LoadingAndTools, false },
    { "Wait.Workers", ScopeCategory::Waiting, true },
    { "Wait.TargetResize", ScopeCategory::Waiting, true },
    { "Wait.OneShotSubmit", ScopeCategory::Waiting, true },
    { "SkinVertices", ScopeCategory::Simulation, false },
    { "PackMeshVertices", ScopeCategory::Simulation, false },
    { "PackMeshVertexUvs", ScopeCategory::Simulation, false },
    { "StepDynamicChain", ScopeCategory::Simulation, false },
    { "MeshClusterBuild", ScopeCategory::LoadingAndTools, false },
    { "Project.EnsureRoot", ScopeCategory::LoadingAndTools, false },
    { "Project.ScanTree", ScopeCategory::LoadingAndTools, false },
    { "Project.LinkedFolder", ScopeCategory::LoadingAndTools, false },
};

constexpr std::string_view kWaitSites[] = {
    "PreviousOffscreen", "Offscreen", "PresentInFlight", "AcquireImage", "PresentQueue",
    "ImmediateSubmit", "WarmUp", "SwapchainRecreate", "TargetResize", "CaptureReadback",
    "ResourceRelease", "CodeUnload", "Shutdown", "ImGuiBackend",
};

TEST(ProfilerScopeCatalogTest, EveryExactNameHasItsListedText)
{
    for (const ExactCase& c : kExactCases) {
        const ScopeInfo info = DescribeScope(c.name, ScopeLane::Main, ScopeOrigin::Engine);
        EXPECT_FALSE(info.title.empty()) << c.name;
        EXPECT_FALSE(info.explanation.empty()) << c.name;
        EXPECT_FALSE(info.generic) << c.name;
        EXPECT_EQ(info.category, c.category) << c.name;
        EXPECT_EQ(info.waiting, c.waiting) << c.name;
    }
}

TEST(ProfilerScopeCatalogTest, ExactNameBeatsPrefixRule)
{
    const ScopeInfo exact = DescribeScope("Panel.ViewDescriptors", ScopeLane::Main, ScopeOrigin::Engine);
    EXPECT_EQ(exact.title, "View pictures");
    const ScopeInfo prefix = DescribeScope("Panel.Sample", ScopeLane::Main, ScopeOrigin::Engine);
    EXPECT_NE(prefix.title.find("Sample"), std::string::npos);
}

TEST(ProfilerScopeCatalogTest, PrefixRulesCarryTheNamePart)
{
    const struct {
        std::string_view name;
        ScopeCategory category;
    } cases[] = {
        { "Pass.Sample.Clear", ScopeCategory::RenderRecord },
        { "Declare.SampleFeatures", ScopeCategory::RenderPrepare },
        { "Feature.Sample", ScopeCategory::RenderFeature },
        { "Panel.Sample", ScopeCategory::EditorUi },
    };
    for (const auto& c : cases) {
        const ScopeInfo info = DescribeScope(c.name, ScopeLane::Main, ScopeOrigin::Engine);
        const std::string_view part = c.name.substr(c.name.find('.') + 1);
        EXPECT_NE(info.title.find(part), std::string::npos) << c.name;
        EXPECT_EQ(info.category, c.category) << c.name;
        EXPECT_FALSE(info.generic) << c.name;
    }

    const ScopeInfo callback = DescribeScope("Feature.A.cb", ScopeLane::Main, ScopeOrigin::Engine);
    EXPECT_NE(callback.title.find("A"), std::string::npos);
    EXPECT_NE(callback.title.find("cb"), std::string::npos);
    EXPECT_EQ(callback.category, ScopeCategory::RenderFeature);
}

TEST(ProfilerScopeCatalogTest, EveryGpuWaitSiteHasItsOwnExplanation)
{
    std::set<std::string> explanations;
    for (const std::string_view site : kWaitSites) {
        const std::string name = "GpuWait(" + std::string(site) + ")";
        const ScopeInfo info = DescribeScope(name, ScopeLane::Main, ScopeOrigin::Engine);
        EXPECT_TRUE(info.waiting) << name;
        EXPECT_EQ(info.category, ScopeCategory::Waiting) << name;
        EXPECT_FALSE(info.generic) << name;
        EXPECT_TRUE(explanations.insert(info.explanation).second) << name;
    }

    const ScopeInfo detail = DescribeScope("GpuWait(Offscreen:offscreen submit)", ScopeLane::Main, ScopeOrigin::Engine);
    EXPECT_NE(detail.title.find("offscreen submit"), std::string::npos);
    EXPECT_FALSE(detail.hint.empty());
    const ScopeInfo vsync = DescribeScope("GpuWait(AcquireImage)", ScopeLane::Main, ScopeOrigin::Engine);
    EXPECT_NE(vsync.hint.find("--no-vsync"), std::string::npos);
}

TEST(ProfilerScopeCatalogTest, UnknownNamesFallBackByOrigin)
{
    const ScopeInfo project = DescribeScope("sample::Widget::Update", ScopeLane::Main, ScopeOrigin::Project);
    EXPECT_EQ(project.category, ScopeCategory::ProjectCode);
    EXPECT_EQ(project.title, "sample::Widget::Update");
    EXPECT_NE(project.explanation.find("your project"), std::string::npos);

    for (const ScopeOrigin origin : { ScopeOrigin::Engine, ScopeOrigin::Unknown }) {
        const ScopeInfo info = DescribeScope("sample::Other", ScopeLane::Main, origin);
        EXPECT_EQ(info.category, ScopeCategory::Other);
        EXPECT_EQ(info.title, "sample::Other");
        EXPECT_TRUE(info.generic);
    }
}

TEST(ProfilerScopeCatalogTest, GpuLaneUsesGpuWording)
{
    const ScopeInfo pass = DescribeScope("Sample.Pass", ScopeLane::Gpu, ScopeOrigin::Unknown);
    EXPECT_EQ(pass.category, ScopeCategory::GpuWork);
    EXPECT_NE(pass.title.find("Sample.Pass"), std::string::npos);
    EXPECT_FALSE(pass.waiting);

    const ScopeInfo folded = DescribeScope("(folded passes)", ScopeLane::Gpu, ScopeOrigin::Unknown);
    EXPECT_EQ(folded.category, ScopeCategory::GpuWork);
    EXPECT_FALSE(folded.explanation.empty());

    const ScopeInfo feature = DescribeGpuFeature("Sample");
    EXPECT_EQ(feature.category, ScopeCategory::GpuWork);
    EXPECT_NE(feature.title.find("Sample"), std::string::npos);
}

TEST(ProfilerScopeCatalogTest, EveryVerdictHasAdvice)
{
    for (const VerdictKind kind : { VerdictKind::Unknown, VerdictKind::Unreliable, VerdictKind::Stalled,
             VerdictKind::PresentLimited, VerdictKind::GpuLimited, VerdictKind::CpuLimited, VerdictKind::Serial }) {
        const char* text = DescribeVerdict(kind);
        ASSERT_NE(text, nullptr);
        EXPECT_NE(std::string_view(text), std::string_view{});
    }
}

TEST(ProfilerScopeCatalogTest, EveryCategoryHasALabel)
{
    for (std::size_t i = 0; i < kScopeCategoryCount; ++i) {
        EXPECT_NE(std::string_view(CategoryLabel(static_cast<ScopeCategory>(i))), std::string_view{});
    }
}

TEST(ProfilerScopeCatalogTest, SummaryFormat)
{
    PlainSummaryInput input;
    input.frameMs = 25.0;
    input.cpuBusyMs = 6.4;
    input.cpuWaitMs = 18.6;
    input.gpuMs = 24.3;
    input.gpuStatus = SampleStatus::Present;
    EXPECT_EQ(BuildPlainSummary(input),
        "This frame took 25.0 ms (40 FPS). The CPU worked for 6.4 ms and waited for 18.6 ms. The GPU worked for 24.3 ms.");

    input.gpuStatus = SampleStatus::Absent;
    EXPECT_EQ(BuildPlainSummary(input),
        "This frame took 25.0 ms (40 FPS). The CPU worked for 6.4 ms and waited for 18.6 ms. "
        "GPU time is not available (absent).");

    input.frameMs = 0.0;
    EXPECT_EQ(BuildPlainSummary(input), "No frame data yet.");
}

} // namespace
} // namespace gte
