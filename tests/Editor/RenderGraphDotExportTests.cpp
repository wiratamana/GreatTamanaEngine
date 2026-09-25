// Unit tests for editor-core-separation-7 campaign's PHASE3
// (task_manager/editor-core-separation-7/
// PHASE3_EDITOR_PANEL_DATA_DRIVEN_MIGRATION_AND_EXPORT_DOT.md) - the real
// "Export DOT" implementation (src/Editor/RenderGraphDotExport.h/.cpp).
// gte_editor-tier (PHASE0_MASTER_STRATEGY.md's Locked Design Decision #14),
// mirroring Editor/AtmosphereAerialPerspectiveLutInspectionTests.cpp's own
// "GreatTamanaEngineTests already links gte_editor" precedent - no live
// Vulkan/RenderGraph/ImGui involved anywhere in this file.
//
// BuildRenderGraphDot() is pure - every fixture here hand-fabricates an
// rg::RenderGraphMetadata directly (all fields are public plain data), never
// via BuildRenderGraphMetadata()/a live RenderGraphSnapshot. A full
// Graphviz-syntax-correctness check is deliberately out of scope (per the
// phase plan) - these tests assert a sane overall shape (starts with
// "digraph", balanced braces) plus real substring content (every pass/
// resource name appears, a culled pass carries the expected style
// attribute, quotes are escaped).

#include "Editor/RenderGraphDotExport.h"

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <sstream>

#include <gtest/gtest.h>

namespace gte {
namespace {

// Counts '{' vs '}' across the whole text - a cheap, real structural sanity
// check for "this is at least well-formed enough to be valid DOT", without
// implementing a full Graphviz parser.
bool HasBalancedBraces(const std::string& text)
{
    std::ptrdiff_t balance = 0;
    for (const char c : text) {
        if (c == '{') {
            ++balance;
        } else if (c == '}') {
            --balance;
        }
    }
    return balance == 0;
}

rg::RenderGraphPassMetadata MakePass(const char* name, bool isCulled)
{
    rg::RenderGraphPassMetadata pass;
    pass.name = name;
    pass.isCulled = isCulled;
    return pass;
}

// --- BuildRenderGraphDot: overall shape --------------------------------

TEST(RenderGraphDotExportTest, EmptyMetadataProducesValidDigraphSkeleton)
{
    rg::RenderGraphMetadata metadata; // Both regimes empty, no batches/features.
    // A hand-fabricated RenderGraphMetadata (not built via
    // BuildRenderGraphMetadata()) starts with an empty regimeName by default -
    // set it explicitly here to the real values BuildRenderGraphMetadata()
    // itself always fills in (RenderGraphMetadata.cpp), so this test
    // exercises the real, expected regime-label text.
    metadata.offscreenRegime.regimeName = "SynchronousImmediateReadback";
    metadata.presentRegime.regimeName = "PipelinedDeferredReadback";

    const std::string dot = BuildRenderGraphDot(metadata);

    EXPECT_EQ(dot.rfind("digraph RenderGraph {", 0), 0u); // Starts with "digraph RenderGraph {".
    EXPECT_TRUE(HasBalancedBraces(dot));
    EXPECT_NE(dot.find("cluster_offscreen"), std::string::npos);
    EXPECT_NE(dot.find("cluster_present"), std::string::npos);
    EXPECT_NE(dot.find("SynchronousImmediateReadback"), std::string::npos);
    EXPECT_NE(dot.find("PipelinedDeferredReadback"), std::string::npos);
}

// --- Passes/resources/edges --------------------------------------------

TEST(RenderGraphDotExportTest, SurvivingPassAppearsWithFilledStyleAndReadWriteEdges)
{
    rg::RenderGraphPassMetadata pass = MakePass("RenderOpaque", /*isCulled=*/false);
    pass.reads.push_back(rg::RenderGraphResourceRefMetadata{ "Depth", "Texture" });
    pass.writes.push_back(rg::RenderGraphResourceRefMetadata{ "Color", "Texture" });

    rg::RenderGraphMetadata metadata;
    metadata.offscreenRegime.passes.push_back(pass);

    const std::string dot = BuildRenderGraphDot(metadata);

    EXPECT_TRUE(HasBalancedBraces(dot));
    EXPECT_NE(dot.find("RenderOpaque"), std::string::npos);
    EXPECT_NE(dot.find("Depth"), std::string::npos);
    EXPECT_NE(dot.find("Color"), std::string::npos);
    EXPECT_NE(dot.find("style=\"filled\""), std::string::npos);
    EXPECT_NE(dot.find("->"), std::string::npos); // At least one real edge.
}

TEST(RenderGraphDotExportTest, CulledPassGetsDashedGreyStyleNotFilled)
{
    const rg::RenderGraphPassMetadata pass = MakePass("UnusedPass", /*isCulled=*/true);

    rg::RenderGraphMetadata metadata;
    metadata.offscreenRegime.passes.push_back(pass);

    const std::string dot = BuildRenderGraphDot(metadata);

    EXPECT_NE(dot.find("UnusedPass"), std::string::npos);
    EXPECT_NE(dot.find("style=\"dashed\""), std::string::npos);
    EXPECT_NE(dot.find("grey"), std::string::npos);
}

TEST(RenderGraphDotExportTest, ResourceReferencedByNameOnlyOnceAcrossMultiplePasses)
{
    rg::RenderGraphPassMetadata writePass = MakePass("WritePass", /*isCulled=*/false);
    writePass.writes.push_back(rg::RenderGraphResourceRefMetadata{ "Shared", "Buffer" });

    rg::RenderGraphPassMetadata readPass = MakePass("ReadPass", /*isCulled=*/false);
    readPass.reads.push_back(rg::RenderGraphResourceRefMetadata{ "Shared", "Buffer" });

    rg::RenderGraphMetadata metadata;
    metadata.offscreenRegime.passes.push_back(writePass);
    metadata.offscreenRegime.passes.push_back(readPass);

    const std::string dot = BuildRenderGraphDot(metadata);

    EXPECT_TRUE(HasBalancedBraces(dot));
    // "Shared" is declared as a node exactly once (one [label="Shared" ...]
    // declaration), even though it is referenced by two different passes.
    const std::size_t firstDecl = dot.find("label=\"Shared\"");
    ASSERT_NE(firstDecl, std::string::npos);
    EXPECT_EQ(dot.find("label=\"Shared\"", firstDecl + 1), std::string::npos);
}

// --- Name escaping -------------------------------------------------------

TEST(RenderGraphDotExportTest, PassNameContainingDoubleQuoteIsEscaped)
{
    const rg::RenderGraphPassMetadata pass = MakePass("Weird\"Pass", /*isCulled=*/false);

    rg::RenderGraphMetadata metadata;
    metadata.offscreenRegime.passes.push_back(pass);

    const std::string dot = BuildRenderGraphDot(metadata);

    EXPECT_TRUE(HasBalancedBraces(dot));
    EXPECT_NE(dot.find("Weird\\\"Pass"), std::string::npos); // The quote was escaped, not left raw.
}

// --- ExportRenderGraphDotToFile: the thin file-writing wrapper -----------

TEST(RenderGraphDotExportTest, ExportRenderGraphDotToFileWritesRealNonEmptyFile)
{
    rg::RenderGraphPassMetadata pass = MakePass("TestExportPass", /*isCulled=*/false);
    pass.reads.push_back(rg::RenderGraphResourceRefMetadata{ "TestExportResource", "Texture" });

    rg::RenderGraphMetadata metadata;
    metadata.offscreenRegime.passes.push_back(pass);

    const std::string resolvedPath = ExportRenderGraphDotToFile(metadata);
    ASSERT_FALSE(resolvedPath.empty());

    std::ifstream file(resolvedPath, std::ios::binary);
    ASSERT_TRUE(file.is_open());
    std::ostringstream buffer;
    buffer << file.rdbuf();
    file.close();

    const std::string contents = buffer.str();
    EXPECT_FALSE(contents.empty());
    EXPECT_NE(contents.find("TestExportPass"), std::string::npos);
    EXPECT_NE(contents.find("TestExportResource"), std::string::npos);

    // Always writes to "render_graph_export.dot" (working-directory-relative,
    // PHASE0_MASTER_STRATEGY.md's Locked Design Decision #13) - confirmed the
    // resolved absolute path really does end with that fixed file name.
    const std::filesystem::path asPath(resolvedPath);
    EXPECT_EQ(asPath.filename().string(), "render_graph_export.dot");

    std::filesystem::remove(asPath); // Test cleanliness - this file is always overwritten anyway, never load-bearing.
}

} // namespace
} // namespace gte
