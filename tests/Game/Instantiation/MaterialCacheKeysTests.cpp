#include "Game/Instantiation/MaterialCacheKeys.h"

#include <gtest/gtest.h>

namespace gte {
namespace {

MaterialKey MakeKey(float metallic)
{
    MaterialKey key;
    key.guids[0] = Guid{ 1, 2 };
    key.params = MakeMaterialParams(1.0f, 1.0f, 1.0f, 1.0f, 0.5f, metallic);
    return key;
}

TEST(MaterialCacheKeys, EqualKeysHashEqual)
{
    const MaterialKey a = MakeKey(0.25f);
    const MaterialKey b = MakeKey(0.25f);
    EXPECT_TRUE(a == b);
    EXPECT_EQ(MaterialKeyHash{}(a), MaterialKeyHash{}(b));
}

TEST(MaterialCacheKeys, DifferentParamsOrGuidsAreNotEqual)
{
    const MaterialKey a = MakeKey(0.25f);
    MaterialKey otherParams = MakeKey(0.5f);
    MaterialKey otherGuid = a;
    otherGuid.guids[2] = Guid{ 3, 4 };
    EXPECT_FALSE(a == otherParams);
    EXPECT_FALSE(a == otherGuid);
}

TEST(MaterialCacheKeys, NegativeZeroKeyEqualsPositiveZeroKey)
{
    const MaterialKey positive = MakeKey(0.0f);
    const MaterialKey negative = MakeKey(-0.0f);
    EXPECT_TRUE(positive == negative);
    EXPECT_EQ(MaterialKeyHash{}(positive), MaterialKeyHash{}(negative));
}

TEST(MaterialCacheKeys, ImageKeyDistinguishesColorEncoding)
{
    const MaterialImageKey srgb{ Guid{ 7, 8 }, true };
    const MaterialImageKey linear{ Guid{ 7, 8 }, false };
    EXPECT_FALSE(srgb == linear);
    EXPECT_EQ(MaterialImageKeyHash{}(srgb), MaterialImageKeyHash{}(MaterialImageKey{ Guid{ 7, 8 }, true }));
}

} // namespace
} // namespace gte
