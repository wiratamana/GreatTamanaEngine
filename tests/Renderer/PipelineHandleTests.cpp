// Unit tests for PipelineHandle's std::hash specialization (Renderer/PipelineHandle.h).

#include "Renderer/PipelineHandle.h"

#include <gtest/gtest.h>

#include <unordered_set>

namespace gte {
namespace {

TEST(PipelineHandleTest, HashDistinguishesDifferentIndexOrGeneration)
{
    const PipelineHandle a{ 1u, 1u };
    const PipelineHandle b{ 2u, 1u };
    const PipelineHandle c{ 1u, 2u };

    const std::hash<PipelineHandle> hasher;
    EXPECT_NE(hasher(a), hasher(b));
    EXPECT_NE(hasher(a), hasher(c));
}

TEST(PipelineHandleTest, HashTreatsEqualHandlesAsEqual)
{
    const PipelineHandle a{ 5u, 7u };
    const PipelineHandle b{ 5u, 7u };

    const std::hash<PipelineHandle> hasher;
    EXPECT_EQ(hasher(a), hasher(b));
}

TEST(PipelineHandleTest, UsableAsAnUnorderedSetKey)
{
    std::unordered_set<PipelineHandle> handles;
    handles.insert(PipelineHandle{ 1u, 1u });
    handles.insert(PipelineHandle{ 2u, 1u });
    handles.insert(PipelineHandle{ 1u, 1u }); // Duplicate - set size stays 2.

    EXPECT_EQ(handles.size(), 2u);
    EXPECT_TRUE(handles.contains(PipelineHandle{ 1u, 1u }));
    EXPECT_TRUE(handles.contains(PipelineHandle{ 2u, 1u }));
    EXPECT_FALSE(handles.contains(PipelineHandle{ 3u, 1u }));
}

} // namespace
} // namespace gte
