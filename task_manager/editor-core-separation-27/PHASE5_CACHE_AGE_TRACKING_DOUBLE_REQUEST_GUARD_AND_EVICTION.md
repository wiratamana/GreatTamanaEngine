# PHASE5 — Age Tracking, Same-Frame Double-Request Refusal, Debug Misuse Guard, and Eviction

Campaign folder: `task_manager/editor-core-separation-27/`

## Parent — MUST READ
`PHASE0_MASTER_STRATEGY.md`, Locked Decisions 4 and 7. Also re-read
`PHASE4_COMPLETION_REPORT.md` in full — this phase EXTENDS
`RenderGraphPersistentResourceCache::Resolve()`'s existing body (grown, not
rewritten) and adds new methods to the SAME class in the SAME two files.

## Step 1: The Goal (Where are we going?)

Three genuinely different correctness concerns, added onto the PHASE4
cache in this one phase because they all revolve around the SAME new
concept — "which real frame is it, right now" — flowing through the SAME
`Resolve()` call:

1. **Eviction** (Section 8): an entry nobody has requested in
   `kPersistentResourceStaleThresholdFrames` (300, Locked Decision 4) real
   frames is destroyed and its map entry erased, freeing its GPU memory —
   with no per-feature action needed to opt in or out.
2. **Same-real-frame double-request refusal** (Section 5.3): if the exact
   same `(owner, name)` identity is requested twice within one real frame
   (a genuine caller bug — the two `ExecuteTimingMode` regimes would then
   be racing on the same shared `VkImage` with zero coordination between
   their command buffers), the SECOND request is REFUSED outright
   (`std::nullopt`), logged exactly once ever per identity, regardless of
   which regime happened to call first.
3. **Debug-only token misuse guard** (Section 4): infrastructure only in
   this phase (the real caller is PHASE8's token-based overload) — a way
   to cheaply confirm, in debug builds only, that a caller's
   `PersistentTextureCacheToken` still refers to the SAME `(owner, name)`
   identity it was originally resolved against.

## Step 2: The Situation (Where are we now?)

- `RenderGraphPersistentResourceCache::Resolve()` (PHASE4) currently takes
  `(owner, name, desc)` only, has no concept of "current frame" at all,
  and never touches `PersistentResourceCacheEntry::lastUsedFrame`/
  `lastRequestedFrame`/`hasLoggedDoubleRequest` — all three fields already
  exist on the struct (declared in PHASE4) but are currently dead weight.
  This phase's job is to make `Resolve()` actually use them.
- **A genuine, narrow, honestly-flagged risk in the source document's own
  design, inherited here on purpose**: `PersistentTextureCacheToken::entry`
  is a raw `PersistentResourceCacheEntry*` that can, in principle, point at
  memory `std::unordered_map::erase()` has already deallocated (if this
  campaign's own eviction, Section 8, removed that exact entry). The
  epoch-comparison safety net (`entryEpoch` field) assumes reading
  `entry->epoch` through that now-dangling pointer either (a) lands on
  genuinely-recycled node memory now belonging to a DIFFERENT, later
  entry with a strictly higher epoch (the safe, intended outcome), or (b)
  is otherwise harmless in practice on this project's actual toolchain
  (MinGW/GCC libstdc++ on Windows) even though the C++ standard does not
  strictly guarantee this is defined behavior for an already-deallocated
  object. **This is stated here PLAINLY, not smoothed over**: this is
  accepted, matching the source document's own design, because (a) it
  only matters on the rare "caller kept a token past its entry's eviction"
  path, never the hot/common path, and (b) this exact "epoch beats a
  dangling generational pointer" trick is already a widely-used, accepted
  pattern in this very codebase (`gte::Entity`'s own
  index+generation shape, `GpuResourceHandle`). This phase's own Step 4
  REQUIRES a real, live test that evicts an entry and then exercises its
  stale token, specifically to confirm this is genuinely safe ON THIS
  PROJECT'S ACTUAL TOOLCHAIN, not merely assumed safe from the design
  document's prose. If that test ever crashes/UB-sanitizer-flags this
  path, STOP and use `ask_questions` — do not silently paper over a real
  memory-safety finding.
- Source document's own precedent for "log an overflow/misuse exactly
  once, never once per frame forever":
  `RenderGraphNameSlotTable::JustOverflowed()`'s own doc comment and
  `RenderGraph.cpp`'s own overflow-reporting block (~line 566-601, using
  `m_reportedSynchronousOverflows`/`m_reportedPipelinedOverflows` as a
  one-time latch vector) — this phase's own `hasLoggedDoubleRequest` bool
  per-entry is the SAME discipline, simplified (a per-entry bool is
  simpler than a parallel vector here, since this cache is already keyed
  by identity).

## Step 3: The Plan

### 3.1 — `Resolve()` grows a `currentFrame` parameter

New signature (grows PHASE4's own, still no other production caller exists
yet — safe to change freely):

```cpp
std::optional<ResolvedTexture> Resolve(
    const char* owner, const char* name, const TextureDesc& desc, std::uint64_t currentFrame);
```

Insert the double-request check and the age-stamps into the EXISTING body,
in this exact order, right after the `try_emplace()` call and BEFORE the
"if (!it->second.texture.has_value())" construction branch:

```cpp
auto [it, inserted] = m_entries.try_emplace(key);

// Section 5.3 - refuse a second request for the SAME identity within the
// SAME real frame, regardless of which ExecuteTimingMode regime called
// first. A brand-new entry's lastRequestedFrame defaults to 0, which
// currentFrame (always >= 1 - see PHASE7's frame-counter seeding) can
// never coincidentally equal, so this never misfires for the first-ever
// request of a new identity.
if (!inserted && it->second.lastRequestedFrame == currentFrame) {
    if (!it->second.hasLoggedDoubleRequest) {
        GTE_LOG_ERROR("RenderGraphPersistentResourceCache",
            "\"" + key + "\" was requested twice within the same real frame (frame "
                + std::to_string(currentFrame) + ") - refusing the second request. Two "
                  "ExecuteTimingMode regimes racing on the same shared VkImage would be a "
                  "genuine GPU data race - see BIG_STEP_3 Section 5.3.");
        it->second.hasLoggedDoubleRequest = true;
    }
    return std::nullopt;
}
it->second.lastRequestedFrame = currentFrame;
it->second.lastUsedFrame = currentFrame; // Section 8's own eviction input - stamped every call, fast path included (PHASE8).
```

Then, AFTER the existing construction branch's `it->second.epoch =
m_nextEntryEpoch++;` line, add:

```cpp
it->second.ownKeyForDebugAssert = &it->first; // stable forever (TR4) - Section 4's debug-only misuse guard.
```

(add the matching `const std::string* ownKeyForDebugAssert = nullptr;`
field to `PersistentResourceCacheEntry` in the header.)

### 3.2 — `BeginFrame()` (Section 8's eviction sweep)

```cpp
// RenderGraphPersistentResourceCache.h - PUBLIC method (RenderGraph::
// BeginPersistentResourceFrame(), PHASE7, is the one real caller, from a
// DIFFERENT class - this cannot be private).
void BeginFrame(std::uint64_t currentFrame, std::uint64_t staleThresholdFrames);
```

```cpp
// .cpp
void RenderGraphPersistentResourceCache::BeginFrame(std::uint64_t currentFrame, std::uint64_t staleThresholdFrames)
{
    for (auto it = m_entries.begin(); it != m_entries.end(); ) {
        if (IsStaleCacheEntry(it->second.lastUsedFrame, currentFrame, staleThresholdFrames)) {
            it = m_entries.erase(it); // destroys the RenderTexture (its destructor runs here).
        } else {
            ++it;
        }
    }
}
```

**Correction — this must be a NAMESPACE-scope constant, never a class `static
constexpr` member**: add
`inline constexpr std::uint64_t kPersistentResourceStaleThresholdFrames = 300;`
directly inside `namespace gte::rg { ... }` in `RenderGraphPersistentResourceCache.h`
(sibling to the class, NOT a member inside it) — mirroring
`RenderGraphNameSlotTable.h`'s own `inline constexpr std::int32_t kNoNameSlot = -1;`
precedent EXACTLY (confirmed: that constant is used bare, unqualified, from
multiple OTHER classes' own `.cpp` files - `RenderGraph.cpp`,
`RenderGraphTimestampPool.cpp` - purely because it is namespace-scope, not a
class member). If this were instead declared as a class `static constexpr`
member (as `RenderGraph::kSynchronousTimingSlotBudget` is, see `RenderGraph.h`),
PHASE7's own `RenderGraph.cpp` — a DIFFERENT class — could not reference it
unqualified merely by including this header; it would need the full
`RenderGraphPersistentResourceCache::kPersistentResourceStaleThresholdFrames`
qualification (confirmed by direct read: `RenderGraphSnapshot.h` already has to
qualify `RenderGraph::kSynchronousTimingSlotBudget` this exact way from outside
`RenderGraph` itself). A namespace-scope constant avoids this entirely and is
usable bare by PHASE7 exactly as that phase's own file describes.

### 3.3 — `FramesUntilEviction()` (Section 9)

```cpp
// Header - PUBLIC methods (Section 9's own external consumers - a future
// Editor "Render Graph" panel/HTTP handler - would call these from OUTSIDE
// this class; neither may be private). PRIMARY overload, takes the
// already-combined identity string
// (the SAME string ListDebugTextures()/DebugTextureSnapshotFor() already
// expose as DebugTextureSnapshot::name for a persistent-cache-backed
// entry - no parsing/splitting needed).
std::optional<std::uint64_t> FramesUntilEviction(const std::string& combinedIdentity, std::uint64_t currentFrame) const;

// CONVENIENCE overload - builds the same combined key internally, forwards.
std::optional<std::uint64_t> FramesUntilEviction(const char* owner, const char* name, std::uint64_t currentFrame) const;
```

```cpp
std::optional<std::uint64_t> RenderGraphPersistentResourceCache::FramesUntilEviction(
    const std::string& combinedIdentity, std::uint64_t currentFrame) const
{
    const auto it = m_entries.find(combinedIdentity);
    if (it == m_entries.end()) {
        return std::nullopt;
    }
    const std::uint64_t elapsed = currentFrame - it->second.lastUsedFrame;
    if (elapsed >= kPersistentResourceStaleThresholdFrames) {
        return 0; // already past due (should be evicted on the NEXT BeginFrame() call).
    }
    return kPersistentResourceStaleThresholdFrames - elapsed;
}

std::optional<std::uint64_t> RenderGraphPersistentResourceCache::FramesUntilEviction(
    const char* owner, const char* name, std::uint64_t currentFrame) const
{
    return FramesUntilEviction(std::string(owner) + "::" + name, currentFrame);
}
```

### 3.4 — Debug-only token misuse guard (infrastructure — PHASE8 is the real caller)

```cpp
// Header - PUBLIC (PHASE8's RenderGraphBuilder - a DIFFERENT class - is the
// real caller). Guarded so a release build never even declares/compiles this:
#ifndef NDEBUG
bool DebugTokenIdentityMatches(const PersistentResourceCacheEntry* entry, const char* owner, const char* name) const;
#endif
```

```cpp
#ifndef NDEBUG
bool RenderGraphPersistentResourceCache::DebugTokenIdentityMatches(
    const PersistentResourceCacheEntry* entry, const char* owner, const char* name) const
{
    if (entry == nullptr || entry->ownKeyForDebugAssert == nullptr) {
        return true; // nothing to compare against - never a false failure.
    }
    return *entry->ownKeyForDebugAssert == (std::string(owner) + "::" + name);
}
#endif
```

Do NOT wire this into `GetOrCreatePersistentTexture()`'s token overload
yet — that overload does not exist until PHASE8. This phase only needs the
method to exist and be independently testable (Step 4).

### 3.5 — Token liveness check (infrastructure — PHASE8 is the real caller)

```cpp
// Header - PUBLIC (PHASE8's RenderGraphBuilder - a DIFFERENT class - is the
// real caller; unlike DebugTokenIdentityMatches() above, this is NOT
// #ifndef NDEBUG-guarded - it is the actual functional fast/slow-path gate,
// needed in release builds too, not merely a debug-only assert helper).
bool IsTokenLive(const PersistentResourceCacheEntry* entry, std::uint64_t entryEpoch) const noexcept
{
    // Deliberately does NOT dereference `entry` through m_entries in any
    // way that requires it to still be a live node - see this phase's own
    // "Situation" section for the accepted, narrow risk this implies, and
    // the required live test (Step 4) proving it in practice on this
    // project's actual toolchain.
    return entry != nullptr && entry->epoch == entryEpoch && entry->epoch != 0;
}
```

## Step 4: Required Tests

Extend `tests/Renderer/RenderGraph/RenderGraphPersistentResourceCacheTests.cpp`
(PHASE4's own new file — no second new test file needed):

1. **Eviction, basic**: `Resolve("Test", "EvictMe", desc, frame=1)`; then
   call `BeginFrame(frame, threshold)` with `frame` walked forward past
   `threshold` (use a SMALL threshold passed directly to `BeginFrame()`
   for this test, e.g. `staleThresholdFrames = 2`, NOT the real 300-frame
   default — confirm `BeginFrame()`'s signature genuinely accepts an
   arbitrary threshold per call, as designed, so a test never has to
   actually iterate 300 real frames). Confirm `FramesUntilEviction("Test",
   "EvictMe", frame)` returns `std::nullopt` after the entry is evicted,
   and confirm (via `GetMemoryResources()`/a totals comparison, mirroring
   PHASE4's own "no depth companion" test technique) that its GPU memory
   was genuinely released.
2. **Eviction does not fire while still in active use**: `Resolve()` the
   same identity every simulated frame, calling `BeginFrame()` between each
   one with the SAME small threshold — confirm the entry is NEVER evicted
   as long as it keeps being requested every frame (this is the direct
   regression test for "an entry requested every real frame always
   evaluates as zero-or-one frames idle at sweep time, never accidentally
   evicted mid-use").
3. **`FramesUntilEviction()` both overloads agree**: after a `Resolve()`
   call, confirm `FramesUntilEviction(combinedIdentityString, frame)` and
   `FramesUntilEviction("Owner", "Name", frame)` return the IDENTICAL
   value.
4. **Double-request refusal, order A**: within ONE simulated frame value
   (`currentFrame == 5` for both calls), call `Resolve("Test", "Dup", desc,
   5)` twice in a row. Confirm the FIRST call succeeds and the SECOND
   returns `std::nullopt`, and confirm (via `GET /get_logs` if running
   through a live Editor session, or a direct log-capture hook if this
   engine's `GTE_LOG_ERROR` supports one for tests — check
   `src/Core/Logging.h` for a test-friendly capture mechanism before
   assuming one exists; if none exists, confirm indirectly via
   `hasLoggedDoubleRequest`-style behavior, i.e. that a THIRD call the
   SAME frame is still refused without crashing/re-logging visibly wrong
   data) that the log fired only once for this identity.
5. **Double-request refusal, order B**: the reverse order is meaningless
   for THIS phase's own tests (order-independence is a `Resolve()`-level,
   regime-agnostic property already proven by test 4 alone, since
   `Resolve()` itself has no concept of which regime called it — regime-
   awareness is entirely PHASE6/PHASE8's own concern for the RESIZE path
   only). Do not fabricate a "regime order" test here that this phase's
   own code cannot actually distinguish.
6. **A request in a LATER frame after a same-frame refusal succeeds
   normally**: after test 4's refused second call, `Resolve("Test", "Dup",
   desc, 6)` (a genuinely later frame) must succeed and return the SAME
   underlying `RenderTexture*` as the original frame-5 call (proves a
   same-frame refusal never poisons the entry for future frames).
7. **Stale-token safety (the accepted-risk test, Step 2's own
   requirement)**: `Resolve()` an identity, capture its `entry`/`epoch`
   into a `PersistentTextureCacheToken`-shaped pair of locals, evict it
   (small threshold + `BeginFrame()`), THEN `Resolve()` a DIFFERENT,
   brand-new identity (to encourage, though never guarantee, the
   allocator reusing the freed node), and finally call `IsTokenLive()`
   with the ORIGINAL (now-stale) entry pointer/epoch. Confirm this returns
   `false` and — most importantly — confirm the process does not crash and
   no sanitizer/AddressSanitizer warning fires (if this build is compiled
   with ASan available, run this specific test under it; if not available
   on this machine, say so plainly in the completion report rather than
   silently skipping the concern).
8. **`DebugTokenIdentityMatches()` — debug builds only**: confirm `true`
   for the identity actually resolved, `false` for a different
   `owner`/`name` pair, and `true` (safe/no-op) for a `nullptr` entry.
   Wrap this whole test in `#ifndef NDEBUG` (mirrors the production code's
   own guard) so a release-configured `ctest` run does not fail to find
   symbols that don't exist in that build.

## Step 5: Rules recap

See `PHASE0_MASTER_STRATEGY.md`, Section 3.3, in full. No new files this
phase — only edits to the two existing
`RenderGraphPersistentResourceCache.h`/`.cpp` files and the existing
PHASE4 test file. Incremental build + `ctest -R
RenderGraphPersistentResourceCacheTest`. End with
`PHASE5_COMPLETION_REPORT.md` + git commit — and if the stale-token test
(Step 4, item 7) reveals a genuine problem on this toolchain, the
completion report must say so honestly and this phase is NOT done until
resolved via `ask_questions`.
