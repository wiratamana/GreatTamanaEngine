// Tier-1 tests for SelectVolumeTexturePreviewInterpretation(): pure name-based selection, no GPU.

#include "Renderer/VolumeTexturePreviewRenderer.h"

#include <gtest/gtest.h>

namespace gte {
namespace {

TEST(VolumeTexturePreviewRendererTest, AerialPerspectiveVolumeNamePrefixSelectsTheFrustumInterpretation)
{
    EXPECT_EQ(SelectVolumeTexturePreviewInterpretation("AtmosphereAerialPerspectiveVolume_GameView"),
        VolumeTexturePreviewInterpretation::AerialPerspectiveFrustum);
    EXPECT_EQ(SelectVolumeTexturePreviewInterpretation("AtmosphereAerialPerspectiveVolume"),
        VolumeTexturePreviewInterpretation::AerialPerspectiveFrustum);
}

TEST(VolumeTexturePreviewRendererTest, AnyOtherNameSelectsGenericInterpretation)
{
    EXPECT_EQ(SelectVolumeTexturePreviewInterpretation("SomeOtherVolumeTexture"),
        VolumeTexturePreviewInterpretation::GenericDensityInAlpha);
    EXPECT_EQ(SelectVolumeTexturePreviewInterpretation(""), VolumeTexturePreviewInterpretation::GenericDensityInAlpha);
}

TEST(VolumeTexturePreviewRendererTest, NamePrefixMatchIsAnchoredAtTheStartNotJustAnySubstring)
{
    // The prefix must be at the very start of the name; containing it elsewhere does not match.
    EXPECT_EQ(SelectVolumeTexturePreviewInterpretation("PrefixedAtmosphereAerialPerspectiveVolume"),
        VolumeTexturePreviewInterpretation::GenericDensityInAlpha);
}

} // namespace
} // namespace gte
