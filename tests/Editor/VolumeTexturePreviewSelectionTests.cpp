// Tier-1 tests for the editor's name -> volume preview interpretation choice (pure, no GPU).

#include "Editor/VolumeTexturePreviewSelection.h"

#include <gtest/gtest.h>

namespace gte {
namespace {

TEST(VolumeTexturePreviewSelectionTest, AerialPerspectiveVolumeNamePrefixSelectsTheFrustumInterpretation)
{
    EXPECT_EQ(SelectVolumeTexturePreviewInterpretation("AtmosphereAerialPerspectiveVolume_GameView"),
        VolumeTexturePreviewInterpretation::AerialPerspectiveFrustum);
    EXPECT_EQ(SelectVolumeTexturePreviewInterpretation("AtmosphereAerialPerspectiveVolume"),
        VolumeTexturePreviewInterpretation::AerialPerspectiveFrustum);
}

TEST(VolumeTexturePreviewSelectionTest, AnyOtherNameSelectsGenericInterpretation)
{
    EXPECT_EQ(SelectVolumeTexturePreviewInterpretation("SomeOtherVolumeTexture"),
        VolumeTexturePreviewInterpretation::GenericDensityInAlpha);
    EXPECT_EQ(SelectVolumeTexturePreviewInterpretation(""), VolumeTexturePreviewInterpretation::GenericDensityInAlpha);
}

TEST(VolumeTexturePreviewSelectionTest, PrefixMatchIsAnchoredAtTheStart)
{
    EXPECT_EQ(SelectVolumeTexturePreviewInterpretation("PrefixedAtmosphereAerialPerspectiveVolume"),
        VolumeTexturePreviewInterpretation::GenericDensityInAlpha);
}

} // namespace
} // namespace gte
