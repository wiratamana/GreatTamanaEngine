// Unit tests for the small, PURE piece of decision logic living in
// src/Renderer/VolumeTexturePreviewRenderer.h/.cpp -
// SelectVolumeTexturePreviewInterpretation() - no live VkDevice/Renderer/
// VolumeTexturePreviewRenderer instance involved at all (the class itself
// remains Tier-2/untested directly - a real ray-march needs a live VkDevice -
// see AGENTS.md, "Testability & Regression Safety").
//
// frame-debugger-5 campaign, PHASE4
// (task_manager/frame-debugger-5/PHASE4_VOLUME_TEXTURE_RAYMARCH_PREVIEW_REUSE.md)
// - this function was extracted from Application.cpp's own previously-inline
// GET /get_texture volume-texture branch so a second real call site
// (FrameDebuggerHistory::CaptureFrame()) could reuse the exact same rule
// rather than re-deriving it independently by hand.

#include "Renderer/VolumeTexturePreviewRenderer.h"

#include <gtest/gtest.h>

namespace gte {
namespace {

TEST(VolumeTexturePreviewRendererTest, AerialPerspectiveVolumeNamePrefixSelectsAtmosphereInterpretation)
{
    EXPECT_EQ(SelectVolumeTexturePreviewInterpretation("AtmosphereAerialPerspectiveVolume_GameView"),
        VolumeTexturePreviewInterpretation::AtmosphereAerialPerspective);
    EXPECT_EQ(SelectVolumeTexturePreviewInterpretation("AtmosphereAerialPerspectiveVolume"),
        VolumeTexturePreviewInterpretation::AtmosphereAerialPerspective);
}

TEST(VolumeTexturePreviewRendererTest, AnyOtherNameSelectsGenericInterpretation)
{
    EXPECT_EQ(SelectVolumeTexturePreviewInterpretation("SomeOtherVolumeTexture"),
        VolumeTexturePreviewInterpretation::GenericDensityInAlpha);
    EXPECT_EQ(SelectVolumeTexturePreviewInterpretation(""), VolumeTexturePreviewInterpretation::GenericDensityInAlpha);
}

TEST(VolumeTexturePreviewRendererTest, NamePrefixMatchIsAnchoredAtTheStartNotJustAnySubstring)
{
    // The prefix must appear at the very START of the name (rfind(..., 0) ==
    // 0) - a name that merely CONTAINS the substring elsewhere, but doesn't
    // START with it, must NOT match.
    EXPECT_EQ(SelectVolumeTexturePreviewInterpretation("PrefixedAtmosphereAerialPerspectiveVolume"),
        VolumeTexturePreviewInterpretation::GenericDensityInAlpha);
}

} // namespace
} // namespace gte
