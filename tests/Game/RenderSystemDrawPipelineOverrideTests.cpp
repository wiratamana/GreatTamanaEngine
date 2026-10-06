// Tier-2 (real, headless GPU) tests for RenderSystem::Draw()'s optional
// pipelineOverride parameter - valid/invalid/mismatched-vertex-layout
// overrides, and the once-per-handle warning-log discipline around each.
//
// Verifies "which Pipeline did entity X actually draw with" via
// IFrameDebuggerCaptureRecorder::RecordFrameDebuggerDraw()'s own resolved
// `const Pipeline&` argument (pointer-identity compared against
// RenderSystem::TryGetPipeline()) - the exact same value Renderer::Submit()
// itself receives - rather than any GPU pixel read-back. Renderer::Submit()
// is safe to call with no active RenderGraph pass recording in progress (it
// queues into an internal draw list, discarded if never consumed) - see
// src/Renderer/Renderer.cpp's own Submit() comment - so none of these tests
// drive an actual RenderGraph::Execute() frame at all.

#include "Game/RenderSystem.h"

#include "../Fakes/HeadlessRenderGraphFixture.h"

#include "Core/FrameDebuggerCaptureRecorder.h"
#include "Core/LogSink.h"
#include "ECS/Components/MeshRenderer.h"
#include "ECS/Components/Transform.h"
#include "ECS/Entity.h"
#include "ECS/Registry.h"
#include "Editor/Logger.h" // LoggerLogSink::Instance() - unconditional, see Core/LogSinkTests.cpp's own precedent.
#include "Renderer/Mesh.h"
#include "Renderer/MeshVertex.h" // MeshVertex - not transitively pulled in by Mesh.h/Renderer.h/Pipeline.h alone.
#include "Renderer/Pipeline.h"
#include "Renderer/Renderer.h"
#include "Renderer/Vertex.h" // Vertex (PositionColor) - the mismatched-layout test's own second mesh format.

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <vector>

namespace gte {
namespace {

class RecordingFrameDebuggerCapture : public IFrameDebuggerCaptureRecorder {
public:
    struct Recorded {
        Entity entity;
        const Pipeline* pipeline = nullptr;
    };

    void RecordFrameDebuggerDraw(Registry& /*registry*/, Renderer& /*renderer*/, Entity entity, const Mesh& /*mesh*/,
        const Pipeline& pipeline, const MaterialTexture* /*materialTexture*/, const Mat4& /*viewProjection*/) override
    {
        recorded.push_back(Recorded{ entity, &pipeline });
    }

    std::vector<rg::TextureHandle> AddReplayPasses(rg::RenderGraphBuilder&, Game&, Renderer&, float, std::size_t,
        const std::vector<rg::BufferHandle>&, const std::function<void(VkCommandBuffer)>&, RenderTexture&,
        rg::RenderPassToggleRegistry* = nullptr) override
    {
        ADD_FAILURE() << "AddReplayPasses() is not expected to be called by any test in this file.";
        return {};
    }

    std::vector<Recorded> recorded;
};

class RecordingLogSink : public ILogSink {
public:
    struct Recorded {
        LogLevel level;
        std::string category;
        std::string message;
    };
    void Log(LogLevel level, std::string_view category, std::string_view message, bool isBlocking = false) override
    {
        entries.push_back(Recorded{ level, std::string(category), std::string(message) });
        (void)isBlocking;
    }
    std::vector<Recorded> entries;
};

class ScopedLogSinkInstall {
public:
    explicit ScopedLogSinkInstall(ILogSink* sink) { InstallLogSink(sink); }
    ~ScopedLogSinkInstall() { InstallLogSink(&LoggerLogSink::Instance()); }
};

// Builds a small, indexed, VertexLayout::PositionNormal quad Mesh - mirrors
// src/Editor/GpuDrivenBatchTestSpawner.cpp's own validation-content quad
// exactly (real, proven, in-tree precedent for hand-built test geometry).
Mesh MakeQuadMesh(Renderer& renderer, const char* debugName)
{
    const MeshVertex vertices[4] = {
        { { -0.5f, -0.5f, 0.0f }, { 0.0f, 0.0f, -1.0f } },
        { { 0.5f, -0.5f, 0.0f }, { 0.0f, 0.0f, -1.0f } },
        { { 0.5f, 0.5f, 0.0f }, { 0.0f, 0.0f, -1.0f } },
        { { -0.5f, 0.5f, 0.0f }, { 0.0f, 0.0f, -1.0f } },
    };
    const std::uint32_t indices[6] = { 0, 1, 2, 2, 3, 0 };
    return renderer.CreateMesh(vertices, sizeof(vertices), 4, indices, sizeof(indices), 6, debugName);
}

// A VertexLayout::PositionColor quad - deliberately a DIFFERENT raw vertex
// format than MakeQuadMesh() above (Vertex, not MeshVertex), matching
// MakePositionColorPipeline() below's own layout.
Mesh MakeColorQuadMesh(Renderer& renderer, const char* debugName)
{
    const Vertex vertices[4] = {
        { { -0.5f, -0.5f, 0.0f }, { 1.0f, 1.0f, 1.0f } },
        { { 0.5f, -0.5f, 0.0f }, { 1.0f, 1.0f, 1.0f } },
        { { 0.5f, 0.5f, 0.0f }, { 1.0f, 1.0f, 1.0f } },
        { { -0.5f, 0.5f, 0.0f }, { 1.0f, 1.0f, 1.0f } },
    };
    const std::uint32_t indices[6] = { 0, 1, 2, 2, 3, 0 };
    return renderer.CreateMesh(vertices, sizeof(vertices), 4, indices, sizeof(indices), 6, debugName);
}

Pipeline MakePositionNormalPipeline(Renderer& renderer, const char* debugName)
{
    return renderer.CreatePipeline("shaders/Mesh.vert.spv", "shaders/Mesh.frag.spv", VertexLayout::PositionNormal,
        /*useMaterialTexture=*/false, debugName);
}

Pipeline MakePositionColorPipeline(Renderer& renderer, const char* debugName)
{
    return renderer.CreatePipeline("shaders/Triangle.vert.spv", "shaders/Triangle.frag.spv",
        VertexLayout::PositionColor, /*useMaterialTexture=*/false, debugName);
}

} // namespace

TEST(RenderSystemDrawPipelineOverrideTest, NulloptOverrideDrawsEachEntityThroughItsOwnPipeline)
{
    HeadlessRenderGraphFixture fixture;
    if (!fixture.IsUsable()) {
        GTEST_SKIP() << fixture.SkipReason();
    }
    Renderer& renderer = fixture.GetRenderer();
    RenderSystem renderSystem;

    const MeshHandle meshHandle = renderSystem.RegisterMesh(MakeQuadMesh(renderer, "Quad"));
    const PipelineHandle pipelineAHandle =
        renderSystem.RegisterPipeline(MakePositionNormalPipeline(renderer, "PipelineA"));
    const PipelineHandle pipelineBHandle =
        renderSystem.RegisterPipeline(MakePositionNormalPipeline(renderer, "PipelineB"));

    Registry registry;
    const Entity entityA = registry.CreateEntity();
    registry.AddComponent<Transform>(entityA);
    registry.AddComponent<MeshRenderer>(entityA, MeshRenderer{ meshHandle, pipelineAHandle });

    const Entity entityB = registry.CreateEntity();
    registry.AddComponent<Transform>(entityB);
    registry.AddComponent<MeshRenderer>(entityB, MeshRenderer{ meshHandle, pipelineBHandle });

    RecordingFrameDebuggerCapture capture;
    renderSystem.Draw(registry, renderer, Mat4::Identity(), &capture);

    ASSERT_EQ(capture.recorded.size(), 2u);
    for (const auto& entry : capture.recorded) {
        if (entry.entity == entityA) {
            EXPECT_EQ(entry.pipeline, renderSystem.TryGetPipeline(pipelineAHandle));
        } else if (entry.entity == entityB) {
            EXPECT_EQ(entry.pipeline, renderSystem.TryGetPipeline(pipelineBHandle));
        } else {
            ADD_FAILURE() << "Unexpected entity recorded.";
        }
    }
}

TEST(RenderSystemDrawPipelineOverrideTest, ValidOverrideForcesEveryEntityThroughTheOverridePipeline)
{
    HeadlessRenderGraphFixture fixture;
    if (!fixture.IsUsable()) {
        GTEST_SKIP() << fixture.SkipReason();
    }
    Renderer& renderer = fixture.GetRenderer();
    RenderSystem renderSystem;

    const MeshHandle meshHandle = renderSystem.RegisterMesh(MakeQuadMesh(renderer, "Quad"));
    const PipelineHandle pipelineAHandle =
        renderSystem.RegisterPipeline(MakePositionNormalPipeline(renderer, "PipelineA"));
    const PipelineHandle pipelineBHandle =
        renderSystem.RegisterPipeline(MakePositionNormalPipeline(renderer, "PipelineB"));
    const PipelineHandle overrideHandle =
        renderSystem.RegisterPipeline(MakePositionNormalPipeline(renderer, "OverridePipeline"));

    Registry registry;
    const Entity entityA = registry.CreateEntity();
    registry.AddComponent<Transform>(entityA);
    registry.AddComponent<MeshRenderer>(entityA, MeshRenderer{ meshHandle, pipelineAHandle });

    const Entity entityB = registry.CreateEntity();
    registry.AddComponent<Transform>(entityB);
    registry.AddComponent<MeshRenderer>(entityB, MeshRenderer{ meshHandle, pipelineBHandle });

    RecordingFrameDebuggerCapture capture;
    renderSystem.Draw(registry, renderer, Mat4::Identity(), &capture, /*maxDrawCount=*/std::nullopt,
        /*batchedEntities=*/{}, overrideHandle);

    ASSERT_EQ(capture.recorded.size(), 2u);
    const Pipeline* overridePipeline = renderSystem.TryGetPipeline(overrideHandle);
    ASSERT_NE(overridePipeline, nullptr);
    for (const auto& entry : capture.recorded) {
        EXPECT_EQ(entry.pipeline, overridePipeline);
    }
}

TEST(RenderSystemDrawPipelineOverrideTest, InvalidOverrideSkipsEveryEntityAndLogsExactlyOneWarning)
{
    HeadlessRenderGraphFixture fixture;
    if (!fixture.IsUsable()) {
        GTEST_SKIP() << fixture.SkipReason();
    }
    Renderer& renderer = fixture.GetRenderer();
    RenderSystem renderSystem;

    const MeshHandle meshHandle = renderSystem.RegisterMesh(MakeQuadMesh(renderer, "Quad"));
    const PipelineHandle pipelineAHandle =
        renderSystem.RegisterPipeline(MakePositionNormalPipeline(renderer, "PipelineA"));

    Registry registry;
    const Entity entityA = registry.CreateEntity();
    registry.AddComponent<Transform>(entityA);
    registry.AddComponent<MeshRenderer>(entityA, MeshRenderer{ meshHandle, pipelineAHandle });

    const PipelineHandle neverRegisteredHandle{ 9999u, 9999u }; // Never returned by RegisterPipeline() above.

    RecordingLogSink sink;
    ScopedLogSinkInstall logGuard(&sink);
    RecordingFrameDebuggerCapture capture;

    renderSystem.Draw(registry, renderer, Mat4::Identity(), &capture, /*maxDrawCount=*/std::nullopt,
        /*batchedEntities=*/{}, neverRegisteredHandle);

    EXPECT_TRUE(capture.recorded.empty());

    int matchingWarningCount = 0;
    for (const auto& entry : sink.entries) {
        if (entry.category == "RenderSystem" && entry.level == LogLevel::Warning) {
            ++matchingWarningCount;
        }
    }
    EXPECT_EQ(matchingWarningCount, 1);
}

TEST(RenderSystemDrawPipelineOverrideTest, NoWarningIsLoggedWhenOverrideIsNulloptOrResolvesSuccessfully)
{
    HeadlessRenderGraphFixture fixture;
    if (!fixture.IsUsable()) {
        GTEST_SKIP() << fixture.SkipReason();
    }
    Renderer& renderer = fixture.GetRenderer();
    RenderSystem renderSystem;

    const MeshHandle meshHandle = renderSystem.RegisterMesh(MakeQuadMesh(renderer, "Quad"));
    const PipelineHandle pipelineAHandle =
        renderSystem.RegisterPipeline(MakePositionNormalPipeline(renderer, "PipelineA"));

    Registry registry;
    const Entity entityA = registry.CreateEntity();
    registry.AddComponent<Transform>(entityA);
    registry.AddComponent<MeshRenderer>(entityA, MeshRenderer{ meshHandle, pipelineAHandle });

    RecordingLogSink sink;
    ScopedLogSinkInstall logGuard(&sink);
    RecordingFrameDebuggerCapture capture;

    renderSystem.Draw(registry, renderer, Mat4::Identity(), &capture); // pipelineOverride defaults to std::nullopt.
    renderSystem.Draw(registry, renderer, Mat4::Identity(), &capture, /*maxDrawCount=*/std::nullopt,
        /*batchedEntities=*/{}, pipelineAHandle); // a VALID override.

    for (const auto& entry : sink.entries) {
        EXPECT_FALSE(entry.category == "RenderSystem" && entry.level == LogLevel::Warning)
            << "Unexpected RenderSystem warning: " << entry.message;
    }
}

TEST(RenderSystemDrawPipelineOverrideTest, MismatchedVertexLayoutOverrideSkipsOnlyMismatchedEntitiesAndLogsOnce)
{
    HeadlessRenderGraphFixture fixture;
    if (!fixture.IsUsable()) {
        GTEST_SKIP() << fixture.SkipReason();
    }
    Renderer& renderer = fixture.GetRenderer();
    RenderSystem renderSystem;

    const MeshHandle normalMeshHandle = renderSystem.RegisterMesh(MakeQuadMesh(renderer, "NormalQuad"));
    const MeshHandle colorMeshHandle = renderSystem.RegisterMesh(MakeColorQuadMesh(renderer, "ColorQuad"));
    const PipelineHandle pipelineAHandle =
        renderSystem.RegisterPipeline(MakePositionNormalPipeline(renderer, "PipelineA"));
    const PipelineHandle pipelineBHandle =
        renderSystem.RegisterPipeline(MakePositionColorPipeline(renderer, "PipelineB"));
    const PipelineHandle overrideHandle =
        renderSystem.RegisterPipeline(MakePositionColorPipeline(renderer, "OverridePipeline"));

    Registry registry;
    // entityA's own pipeline (PositionNormal) MISMATCHES the override
    // (PositionColor) - must be skipped, never drawn.
    const Entity entityA = registry.CreateEntity();
    registry.AddComponent<Transform>(entityA);
    registry.AddComponent<MeshRenderer>(entityA, MeshRenderer{ normalMeshHandle, pipelineAHandle });

    // entityB's own pipeline (PositionColor) MATCHES the override's layout -
    // still drawn, through the override Pipeline.
    const Entity entityB = registry.CreateEntity();
    registry.AddComponent<Transform>(entityB);
    registry.AddComponent<MeshRenderer>(entityB, MeshRenderer{ colorMeshHandle, pipelineBHandle });

    RecordingLogSink sink;
    ScopedLogSinkInstall logGuard(&sink);
    RecordingFrameDebuggerCapture capture;

    renderSystem.Draw(registry, renderer, Mat4::Identity(), &capture, /*maxDrawCount=*/std::nullopt,
        /*batchedEntities=*/{}, overrideHandle);

    ASSERT_EQ(capture.recorded.size(), 1u);
    EXPECT_EQ(capture.recorded[0].entity, entityB);
    EXPECT_EQ(capture.recorded[0].pipeline, renderSystem.TryGetPipeline(overrideHandle));

    int matchingWarningCount = 0;
    for (const auto& entry : sink.entries) {
        if (entry.category == "RenderSystem" && entry.level == LogLevel::Warning) {
            ++matchingWarningCount;
        }
    }
    EXPECT_EQ(matchingWarningCount, 1);
}

TEST(RenderSystemDrawPipelineOverrideTest, RepeatedDrawCallsWithTheSameBadOverrideLogExactlyOneWarningTotal)
{
    HeadlessRenderGraphFixture fixture;
    if (!fixture.IsUsable()) {
        GTEST_SKIP() << fixture.SkipReason();
    }
    Renderer& renderer = fixture.GetRenderer();
    RenderSystem renderSystem;

    const MeshHandle meshHandle = renderSystem.RegisterMesh(MakeQuadMesh(renderer, "Quad"));
    const PipelineHandle pipelineAHandle =
        renderSystem.RegisterPipeline(MakePositionNormalPipeline(renderer, "PipelineA"));

    Registry registry;
    const Entity entityA = registry.CreateEntity();
    registry.AddComponent<Transform>(entityA);
    registry.AddComponent<MeshRenderer>(entityA, MeshRenderer{ meshHandle, pipelineAHandle });

    const PipelineHandle neverRegisteredHandle{ 9999u, 9999u }; // Never returned by RegisterPipeline() above.

    RecordingLogSink sink;
    ScopedLogSinkInstall logGuard(&sink);
    RecordingFrameDebuggerCapture capture;

    // Same bad override handle, across several consecutive "frames" - the
    // warning must fire once total, not once per call.
    for (int frame = 0; frame < 3; ++frame) {
        renderSystem.Draw(registry, renderer, Mat4::Identity(), &capture, /*maxDrawCount=*/std::nullopt,
            /*batchedEntities=*/{}, neverRegisteredHandle);
    }

    EXPECT_TRUE(capture.recorded.empty());

    int matchingWarningCount = 0;
    for (const auto& entry : sink.entries) {
        if (entry.category == "RenderSystem" && entry.level == LogLevel::Warning) {
            ++matchingWarningCount;
        }
    }
    EXPECT_EQ(matchingWarningCount, 1);
}

} // namespace gte
