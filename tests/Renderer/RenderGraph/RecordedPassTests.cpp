// Tier-1 tests: the graph's once-log, validated pass bodies and the culling roots of a host readback.

#include "Renderer/RenderGraph/OnceErrorLog.h"
#include "Renderer/RenderGraph/PassKindDefaults.h"
#include "Renderer/RenderGraph/RecordedPass.h"
#include "Renderer/RenderGraph/RenderGraphBuilder.h"
#include "Renderer/RenderGraph/RenderGraphCompiler.h"
#include "Renderer/RenderGraph/TransferPass.h"

#include "../../Fakes/RecordingLogSink.h"

#include <gtest/gtest.h>

#include <optional>
#include <string>

namespace gte::rg {
namespace {

using test::CountByCategory;
using test::RecordingLogSink;
using test::ScopedLogSinkInstall;

const BufferHandle kBufferA{ 1, 1 };
const BufferHandle kBufferB{ 2, 1 };

TEST(OnceErrorLogTest, SameCategoryAndTextLogsOnce)
{
    RecordingLogSink sink;
    const ScopedLogSinkInstall guard(&sink);
    OnceErrorLog log;

    for (int i = 0; i < 5; ++i) {
        log.Error("Cat", "same text");
    }
    EXPECT_EQ(CountByCategory(sink, "Cat"), 1);
}

TEST(OnceErrorLogTest, DifferentTextOrCategoryLogsAgain)
{
    RecordingLogSink sink;
    const ScopedLogSinkInstall guard(&sink);
    OnceErrorLog log;

    log.Error("A", "text");
    log.Error("A", "other text");
    log.Error("B", "text");
    EXPECT_EQ(CountByCategory(sink, "A"), 2);
    EXPECT_EQ(CountByCategory(sink, "B"), 1);
}

TEST(OnceErrorLogTest, FloodOfDistinctTextsEndsWithOneSuppressedNotice)
{
    RecordingLogSink sink;
    const ScopedLogSinkInstall guard(&sink);
    OnceErrorLog log;

    for (int i = 0; i < 400; ++i) {
        log.Error("Flood", "text " + std::to_string(i));
    }
    // The cap's worth of messages plus exactly one notice.
    EXPECT_EQ(CountByCategory(sink, "Flood"), 129);
}

TEST(OnceErrorLogTest, WarningLogsOnceAtWarningLevelAndIsSeparateFromError)
{
    RecordingLogSink sink;
    const ScopedLogSinkInstall guard(&sink);
    OnceErrorLog log;

    log.Warning("Cat", "text");
    log.Warning("Cat", "text");
    log.Error("Cat", "text");
    ASSERT_EQ(sink.entries.size(), 2u);
    EXPECT_EQ(sink.entries[0].level, LogLevel::Warning);
    EXPECT_EQ(sink.entries[1].level, LogLevel::Error);
}

TEST(BuilderReportOnceTest, BareBuilderDedupesThroughItsOwnLog)
{
    RecordingLogSink sink;
    const ScopedLogSinkInstall guard(&sink);
    RenderGraphBuilder builder;

    builder.ReportOnce("Bare", "problem");
    builder.ReportOnce("Bare", "problem");
    EXPECT_EQ(CountByCategory(sink, "Bare"), 1);
}

TEST(BuilderReportOnceTest, SharedLogDedupesAcrossBuilders)
{
    RecordingLogSink sink;
    const ScopedLogSinkInstall guard(&sink);
    OnceErrorLog shared;

    for (int frame = 0; frame < 3; ++frame) {
        RenderGraphBuilder builder;
        builder.SetOnceLog(&shared);
        builder.ReportOnce("Shared", "problem");
    }
    EXPECT_EQ(CountByCategory(sink, "Shared"), 1);
}

TEST(RecordedPassTest, ValidTransferRecordYieldsSetupAndExecute)
{
    RenderGraphBuilder builder;
    const auto body = MakeTransferBody(builder, "Test.Fill", TransferRecord::Fill(kBufferA, 0, 0, 4));
    ASSERT_TRUE(body.has_value());
    EXPECT_TRUE(static_cast<bool>(body->setup));
    EXPECT_TRUE(static_cast<bool>(body->execute));
}

TEST(RecordedPassTest, InvalidTransferRecordYieldsNothingAndLogsOncePerGraph)
{
    RecordingLogSink sink;
    const ScopedLogSinkInstall guard(&sink);
    OnceErrorLog shared;

    for (int frame = 0; frame < 3; ++frame) {
        RenderGraphBuilder builder;
        builder.SetOnceLog(&shared);
        EXPECT_FALSE(MakeTransferBody(builder, "Test.Bad", TransferRecord::CopyBuffer(kBufferA, kBufferA, 4)).has_value());
    }
    EXPECT_EQ(CountByCategory(sink, "TransferPass"), 1);
}

TEST(RecordedPassTest, IndirectDrawNeedsAValidRecordAndAResolver)
{
    RenderGraphBuilder builder;
    const auto resolver = [] { return IndirectDrawInputs{}; };

    EXPECT_FALSE(MakeIndirectDrawBody(builder, "Test.Draw", IndirectDrawRecord{}, resolver).has_value());

    IndirectDrawRecord record;
    record.target = TextureHandle{ 1, 1 };
    record.indirect = kBufferA;
    record.instances = kBufferB;
    record.visible = BufferHandle{ 3, 1 };
    record.count = BufferHandle{ 4, 1 };
    record.maxDrawCount = 4;
    EXPECT_FALSE(MakeIndirectDrawBody(builder, "Test.Draw", record, nullptr).has_value());
    EXPECT_TRUE(MakeIndirectDrawBody(builder, "Test.Draw", record, resolver).has_value());
}

TEST(TransferReadbackTest, RecordMarksTheCopyAsAHostRead)
{
    EXPECT_TRUE(TransferRecord::Readback(kBufferA, kBufferB, 4).hostRead);
    EXPECT_FALSE(TransferRecord::CopyBuffer(kBufferA, kBufferB, 4).hostRead);
}

// Declares `record` the way production does: validated body into AddRenderPass.
bool DeclareTransfer(RenderGraphBuilder& builder, const char* name, const TransferRecord& record)
{
    std::optional<PassBody> body = MakeTransferBody(builder, name, record);
    if (!body.has_value()) {
        return false;
    }
    builder.AddRenderPass(name, DefaultPassKind(DeclarativePassType::Transfer), ViewScope::Primary,
        RenderPassCategory::General, body->setup, body->execute, DefaultDrawKind(DeclarativePassType::Transfer),
        RenderPassEvent::Opaques);
    return true;
}

// A pass whose only output is a host buffer must survive culling when that buffer is kept.
TEST(TransferReadbackTest, ReadbackPassSurvivesCullingBecauseItsBufferIsKept)
{
    RenderGraphBuilder builder;
    const BufferHandle count = builder.ImportBuffer("Count", BufferObjectHandle::Invalid, 4);
    const BufferHandle host = builder.ImportBuffer("Host", BufferObjectHandle::Invalid, 4);

    builder.AddComputePass(
        "WritesCount", [&](RenderGraphBuilder::PassBuilder& pass) { pass.WriteBuffer(count, ResourceAccess::ComputeShaderWrite); },
        [](PassContext&) {});
    ASSERT_TRUE(DeclareTransfer(builder, "Test.CountReadback", TransferRecord::Readback(count, host, 4)));
    builder.KeepBufferOutput(host);

    CompiledGraphInput input = builder.Finish();
    const CompiledGraph compiled = Compile(input, {});

    EXPECT_EQ(compiled.executionOrder.size(), 2u);
    EXPECT_FALSE(input.passes[0].isCulled);
    EXPECT_FALSE(input.passes[1].isCulled);
}

TEST(TransferReadbackTest, ACopyIntoAnUnkeptBufferIsCulled)
{
    RenderGraphBuilder builder;
    const BufferHandle count = builder.ImportBuffer("Count", BufferObjectHandle::Invalid, 4);
    const BufferHandle host = builder.ImportBuffer("Host", BufferObjectHandle::Invalid, 4);

    ASSERT_TRUE(DeclareTransfer(builder, "Test.Copy", TransferRecord::CopyBuffer(count, host, 4)));

    CompiledGraphInput input = builder.Finish();
    const CompiledGraph compiled = Compile(input, {});
    EXPECT_TRUE(compiled.executionOrder.empty());
    EXPECT_TRUE(input.passes[0].isCulled);
}

} // namespace
} // namespace gte::rg
