// Tier-1 tests for better-render-pass-5 effort, BLOCK 3 ("Wire
// PreOpaque/PostOpaque stages") -
// task_manager/better-render-pass-5/PHASE6_TIER1_TEST_COVERAGE.md.
//
// Covers the source spec's Section 7, Tests 1-3:
//   1. Compiler-level ordering: a RenderPassEvent::PreOpaques-tagged
//      writer always executes before an Opaques-tagged reader.
//   2. The runtime safety net (FindPassesNotTaggedPreOpaque(),
//      src/Core/Plugins/ProjectPreOpaqueCallback.h) genuinely detects
//      the "forgot to tag, fell back to the implicit default" mistake -
//      plus a direct, explicit proof that AddRenderPass()'s own
//      implicit default really is RenderPassEvent::Opaques, the fact
//      this entire design is built around.
//   3. The OPPOSITE mistake (a writer mistagged to a tier LATER than
//      its reader) is caught by the pre-existing, unmodified
//      DetectRenderPassEventContradictions() - mirrors
//      RenderGraphCompilerTests.cpp's own EdgeContradictingDeclaredEventOrderIsDetected
//      precedent exactly, with PreOpaque-flavored names/tiers for this
//      campaign's own explicit regression record.
//
// No Vulkan device/Core/RenderFeatureCompositor involved anywhere in
// this file - pure RenderGraphBuilder/RenderGraphCompiler usage,
// mirroring RenderGraphCompilerTests.cpp's own established convention.

#include "Core/Plugins/ProjectPreOpaqueCallback.h"
#include "Renderer/RenderGraph/RenderGraphCompiler.h"

#include <gtest/gtest.h>

namespace gte {
namespace {

void NoOpExecute(rg::PassContext&) { }

rg::TextureDesc MakeTextureDesc()
{
    return rg::TextureDesc{ 64, 64, VK_FORMAT_R8G8B8A8_UNORM, false };
}

} // namespace

// --- Test 1: compiler-level ordering ----------------------------------------

TEST(PreOpaqueStageOrderingTest, PreOpaquesTaggedWriterExecutesBeforeOpaquesTaggedReader)
{
    rg::RenderGraphBuilder builder;
    const rg::TextureHandle shadowMap = builder.CreateTexture("ShadowMap", MakeTextureDesc());
    // CONFIRMED SPEC BUG FIX (PHASE6_TIER1_TEST_COVERAGE.md's own Test 1
    // literal code): the reader stub originally declared ONLY a
    // ReadTexture(shadowMap), with no write of its own at all - a pass
    // with zero writes can never be kept by RenderGraphCompiler::Compile()'s
    // backward-reachability cull (a pass is only ever kept if it writes a
    // `finalOutputs` root, or a later-kept pass reads from it - neither
    // applies to a pure read-only pass), so the test's own original shape
    // could never produce a 2-pass executionOrder no matter what
    // RenderPassEvent tag either pass carried. Confirmed via a real build +
    // ctest run (executionOrder.size() was 1, not 2) BEFORE this fix, and
    // confirmed via ask_questions this is a genuine spec bug, not an
    // implementation mistake - fixed by giving the reader stub its OWN
    // second output (sceneColor, mirroring the real "RenderOpaque" pass's
    // own shape of reading an upstream input and writing the scene color
    // target), added to finalOutputs so the reader survives culling - the
    // core assertion under test (a PreOpaques-tagged writer always executes
    // before an Opaques-tagged reader) is completely unchanged.
    const rg::TextureHandle sceneColor = builder.CreateTexture("SceneColor", MakeTextureDesc());

    // Declared in a DELIBERATELY ADVERSARIAL order (reader-shaped pass
    // declared FIRST in source) to prove the ordering guarantee comes
    // from RenderPassEvent, never from declaration order.
    builder.AddRenderPass(
        "RenderOpaque_Stub", rg::PassKind::Graphics,
        [&](rg::RenderGraphBuilder::PassBuilder& pass) {
            pass.ReadTexture(shadowMap);
            pass.WriteColorAttachment(sceneColor);
        },
        NoOpExecute, rg::RenderPassDrawKind::DrawMesh, rg::RenderPassEvent::Opaques); // index 0 - reader, declared FIRST.
    builder.AddRenderPass(
        "MyProject.ShadowMap", rg::PassKind::Graphics,
        [&](rg::RenderGraphBuilder::PassBuilder& pass) { pass.WriteColorAttachment(shadowMap); }, NoOpExecute,
        rg::RenderPassDrawKind::DrawMesh, rg::RenderPassEvent::PreOpaques); // index 1 - writer, declared SECOND.

    rg::CompiledGraphInput input = builder.Finish();
    const rg::TextureHandle finalOutputs[] = { sceneColor };
    const rg::CompiledGraph compiled = rg::Compile(input, finalOutputs);

    ASSERT_EQ(compiled.executionOrder.size(), 2u);
    // The PreOpaques pass (declaration index 1) must execute strictly
    // BEFORE the Opaques pass (declaration index 0), despite being
    // declared SECOND - RenderPassEvent, not declaration order, decides
    // this (render-pass-4 campaign's own effective-order sort).
    EXPECT_EQ(compiled.executionOrder[0].index, 1u);
    EXPECT_EQ(compiled.executionOrder[1].index, 0u);
}

// --- Test 2: the runtime safety net's PURE logic ----------------------------

TEST(PreOpaqueStageOrderingTest, FindPassesNotTaggedPreOpaqueReportsEmptyForACorrectlyTaggedPass)
{
    rg::RenderGraphBuilder builder;
    const rg::TextureHandle t0 = builder.CreateTexture("T0", MakeTextureDesc());

    const std::size_t before = builder.DeclaredPassCount();
    builder.AddRenderPass(
        "MyProject.ShadowMap", rg::PassKind::Graphics,
        [&](rg::RenderGraphBuilder::PassBuilder& pass) { pass.WriteColorAttachment(t0); }, NoOpExecute,
        rg::RenderPassDrawKind::DrawMesh, rg::RenderPassEvent::PreOpaques);
    const std::size_t after = builder.DeclaredPassCount();

    EXPECT_TRUE(FindPassesNotTaggedPreOpaque(builder, before, after).empty());
}

// The single most important test in this whole file: proves
// AddRenderPass()'s own implicit default (no trailing RenderPassEvent
// argument at all) really is RenderPassEvent::Opaques - the SAME tier
// "RenderOpaque" itself uses, and therefore the exact, concrete shape of
// the "author forgot to tag it" mistake this whole safety net exists to
// catch. If this test ever fails, every doc comment/strategy document in
// this campaign referencing "the implicit default is Opaques" is wrong
// and must be re-audited.
TEST(PreOpaqueStageOrderingTest, AddRenderPassImplicitDefaultIsReallyOpaques)
{
    rg::RenderGraphBuilder builder;
    const rg::TextureHandle t0 = builder.CreateTexture("T0", MakeTextureDesc());

    builder.AddRenderPass(
        "SimulatedForgotToTagPass", rg::PassKind::Graphics,
        [&](rg::RenderGraphBuilder::PassBuilder& pass) { pass.WriteColorAttachment(t0); }, NoOpExecute);
    // No RenderPassEvent argument supplied at all - relies entirely on
    // AddRenderPass()'s own trailing default.

    ASSERT_EQ(builder.DeclaredPassCount(), 1u);
    EXPECT_EQ(builder.PassEventAt(0), rg::RenderPassEvent::Opaques);
}

// The safety net's actual, end-to-end catch: a PreOpaque feature's
// callback "forgets" to tag its pass and falls back to the implicit
// default - FindPassesNotTaggedPreOpaque() must report that exact index
// as a violation. Mirrors how Core.cpp's own "PreOpaqueFeatures"
// provider actually calls this function (snapshot before/after a
// callback-shaped block of AddRenderPass() calls).
TEST(PreOpaqueStageOrderingTest, FindPassesNotTaggedPreOpaqueCatchesTheForgottenDefaultTagMistake)
{
    rg::RenderGraphBuilder builder;
    const rg::TextureHandle t0 = builder.CreateTexture("T0", MakeTextureDesc());

    const std::size_t before = builder.DeclaredPassCount();
    // Simulates a PreOpaque feature's callback that forgot Rule 1 -
    // leaves the pass at the implicit default (Opaques) instead of
    // explicitly tagging PreOpaques.
    builder.AddRenderPass(
        "MyProject.ForgotToTag", rg::PassKind::Graphics,
        [&](rg::RenderGraphBuilder::PassBuilder& pass) { pass.WriteColorAttachment(t0); }, NoOpExecute);
    const std::size_t after = builder.DeclaredPassCount();

    const std::vector<std::size_t> violations = FindPassesNotTaggedPreOpaque(builder, before, after);
    ASSERT_EQ(violations.size(), 1u);
    EXPECT_EQ(violations[0], before);
    EXPECT_EQ(builder.PassEventAt(violations[0]), rg::RenderPassEvent::Opaques);
}

// A mixed case - one correctly-tagged pass alongside one mistagged pass
// within the SAME [before, after) range - only the mistagged one is
// reported; the correct one never produces a false positive alongside it.
TEST(PreOpaqueStageOrderingTest, FindPassesNotTaggedPreOpaqueReportsOnlyTheOffendingIndexInAMixedRange)
{
    rg::RenderGraphBuilder builder;
    const rg::TextureHandle t0 = builder.CreateTexture("T0", MakeTextureDesc());
    const rg::TextureHandle t1 = builder.CreateTexture("T1", MakeTextureDesc());

    const std::size_t before = builder.DeclaredPassCount();
    builder.AddRenderPass(
        "MyProject.CorrectlyTagged", rg::PassKind::Graphics,
        [&](rg::RenderGraphBuilder::PassBuilder& pass) { pass.WriteColorAttachment(t0); }, NoOpExecute,
        rg::RenderPassDrawKind::DrawMesh, rg::RenderPassEvent::PreOpaques); // index `before` - correct.
    builder.AddRenderPass(
        "MyProject.MistaggedLater", rg::PassKind::Graphics,
        [&](rg::RenderGraphBuilder::PassBuilder& pass) { pass.WriteColorAttachment(t1); }, NoOpExecute,
        rg::RenderPassDrawKind::DrawMesh, rg::RenderPassEvent::AfterOpaques); // index `before` + 1 - wrong.
    const std::size_t after = builder.DeclaredPassCount();

    const std::vector<std::size_t> violations = FindPassesNotTaggedPreOpaque(builder, before, after);
    ASSERT_EQ(violations.size(), 1u);
    EXPECT_EQ(violations[0], before + 1);
}

// Passes OUTSIDE the [before, after) range (declared earlier, unrelated
// to this feature's own callback invocation) must never be inspected,
// even if mistagged themselves - proving the function only ever looks at
// the exact range it was told to.
TEST(PreOpaqueStageOrderingTest, PassesOutsideTheGivenRangeAreNeverInspected)
{
    rg::RenderGraphBuilder builder;
    const rg::TextureHandle tOutside = builder.CreateTexture("TOutside", MakeTextureDesc());
    const rg::TextureHandle tInside = builder.CreateTexture("TInside", MakeTextureDesc());

    // Declared BEFORE `before` is captured - deliberately mistagged, but
    // must never appear in the result since it is outside the range.
    builder.AddRenderPass(
        "UnrelatedMistaggedPass", rg::PassKind::Graphics,
        [&](rg::RenderGraphBuilder::PassBuilder& pass) { pass.WriteColorAttachment(tOutside); }, NoOpExecute);

    const std::size_t before = builder.DeclaredPassCount();
    builder.AddRenderPass(
        "MyProject.CorrectlyTagged", rg::PassKind::Graphics,
        [&](rg::RenderGraphBuilder::PassBuilder& pass) { pass.WriteColorAttachment(tInside); }, NoOpExecute,
        rg::RenderPassDrawKind::DrawMesh, rg::RenderPassEvent::PreOpaques);
    const std::size_t after = builder.DeclaredPassCount();

    EXPECT_TRUE(FindPassesNotTaggedPreOpaque(builder, before, after).empty());
}

// --- Test 3: the OPPOSITE mistake IS caught by the pre-existing,
// unmodified DetectRenderPassEventContradictions() ---------------------------

TEST(PreOpaqueStageOrderingTest, APreOpaqueFeatureMistaggedLaterThanOpaquesIsCaughtByContradictionDetector)
{
    rg::RenderGraphBuilder builder;
    const rg::TextureHandle shadowMap = builder.CreateTexture("ShadowMap", MakeTextureDesc());

    builder.AddRenderPass(
        "MyProject.ShadowMap", rg::PassKind::Graphics,
        [&](rg::RenderGraphBuilder::PassBuilder& pass) { pass.WriteColorAttachment(shadowMap); }, NoOpExecute,
        rg::RenderPassDrawKind::DrawMesh,
        rg::RenderPassEvent::AfterOpaques); // index 0 - writer, DELIBERATELY mistagged LATER than its reader.
    builder.AddRenderPass(
        "RenderOpaque_Stub", rg::PassKind::Graphics,
        [&](rg::RenderGraphBuilder::PassBuilder& pass) { pass.ReadTexture(shadowMap); }, NoOpExecute,
        rg::RenderPassDrawKind::DrawMesh, rg::RenderPassEvent::Opaques); // index 1 - reader.

    rg::CompiledGraphInput input = builder.Finish();
    const std::int32_t identity[] = { 0, 1 };

    const std::vector<rg::RenderPassEventContradiction> contradictions =
        rg::DetectRenderPassEventContradictions(input, identity);

    ASSERT_EQ(contradictions.size(), 1u);
    EXPECT_EQ(contradictions[0].kind, rg::RenderPassEventContradictionKind::DeclaredEventOrderDisagreesWithRealDependency);
    EXPECT_EQ(contradictions[0].writerPassIndex, 0);
    EXPECT_EQ(contradictions[0].readerPassIndex, 1);
}

} // namespace gte
