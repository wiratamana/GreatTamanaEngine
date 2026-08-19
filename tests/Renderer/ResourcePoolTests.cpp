// Unit tests for the generic ResourcePool<T, HandleT> (src/Renderer/
// ResourcePool.h) - insert/remove/lookup + generation-guard semantics only,
// using a plain payload type (no Mesh/Pipeline/Vulkan involved at all) and
// MeshHandle as a convenient already-existing, dependency-free handle type.
// Mirrors ECS/EntityManagerTests.cpp's structure closely on purpose - same
// underlying slot/free-list/generation recipe, just genuinely shared code
// this time (see ResourcePool.h's class comment).

#include "Renderer/MeshHandle.h"
#include "Renderer/ResourcePool.h"

#include <gtest/gtest.h>

#include <string>

namespace gte {
namespace {

// A plain, movable, non-trivial payload - proves ResourcePool works for
// something more than an int (e.g. a real Mesh/Pipeline would be
// move-only/non-copyable too).
struct Widget {
    std::string name;
    int value = 0;
};

using WidgetPool = ResourcePool<Widget, MeshHandle>;

TEST(ResourcePoolTest, InsertReturnsValidHandleAndStoresValue)
{
    WidgetPool pool;
    const MeshHandle handle = pool.Insert(Widget{ "first", 1 });

    EXPECT_TRUE(handle.IsValid());
    ASSERT_TRUE(pool.IsValid(handle));

    const Widget* widget = pool.TryGet(handle);
    ASSERT_NE(widget, nullptr);
    EXPECT_EQ(widget->name, "first");
    EXPECT_EQ(widget->value, 1);
}

TEST(ResourcePoolTest, DefaultConstructedOrInvalidHandleNeverResolves)
{
    WidgetPool pool;
    pool.Insert(Widget{ "unrelated", 0 });

    EXPECT_FALSE(pool.IsValid(MeshHandle{}));
    EXPECT_FALSE(pool.IsValid(kInvalidMeshHandle));
    EXPECT_EQ(pool.TryGet(MeshHandle{}), nullptr);
}

TEST(ResourcePoolTest, InsertReturnsDistinctHandlesForDistinctIndices)
{
    WidgetPool pool;
    const MeshHandle a = pool.Insert(Widget{ "a", 1 });
    const MeshHandle b = pool.Insert(Widget{ "b", 2 });

    EXPECT_NE(a, b);
    EXPECT_NE(a.index, b.index);
    EXPECT_TRUE(pool.IsValid(a));
    EXPECT_TRUE(pool.IsValid(b));
    EXPECT_EQ(pool.TryGet(a)->name, "a");
    EXPECT_EQ(pool.TryGet(b)->name, "b");
}

TEST(ResourcePoolTest, RemoveMakesHandleNoLongerValid)
{
    WidgetPool pool;
    const MeshHandle handle = pool.Insert(Widget{ "gone", 1 });

    EXPECT_TRUE(pool.Remove(handle));

    EXPECT_FALSE(pool.IsValid(handle));
    EXPECT_EQ(pool.TryGet(handle), nullptr);
}

TEST(ResourcePoolTest, RemoveIsSafeToCallTwice)
{
    WidgetPool pool;
    const MeshHandle handle = pool.Insert(Widget{ "gone", 1 });

    EXPECT_TRUE(pool.Remove(handle));
    EXPECT_FALSE(pool.Remove(handle)); // already removed - must not crash, returns false

    EXPECT_FALSE(pool.IsValid(handle));
}

TEST(ResourcePoolTest, RemoveOfNeverInsertedHandleIsNoOp)
{
    WidgetPool pool;

    EXPECT_FALSE(pool.Remove(MeshHandle{ 123, 1 })); // never returned by Insert() on this pool
}

TEST(ResourcePoolTest, ReusesFreedSlotWithBumpedGenerationAndNewValue)
{
    WidgetPool pool;
    const MeshHandle first = pool.Insert(Widget{ "first", 1 });
    pool.Remove(first);
    const MeshHandle second = pool.Insert(Widget{ "second", 2 });

    EXPECT_EQ(first.index, second.index); // slot reused
    EXPECT_NE(first.generation, second.generation); // generation bumped forward
    EXPECT_NE(first, second);

    ASSERT_TRUE(pool.IsValid(second));
    EXPECT_EQ(pool.TryGet(second)->name, "second");
}

TEST(ResourcePoolTest, StaleHandleFromReusedSlotDoesNotResolveToNewValue)
{
    WidgetPool pool;
    const MeshHandle first = pool.Insert(Widget{ "first", 1 });
    pool.Remove(first);
    const MeshHandle second = pool.Insert(Widget{ "second", 2 });
    (void)second;

    // Same index as `second`, but the old (stale) generation - must NOT
    // silently resolve to `second`'s value (the exact use-after-free-style
    // bug generational handles exist to prevent - see AGENTS.md, "Entity-
    // Component-System").
    EXPECT_FALSE(pool.IsValid(first));
    EXPECT_EQ(pool.TryGet(first), nullptr);
}

} // namespace
} // namespace gte
