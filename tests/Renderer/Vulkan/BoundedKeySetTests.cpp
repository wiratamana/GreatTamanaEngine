// Tier-1 tests for BoundedKeySet: a key set with a hard size cap.

#include "Renderer/Vulkan/BoundedKeySet.h"

#include <gtest/gtest.h>

namespace gte {
namespace {

using Set = BoundedKeySet<int, 3>;

TEST(BoundedKeySetTest, InsertedKeysAreContained)
{
    Set set;
    EXPECT_FALSE(set.Contains(1));
    EXPECT_TRUE(set.Insert(1));
    EXPECT_TRUE(set.Contains(1));
}

TEST(BoundedKeySetTest, InsertRefusesNewKeysOnceFull)
{
    Set set;
    EXPECT_TRUE(set.Insert(1));
    EXPECT_TRUE(set.Insert(2));
    EXPECT_TRUE(set.Insert(3));
    EXPECT_FALSE(set.Insert(4));
    EXPECT_FALSE(set.Contains(4));
}

TEST(BoundedKeySetTest, ReinsertingAKnownKeyWhenFullSucceeds)
{
    Set set;
    set.Insert(1);
    set.Insert(2);
    set.Insert(3);
    EXPECT_TRUE(set.Insert(2));
}

TEST(BoundedKeySetTest, EraseFreesRoom)
{
    Set set;
    set.Insert(1);
    set.Insert(2);
    set.Insert(3);
    set.Erase(2);
    EXPECT_FALSE(set.Contains(2));
    EXPECT_TRUE(set.Insert(4));
}

} // namespace
} // namespace gte
