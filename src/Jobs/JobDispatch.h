#pragma once

#include "JobTypes.h"

#include <cstdint>
#include <vector>

namespace gte::Jobs {

// A half-open [beginIndex, endIndex) contiguous range of item indices - one
// batch's own share of a Dispatch() call's total itemCount.
struct BatchRange {
    std::uint32_t beginIndex = 0;
    std::uint32_t endIndex = 0;
};

// A batch-shaped job function: told which half-open range of the original
// Dispatch()'s itemCount it is responsible for, plus whatever user context
// it needs via the same raw-payload convention Phase 1's plain JobFunction
// already uses (JobTypes.h) - never a std::function, for the exact same
// "no allocation/type-erasure on the hot per-job path" reason.
using BatchJobFunction = void (*)(std::uint32_t beginIndex, std::uint32_t endIndex, void* payload);

// Pure function - no JobSystem, no threads, no scheduling, deterministic.
// Splits [0, itemCount) into a bounded number of contiguous, non-overlapping,
// gap-free BatchRanges:
//   - itemCount == 0                    -> an empty result (nothing to do).
//   - itemCount < minItemsPerBatch      -> exactly ONE batch, [0, itemCount) -
//     the "collapses to one batch" floor: never split smaller than the
//     caller's own stated per-batch cost floor, even if that means fewer
//     batches than there are workers.
//   - otherwise                          -> up to `workerCount` batches (never
//     more - additional batches beyond the worker count cannot run any more
//     in parallel and only add scheduling overhead), each as close to equal
//     size as possible: batchCount = clamp(workerCount, 1, itemCount /
//     max(1, minItemsPerBatch)), then itemCount is distributed as evenly as
//     possible across batchCount batches (the first itemCount % batchCount
//     batches get exactly one extra item - the same "as even as possible"
//     splitting convention this engine's primitive-mesh generators already
//     use elsewhere for evenly distributing counts).
// See JOBSYSTEM_PHASE2_PARALLEL_FOR_DISPATCH.md, Step 3.2, for the full
// design rationale this implements.
std::vector<BatchRange> ComputeBatchRanges(
    std::uint32_t itemCount, std::uint32_t workerCount, std::uint32_t minItemsPerBatch);

// Splits `itemCount` items into a bounded number of batches (see
// ComputeBatchRanges() above for the exact rule - the batch count is derived
// automatically from JobSystem::Instance().WorkerCount(), never something a
// caller has to compute by hand) and Schedule()s one job per batch, each
// invoking fn(batchBegin, batchEnd, payload). Every batch shares the SAME
// `payload` pointer (read-only sharing - `payload`'s lifetime remains
// entirely the caller's own responsibility, same convention as Phase 1's
// Schedule()) and the SAME `handle` (so a single WaitForJobs(handle) call
// waits for the whole dispatch, never one call per batch).
//
// itemCount == 0 is an immediate, job-free no-op - `handle` stays exactly as
// complete as it already was; nothing is scheduled at all.
//
// A NECESSARY, DELIBERATE, DOCUMENTED exception to Phase 1's own "zero heap
// allocation in the steady-state per-job path" guarantee: each batch's own
// small context (the function pointer + user payload pointer + this batch's
// own BatchRange) is heap-allocated via `new`, freed by the job itself right
// before it returns. This is required because each batch needs its OWN
// distinct [begin, end) range, and that per-batch range has to outlive
// Dispatch() returning (Dispatch() does not block). The number of these
// allocations per call is bounded by ComputeBatchRanges()'s own batchCount
// ceiling (at most WorkerCount() - a handful, never one per array element),
// not an unbounded/hot-path allocation - see
// JOBSYSTEM_PHASE2_PARALLEL_FOR_DISPATCH.md, Step 3.3, for the full
// rationale and the future fixed-size-pool escape hatch if this were ever
// found to matter in practice.
void Dispatch(BatchJobFunction fn, std::uint32_t itemCount, void* payload, JobHandle& handle,
    std::uint32_t minItemsPerBatch = 1);

} // namespace gte::Jobs
