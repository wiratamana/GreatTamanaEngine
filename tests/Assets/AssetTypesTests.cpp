// Unit tests for src/Assets/AssetTypes.h - Guid value semantics/generation/
// string round-trip, AssetFlags bit ops, and AssetTypeFromExtension()'s
// pure extension->AssetType mapping. No filesystem/GPU/ImGui involved -
// genuinely Tier 1, always built (this module has no GTE_ENABLE_EDITOR/
// GTE_ENABLE_PROJECT_PANEL dependency at all).

#include "Assets/AssetTypes.h"

#include <unordered_set>

#include <gtest/gtest.h>

namespace gte {
namespace {

// --- Guid --------------------------------------------------------------

TEST(GuidTest, DefaultConstructedIsInvalid)
{
    const Guid guid;
    EXPECT_FALSE(guid.IsValid());
    EXPECT_EQ(guid, Guid::Invalid());
}

TEST(GuidTest, GenerateProducesAValidGuid)
{
    EXPECT_TRUE(Guid::Generate().IsValid());
}

TEST(GuidTest, GenerateProducesDistinctValuesAcrossManyCalls)
{
    std::unordered_set<std::string> seen;
    constexpr int kCount = 1000;
    for (int i = 0; i < kCount; ++i) {
        seen.insert(Guid::Generate().ToString());
    }
    EXPECT_EQ(seen.size(), static_cast<std::size_t>(kCount));
}

TEST(GuidTest, EqualityComparesBothHalves)
{
    const Guid a{ 1, 2 };
    const Guid b{ 1, 2 };
    const Guid c{ 1, 3 };
    EXPECT_EQ(a, b);
    EXPECT_NE(a, c);
}

TEST(GuidTest, ToStringIsLowercase32HexDigits)
{
    const Guid guid{ 0x0123456789abcdefull, 0xfedcba9876543210ull };
    const std::string text = guid.ToString();
    EXPECT_EQ(text.size(), 32u);
    EXPECT_EQ(text, "fedcba98765432100123456789abcdef");
}

TEST(GuidTest, ParseRoundTripsToStringOutput)
{
    const Guid original = Guid::Generate();
    const Guid parsed = Guid::Parse(original.ToString());
    EXPECT_EQ(original, parsed);
}

TEST(GuidTest, ParseReturnsInvalidForMalformedInput)
{
    EXPECT_EQ(Guid::Parse("too_short"), Guid::Invalid());
    EXPECT_EQ(Guid::Parse(""), Guid::Invalid());
    EXPECT_EQ(Guid::Parse(std::string(32, 'z')), Guid::Invalid()); // Not valid hex.
}

TEST(GuidTest, HashAllowsUseAsUnorderedMapKey)
{
    std::unordered_map<Guid, int> map;
    const Guid a = Guid::Generate();
    const Guid b = Guid::Generate();
    map[a] = 1;
    map[b] = 2;
    EXPECT_EQ(map[a], 1);
    EXPECT_EQ(map[b], 2);
}

// --- AssetFlags ----------------------------------------------------------

TEST(AssetFlagsTest, HasFlagDetectsSetBit)
{
    const AssetFlags flags = AssetFlags::Compressed;
    EXPECT_TRUE(HasFlag(flags, AssetFlags::Compressed));
    EXPECT_FALSE(HasFlag(flags, AssetFlags::Encrypted));
}

TEST(AssetFlagsTest, OrCombinesBothFlags)
{
    const AssetFlags flags = AssetFlags::Compressed | AssetFlags::Encrypted;
    EXPECT_TRUE(HasFlag(flags, AssetFlags::Compressed));
    EXPECT_TRUE(HasFlag(flags, AssetFlags::Encrypted));
}

TEST(AssetFlagsTest, NoneHasNoFlagsSet)
{
    EXPECT_FALSE(HasFlag(AssetFlags::None, AssetFlags::Compressed));
    EXPECT_FALSE(HasFlag(AssetFlags::None, AssetFlags::None)); // None is never "has" any flag, including itself.
}

// --- AssetTypeFromExtension() ---------------------------------------------

TEST(AssetTypeFromExtensionTest, RecognizesCommonImageExtensions)
{
    EXPECT_EQ(AssetTypeFromExtension(".png"), AssetType::Texture);
    EXPECT_EQ(AssetTypeFromExtension(".jpg"), AssetType::Texture);
    EXPECT_EQ(AssetTypeFromExtension(".ktx2"), AssetType::Texture);
}

TEST(AssetTypeFromExtensionTest, RecognizesCommonMeshExtensions)
{
    EXPECT_EQ(AssetTypeFromExtension(".obj"), AssetType::Mesh);
    EXPECT_EQ(AssetTypeFromExtension(".gltf"), AssetType::Mesh);
}

TEST(AssetTypeFromExtensionTest, RecognizesCommonAudioExtensions)
{
    EXPECT_EQ(AssetTypeFromExtension(".wav"), AssetType::Audio);
    EXPECT_EQ(AssetTypeFromExtension(".ogg"), AssetType::Audio);
}

TEST(AssetTypeFromExtensionTest, ReturnsUnknownForUnrecognizedExtension)
{
    EXPECT_EQ(AssetTypeFromExtension(".xyz"), AssetType::Unknown);
    EXPECT_EQ(AssetTypeFromExtension(""), AssetType::Unknown);
}

} // namespace
} // namespace gte
