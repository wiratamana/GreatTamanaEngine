#include "JobDispatch.h"

#include "JobSystem.h"

#include <algorithm>

namespace gte::Jobs {

std::vector<BatchRange> ComputeBatchRanges(
    std::uint32_t itemCount, std::uint32_t workerCount, std::uint32_t minItemsPerBatch)
{
    std::vector<BatchRange> ranges;
    if (itemCount == 0) {
        return ranges; // Nothing to do - an empty result, never a single degenerate [0, 0) batch.
    }

    // A zero minItemsPerBatch is treated as "no floor" (equivalent to 1) -
    // never divide by zero below.
    const std::uint32_t effectiveMinItemsPerBatch = minItemsPerBatch == 0 ? 1 : minItemsPerBatch;

    // How many minItemsPerBatch-sized batches would even fit - 0 whenever
    // itemCount itself is smaller than the floor, which is exactly the
    // "collapses to one batch" case (see this function's own header
    // comment).
    const std::uint32_t maxBatchesByFloor = itemCount / effectiveMinItemsPerBatch;

    std::uint32_t batchCount;
    if (maxBatchesByFloor == 0) {
        batchCount = 1;
    } else {
        // Never more than workerCount batches (more cannot run any more in
        // parallel - see this function's own header comment), never fewer
        // than 1, and never more than the floor allows either.
        const std::uint32_t effectiveWorkerCount = workerCount == 0 ? 1 : workerCount;
        batchCount = std::clamp(effectiveWorkerCount, static_cast<std::uint32_t>(1), maxBatchesByFloor);
    }

    ranges.reserve(batchCount);

    // Distribute itemCount as evenly as possible across batchCount batches -
    // the first (itemCount % batchCount) batches get exactly one extra item,
    // so every batch's size differs from every other's by at most 1, and the
    // union of every batch's range is exactly [0, itemCount), contiguous and
    // gap-free, with no overlap.
    const std::uint32_t baseSize = itemCount / batchCount;
    const std::uint32_t remainder = itemCount % batchCount;

    std::uint32_t cursor = 0;
    for (std::uint32_t i = 0; i < batchCount; ++i) {
        const std::uint32_t size = baseSize + (i < remainder ? 1u : 0u);
        ranges.push_back(BatchRange{ cursor, cursor + size });
        cursor += size;
    }

    return ranges;
}

namespace {

// The heap-allocated (see JobDispatch.h's own comment on why) per-batch
// context handed through JobSystem::Schedule()'s opaque `payload` pointer -
// freed by RunBatchJobTrampoline() itself right after invoking the real
// batch job function.
struct DispatchJobContext {
    BatchJobFunction fn;
    void* payload;
    BatchRange range;
};

void RunBatchJobTrampoline(void* rawContext)
{
    // Takes ownership of, and frees, `rawContext` before returning - this is
    // the ONE place a Dispatch()-created DispatchJobContext is ever deleted.
    DispatchJobContext* context = static_cast<DispatchJobContext*>(rawContext);
    context->fn(context->range.beginIndex, context->range.endIndex, context->payload);
    delete context;
}

} // namespace

void Dispatch(BatchJobFunction fn, std::uint32_t itemCount, void* payload, JobHandle& handle,
    std::uint32_t minItemsPerBatch)
{
    if (itemCount == 0) {
        return; // handle stays exactly as complete as it already was - nothing to wait for.
    }

    const std::vector<BatchRange> ranges = ComputeBatchRanges(
        itemCount, static_cast<std::uint32_t>(JobSystem::Instance().WorkerCount()), minItemsPerBatch);

    for (const BatchRange& range : ranges) {
        // See JobDispatch.h's own comment: a deliberate, bounded (at most
        // ranges.size() - itself bounded by WorkerCount() - allocations per
        // Dispatch() call) exception to Phase 1's zero-allocation rule.
        DispatchJobContext* context = new DispatchJobContext{ fn, payload, range };
        JobSystem::Instance().Schedule(&RunBatchJobTrampoline, context, handle);
    }
}

} // namespace gte::Jobs
