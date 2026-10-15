// Tier-1 tests: the pure upload policy behind GpuParams.

#include "Renderer/RenderGraph/GpuParams.h"

#include <gtest/gtest.h>

namespace gte::rg {
namespace {

TEST(GpuParamsPolicyTest, FirstUploadCreatesTheBuffer)
{
    EXPECT_EQ(DecideGpuParamsUpload(0, 144, false), GpuParamsUploadDecision::Create);
}

TEST(GpuParamsPolicyTest, SameSizeOverwrites)
{
    EXPECT_EQ(DecideGpuParamsUpload(144, 144, false), GpuParamsUploadDecision::Overwrite);
}

TEST(GpuParamsPolicyTest, DifferentSizeIsRefused)
{
    EXPECT_EQ(DecideGpuParamsUpload(144, 160, false), GpuParamsUploadDecision::RefuseSizeMismatch);
    EXPECT_EQ(DecideGpuParamsUpload(144, 16, false), GpuParamsUploadDecision::RefuseSizeMismatch);
}

TEST(GpuParamsPolicyTest, PipelinedRegimeIsRefused)
{
    EXPECT_EQ(DecideGpuParamsUpload(0, 144, true), GpuParamsUploadDecision::RefusePipelined);
    EXPECT_EQ(DecideGpuParamsUpload(144, 144, true), GpuParamsUploadDecision::RefusePipelined);
}

TEST(GpuParamsPolicyTest, EmptyUploadIsRefused)
{
    EXPECT_EQ(DecideGpuParamsUpload(0, 0, false), GpuParamsUploadDecision::RefuseEmpty);
    EXPECT_EQ(DecideGpuParamsUpload(144, 0, true), GpuParamsUploadDecision::RefuseEmpty);
}

TEST(GpuParamsPolicyTest, HandleIsInvalidBeforeTheFirstUpload)
{
    const GpuParams<float> params;
    EXPECT_EQ(params.Handle(), BufferObjectHandle::Invalid);
}

} // namespace
} // namespace gte::rg
