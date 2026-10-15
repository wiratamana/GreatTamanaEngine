// Tier-2 (headless GPU) tests for GpuParamsBuffer::Upload failure handling.
// Skipped when the headless fixture is unusable.

#include "Renderer/RenderGraph/GpuParams.h"

#include "../../Fakes/HeadlessRenderGraphFixture.h"
#include "../../Fakes/RecordingLogSink.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace gte::rg {
namespace {

using test::CountErrorsByCategory;
using test::RecordingLogSink;
using test::ScopedLogSinkInstall;

} // namespace

// A failed buffer create returns false, logs once and leaves no partial state.
TEST(GpuParamsUploadTest, UploadReturnsFalseAndLogsOnceWhenCreateFails)
{
    HeadlessRenderGraphFixture fixture;
    if (!fixture.IsUsable()) {
        GTEST_SKIP() << fixture.SkipReason();
    }

    RecordingLogSink sink;
    ScopedLogSinkInstall logGuard(&sink);
    GpuParamsBuffer params;
    const char byte = 0;
    bool uploaded = true;
    fixture.RunSynchronousFrame([&](RenderGraphBuilder& b) -> std::vector<TextureHandle> {
        uploaded = params.Upload(b, &byte, std::size_t{ 1 } << 46, "HugeParams");
        uploaded = params.Upload(b, &byte, std::size_t{ 1 } << 46, "HugeParams") || uploaded;
        return {};
    });
    if (uploaded) {
        GTEST_SKIP() << "This driver accepted a 64 TiB buffer; cannot force a failed create.";
    }

    EXPECT_EQ(params.Handle(), BufferObjectHandle::Invalid);
    EXPECT_EQ(CountErrorsByCategory(sink, "GpuParams"), 1);
}

} // namespace gte::rg
