// Tier-1 tests for rg::ValidateIndirectDrawRecord (no GPU).

#include "Renderer/RenderGraph/IndirectDrawPass.h"

#include <gtest/gtest.h>

namespace gte::rg {
namespace {

constexpr TextureHandle kTarget{ 1, 1 };
constexpr BufferHandle kIndirect{ 2, 1 };
constexpr BufferHandle kCount{ 3, 1 };
constexpr BufferHandle kInstances{ 4, 1 };
constexpr BufferHandle kVisible{ 5, 1 };

IndirectDrawRecord ValidRecord()
{
    IndirectDrawRecord record;
    record.target = kTarget;
    record.indirect = kIndirect;
    record.count = kCount;
    record.instances = kInstances;
    record.visible = kVisible;
    record.maxDrawCount = 16;
    return record;
}

TEST(IndirectDrawRecord, ValidRecordPasses)
{
    EXPECT_FALSE(ValidateIndirectDrawRecord(ValidRecord()).has_value());
}

TEST(IndirectDrawRecord, DepthOnlyRecordIsAccepted)
{
    IndirectDrawRecord record = ValidRecord();
    record.depthOnly = true;
    EXPECT_FALSE(ValidateIndirectDrawRecord(record).has_value());
}

TEST(IndirectDrawRecord, MissingVisibleListIsRefused)
{
    IndirectDrawRecord record = ValidRecord();
    record.visible = {};
    EXPECT_TRUE(ValidateIndirectDrawRecord(record).has_value());
}

TEST(IndirectDrawRecord, MissingTargetBuffersCountOrDrawCountAreRefused)
{
    IndirectDrawRecord noTarget = ValidRecord();
    noTarget.target = {};
    EXPECT_TRUE(ValidateIndirectDrawRecord(noTarget).has_value());

    IndirectDrawRecord noIndirect = ValidRecord();
    noIndirect.indirect = {};
    EXPECT_TRUE(ValidateIndirectDrawRecord(noIndirect).has_value());

    IndirectDrawRecord noInstances = ValidRecord();
    noInstances.instances = {};
    EXPECT_TRUE(ValidateIndirectDrawRecord(noInstances).has_value());

    IndirectDrawRecord noCount = ValidRecord();
    noCount.count = {};
    EXPECT_TRUE(ValidateIndirectDrawRecord(noCount).has_value());

    IndirectDrawRecord zeroDraws = ValidRecord();
    zeroDraws.maxDrawCount = 0;
    EXPECT_TRUE(ValidateIndirectDrawRecord(zeroDraws).has_value());
}

} // namespace
} // namespace gte::rg
