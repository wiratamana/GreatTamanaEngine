#pragma once

// Pure planning of GPU timestamp queries for one frame, and the reverse step that turns raw query values into
// per-pass records. Vulkan-free; the graph executor only does the Vulkan calls.

#include "Culling/ClusterPassCounts.h"
#include "GpuTiming.h"

#include <cstdint>
#include <span>

namespace gte {

// Record budgets per frame (passes beyond the budget fold into one overflow bucket).
inline constexpr std::uint32_t kGpuRecordBudgetSynchronous = 512;
inline constexpr std::uint32_t kGpuRecordBudgetPipelined = 64;

// Passes outside the cluster path that one frame may record.
inline constexpr std::uint32_t kGpuBaseRecordCount = 64;
static_assert(kGpuBaseRecordCount + 2 * kClusterPassNameCount + kHizLevelCount <= kGpuRecordBudgetSynchronous,
    "the synchronous record budget must cover both cluster views, the Hi-Z levels and the base passes");

// A delta above this (or one that wrapped backwards) marks a record as Error.
inline constexpr double kMaxPlausibleGpuMs = 5000.0;

// Queries of one frame: a start/work pair per record and one bucket, then the end (E) and final (Z) stamps.
constexpr std::uint32_t GpuQueryCount(std::uint32_t recordBudget) noexcept
{
    return 2 * (recordBudget + 1) + 2;
}

enum class GpuReadState : std::uint8_t { Present, Partial, Absent, Error, Unsupported, Inactive };

// Fine: a stamp before and after the pass barriers. Coarse: one shared stamp per pass boundary.
enum class GpuStampMode : std::uint8_t { Fine, Coarse };

// Result of the single query-pool read call.
enum class GpuReadCall : std::uint8_t { Ok, NotReady, Failed };

inline constexpr std::uint8_t kGpuRecordSkipped = 1;        // The pass was declared but did not execute.
inline constexpr std::uint8_t kGpuRecordOverflowBucket = 2; // The record stands for several folded passes.
inline constexpr std::uint8_t kGpuRecordStampOrder = 4;     // The stamps were out of order; times are not summed.
inline constexpr std::uint8_t kGpuRecordBarrierUnmeasured = 8; // Coarse stamps: barrierMs is not a measurement.

// One executed pass of one frame, in execution order.
struct GpuPassRecord {
    std::uint64_t bindingInstance = 0;
    std::uint64_t directTriangles = 0;
    std::uint32_t passNameId = 0;
    std::uint32_t featureId = 0;
    std::uint32_t drawCalls = 0;
    std::uint32_t indirectDraws = 0;
    float barrierMs = 0.0f; // Time from the pass start stamp to the stamp after its barriers.
    float workMs = 0.0f;    // Time from that stamp to the start of the next recorded pass.
    std::uint32_t foldedPasses = 0;
    std::uint8_t view = 0;
    std::uint8_t kind = 0;
    std::uint8_t flags = 0;
    GpuReadState state = GpuReadState::Absent;
};
static_assert(sizeof(GpuPassRecord) == 48 && alignof(GpuPassRecord) == 8);

struct GpuStampPassInput {
    bool skipped = false;
};

// Query slots of one input pass. Folded passes share the bucket record and own no queries.
struct GpuStampAssignment {
    std::int32_t startQuery = -1; // Stamp before the pass barriers; -1 = no stamps.
    std::int32_t workQuery = -1;  // Stamp after the barriers; -1 in coarse mode or without stamps.
    std::uint32_t record = 0;     // Index into the frame's record list.
    std::uint8_t flags = 0;
    bool leader = false;          // First pass of its record.
};

struct GpuStampPlan {
    GpuStampMode mode = GpuStampMode::Fine;
    std::uint32_t queryCount = 0;
    std::int32_t endQuery = -1;   // E: right after the last pass work.
    std::int32_t finalQuery = -1; // Z: end of the graph's Execute, before the presenter's capture and present barriers.
    std::uint32_t passCount = 0;  // Input passes that were planned.
    std::uint32_t recordCount = 0;
    std::uint32_t stampedRecords = 0;
    std::uint32_t foldedPasses = 0;
    bool hasBucket = false;
    bool inputTruncated = false;  // More passes than `assignments` could hold.
};

// Assigns query slots. `assignments` must hold one entry per input pass; extra passes are ignored and flagged.
GpuStampPlan PlanGpuStamps(std::span<const GpuStampPassInput> passes, std::span<GpuStampAssignment> assignments,
    GpuStampMode mode, std::uint32_t recordBudget) noexcept;

struct GpuReadInput {
    std::span<const std::uint64_t> values;   // One raw value per query.
    std::span<const std::uint8_t> available; // One availability flag per query.
    GpuTimestampCapability capability;
    bool captureEnabled = true;
    GpuReadCall call = GpuReadCall::Ok;
};

struct GpuFrameReport {
    GpuReadState state = GpuReadState::Absent;
    double spanMs = 0.0; // First pass start to the final stamp.
    double tailMs = 0.0; // Last pass work end to the final stamp.
    double summedMs = 0.0; // Sum of every barrier, work and tail that was measured and not in error.
    bool spanValid = false;
    bool tailValid = false;
    bool barrierMeasured = true;
    std::uint32_t recordCount = 0;
    std::uint32_t foldedPasses = 0;
    std::uint32_t errorRecords = 0;
    std::uint32_t absentRecords = 0;
    std::uint32_t partialRecords = 0;
};

// Fills the timing, state and flag fields of `records` (identity fields are left to the caller).
// `records` must hold at least plan.recordCount entries.
GpuFrameReport ResolveGpuStamps(const GpuStampPlan& plan, std::span<const GpuStampAssignment> assignments,
    const GpuReadInput& input, std::span<GpuPassRecord> records) noexcept;

} // namespace gte
