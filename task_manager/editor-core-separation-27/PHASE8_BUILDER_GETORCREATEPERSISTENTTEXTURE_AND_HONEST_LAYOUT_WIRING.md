# PHASE8 — `RenderGraphBuilder::GetOrCreatePersistentTexture()` + Honest-Layout/Resize-Flush Wiring

Campaign folder: `task_manager/editor-core-separation-27/`

This is the phase where the whole feature becomes REAL and observable for
the first time — the actual entry point a pass author calls, and the tail
hook that makes a persistent texture's layout honest across frames.
Consider a `dispatch_sub_agent` double-check of this phase before writing
its completion report (see `PHASE0_MASTER_STRATEGY.md`, Rule 4) — it is
the second-highest-risk phase in this campaign, after PHASE4.

## Parent — MUST READ
`PHASE0_MASTER_STRATEGY.md`. Also re-read `PHASE7_COMPLETION_REPORT.md` in
full — this phase's own `GetOrCreatePersistentTexture()` is the FIRST real
caller of `RenderGraphPersistentResourceCache::Resolve()`, and the first
thing to read `m_persistentCache`/`m_persistentCacheTimingMode`/
`m_persistentCacheCurrentFrame` (PHASE7's own new builder members).

## Step 1: The Goal (Where are we going?)

1. `RenderGraphBuilder::GetOrCreatePersistentTexture(owner, name, desc)` —
   the plain, always-correct, always-safe entry point (FR1).
2. `RenderGraphBuilder::GetOrCreatePersistentTexture(token, owner, name,
   desc)` — the token-based fast path (FR7), skipping the owned-string
   build and hash-map lookup whenever `token` still references a live
   entry.
3. `RenderGraph::ExecuteCompiledGraph()`'s tail gains the honest-layout-
   recording loop (Section 5.1 — walks `input.persistentCacheTextures`,
   calls the cache's `RecordFinalLayout()`) AND the batched-resize flush
   call (Section 5.5 — `FlushPendingResizes()`, gated to the
   `SynchronousImmediateReadback` regime only), attached at the SAME call
   site as the already-existing `RegisterDebugTextureSnapshots()` call.

When this phase is done: a hand-written pass calling
`GetOrCreatePersistentTexture()` across 3 consecutive real frames gets back
the SAME physical `VkImage`, with its REAL content genuinely surviving
frame-to-frame (no discard) — the single headline capability this whole
campaign exists to deliver.

## Step 2: The Situation (Where are we now?)

- `RenderGraphPersistentResourceCache::Resolve()`'s FINAL signature, after
  PHASE4/5/6, is:
  ```cpp
  std::optional<ResolvedTexture> Resolve(
      const char* owner, const char* name, const TextureDesc& desc,
      std::uint64_t currentFrame, ExecuteTimingMode timingMode);
  ```
  Re-confirm this exact parameter order against the ACTUAL current header
  before writing any call to it — PHASE5/PHASE6 each grew this signature
  by one parameter; confirm both landed in the order described here (if a
  prior phase's own completion report shows a different final order, use
  THAT order, not this document's).
- `RenderGraphBuilder.h` (post-PHASE7) already has
  `m_persistentCache`/`m_persistentCacheTimingMode`/
  `m_persistentCacheCurrentFrame` as private members, and
  `#include "RenderGraphPersistentResourceCache.h"` already present — this
  phase adds no new include there.
- **A genuine, concrete compile gap this phase must ALSO close, confirmed by
  direct read**: `RenderGraphBuilder.cpp` has NEVER used `GTE_LOG_ERROR`
  before (confirmed: `search_in_dir` for `GTE_LOG_ERROR` across
  `src/Renderer/RenderGraph/` finds zero hits anywhere in this folder today)
  — this phase's own two `GetOrCreatePersistentTexture()` overloads (Step
  3.2 below) are the FIRST call sites in this file to use it, so this phase
  must add `#include "../../Core/Logging.h"` to `RenderGraphBuilder.cpp`
  itself (the SAME relative path `RenderGraph.cpp`/`RenderPassGroupRegistry.cpp`
  already use from this exact folder) — without it, this phase's own code
  will not compile. `assert()` needs no new include (`RenderGraphBuilder.h`
  already `#include <cassert>`, confirmed, and is transitively visible from
  the `.cpp`).
- `RenderGraphBuilder::ImportTexture()` (`RenderGraphBuilder.h` ~line 356)
  is the EXISTING, UNCHANGED method this phase's own
  `GetOrCreatePersistentTexture()` calls internally to actually mint this
  frame's `TextureHandle` — confirmed its signature:
  `TextureHandle ImportTexture(const char* name, const RenderTarget&
  externalTarget, VkImageLayout currentLayout);`. The `name` this phase
  passes is `resolved.combinedKey->c_str()` — a pointer into the CACHE's
  own permanently-stable `std::string` (TR4), which the cache's own
  `RenderTexture` already carries as its `debugName` (PHASE4's
  construction recipe) — so `ImportTexture()`'s resulting
  `TextureSlot::name` and the underlying `RenderTexture::m_debugName`
  are, deliberately, THE SAME pointer, one level removed.
- `RenderGraph::ExecuteCompiledGraph()` (`RenderGraph.cpp`), confirmed
  EXACT current tail (~line 825-834):
  ```cpp
  if (isPipelined) {
      ++m_pipelinedFrameCounter;
  }

  RegisterDebugTextureSnapshots(timingMode, input, physicalTextures);
  RegisterDebugVolumeTextureSnapshots(timingMode, input, physicalVolumeTextures);
  ```
  This phase's own new loop + `FlushPendingResizes()` call are inserted
  IMMEDIATELY AFTER these two lines, same function, same cadence — per
  the source document's own Section 5.1/5.5 explicit instruction.
- `RegisterDebugTextureSnapshots()`'s own body (locate via `read_file`
  before editing — not reproduced here in full) already has the EXACT
  defensive pattern this phase's own new loop copies: `if
  (!tex.resolved) { continue; }` for each `TextureSlot` it walks — this
  phase's own loop must have the IDENTICAL guard, since
  `input.persistentCacheTextures` entries are, in the vast majority of
  real frames, already resolved by definition (they were minted via
  `ImportTexture()` inside THIS SAME call), but staying defensive here
  costs nothing and matches house style exactly.
- `TextureSlot::name` (`RenderGraphBuilder.h` ~line 122) is a plain `const
  char*` — `input.textures[h.index].name` is exactly the combined
  `"<owner>::<name>"` key string this phase's new loop needs, with ZERO
  extra bookkeeping (confirmed: this is the SAME string `ImportTexture()`
  was called with, which is itself the cache's own permanently-stable key
  — see PHASE4's construction recipe).

## Step 3: The Plan

### 3.1 — `RenderGraphPersistentResourceCache` gains `RecordFinalLayout()` and `ResolveFast()`

**Recommended implementation approach**: extract a shared, private helper
(e.g. `ResolveAgainstEntry(PersistentResourceCacheEntry& entry, const
TextureDesc& desc, std::uint64_t currentFrame, ExecuteTimingMode
timingMode)`) containing the double-request check, the age-stamps, the
format-change/resize-request handling (PHASE5/PHASE6's own logic, which
today lives inline inside `Resolve()`), and the final `ResolvedTexture`
construction. Both `Resolve()` (after its own `try_emplace`/construction
step) and the new `ResolveFast()` below call this ONE helper — this
avoids duplicating PHASE5/PHASE6's own logic a second time, and guarantees
the token-based fast path can never accidentally skip a check the slow
path enforces (mirrors Locked Decision 6's own reasoning).

```cpp
// Header - additions. NOTE: IsTokenLive() is NOT listed here - it was
// already added by PHASE5 (Section 3.5) and must NOT be re-declared a
// second time (a duplicate member declaration is a compile error) - it is
// shown again ONLY in this phase's own worked `ResolveFast()` example below
// (Step 3.2) as a reminder of its existing signature, never as new text to
// paste into the header a second time.
std::optional<ResolvedTexture> ResolveFast(
    PersistentResourceCacheEntry* entry, const TextureDesc& desc,
    std::uint64_t currentFrame, ExecuteTimingMode timingMode);

// Section 5.1 - `key` is the SAME combined identity string already
// flowing through this whole class (TextureSlot::name, ImportTexture()'s
// own `name` argument). Builds one temporary std::string for the lookup -
// acceptable given this runs at most once per persistent texture per real
// frame (never a hot, per-usage path).
void RecordFinalLayout(const char* key, VkImageLayout layout);
```

```cpp
// .cpp
std::optional<RenderGraphPersistentResourceCache::ResolvedTexture>
RenderGraphPersistentResourceCache::ResolveFast(
    PersistentResourceCacheEntry* entry, const TextureDesc& desc,
    std::uint64_t currentFrame, ExecuteTimingMode timingMode)
{
    // Caller (GetOrCreatePersistentTexture()'s token overload) already
    // confirmed IsTokenLive() before reaching here.
    return ResolveAgainstEntry(*entry, desc, currentFrame, timingMode);
}

void RenderGraphPersistentResourceCache::RecordFinalLayout(const char* key, VkImageLayout layout)
{
    if (key == nullptr) {
        return;
    }
    const auto it = m_entries.find(std::string(key));
    if (it == m_entries.end()) {
        return; // defensive - should not happen in steady state.
    }
    it->second.lastKnownLayout = layout;
}
```

Refactor `Resolve()`'s own existing body (from PHASE4/5/6) so its final
section — from the double-request check through building/returning
`ResolvedTexture` — is moved into the new shared `ResolveAgainstEntry()`
private helper, called once from `Resolve()` (right after the
`try_emplace`/construction block) and once from `ResolveFast()` above.
**This is a pure, zero-behavior-change extraction** — re-run every
PHASE5/PHASE6 test after this refactor to confirm byte-for-byte identical
behavior before proceeding.

### 3.2 — `RenderGraphBuilder::GetOrCreatePersistentTexture()` (both overloads)

```cpp
// RenderGraphBuilder.h - public
TextureHandle GetOrCreatePersistentTexture(const char* owner, const char* name, const TextureDesc& desc);
TextureHandle GetOrCreatePersistentTexture(
    PersistentTextureCacheToken& token, const char* owner, const char* name, const TextureDesc& desc);

// private
TextureHandle MintPersistentHandle(const RenderGraphPersistentResourceCache::ResolvedTexture& resolved);
```

```cpp
// RenderGraphBuilder.cpp
TextureHandle RenderGraphBuilder::MintPersistentHandle(
    const RenderGraphPersistentResourceCache::ResolvedTexture& resolved)
{
    const TextureHandle handle =
        ImportTexture(resolved.combinedKey->c_str(), resolved.texture->Target(), resolved.lastKnownLayout);
    m_persistentCacheTextures.push_back(handle);
    return handle;
}

TextureHandle RenderGraphBuilder::GetOrCreatePersistentTexture(
    const char* owner, const char* name, const TextureDesc& desc)
{
    assert(m_persistentCache != nullptr
        && "RenderGraphBuilder::GetOrCreatePersistentTexture requires a RenderGraphBuilder obtained through "
           "RenderGraph::Execute() (a bare, default-constructed RenderGraphBuilder has no persistent cache)");
    if (m_persistentCache == nullptr) {
        GTE_LOG_ERROR("RenderGraphBuilder",
            "GetOrCreatePersistentTexture() called with no persistent cache installed - refusing.");
        return TextureHandle{};
    }
    const std::optional<RenderGraphPersistentResourceCache::ResolvedTexture> resolved = m_persistentCache->Resolve(
        owner, name, desc, m_persistentCacheCurrentFrame, m_persistentCacheTimingMode);
    if (!resolved.has_value()) {
        return TextureHandle{}; // Resolve() already logged the specific reason - never a second log here.
    }
    return MintPersistentHandle(*resolved);
}

TextureHandle RenderGraphBuilder::GetOrCreatePersistentTexture(
    PersistentTextureCacheToken& token, const char* owner, const char* name, const TextureDesc& desc)
{
    assert(m_persistentCache != nullptr
        && "RenderGraphBuilder::GetOrCreatePersistentTexture requires a RenderGraphBuilder obtained through "
           "RenderGraph::Execute()");
    if (m_persistentCache == nullptr) {
        GTE_LOG_ERROR("RenderGraphBuilder",
            "GetOrCreatePersistentTexture() called with no persistent cache installed - refusing.");
        return TextureHandle{};
    }

    if (m_persistentCache->IsTokenLive(token.entry, token.entryEpoch)) {
#ifndef NDEBUG
        assert(m_persistentCache->DebugTokenIdentityMatches(token.entry, owner, name)
            && "RenderGraphBuilder::GetOrCreatePersistentTexture: this token was already resolved against a "
               "DIFFERENT (owner, name) identity - a token must never be reused across two unrelated identities");
#endif
        const std::optional<RenderGraphPersistentResourceCache::ResolvedTexture> resolved =
            m_persistentCache->ResolveFast(
                token.entry, desc, m_persistentCacheCurrentFrame, m_persistentCacheTimingMode);
        if (!resolved.has_value()) {
            return TextureHandle{}; // e.g. a same-frame double-request - already logged inside Resolve*().
        }
        return MintPersistentHandle(*resolved);
    }

    // Slow path - identical to the no-token overload, then refresh `token`
    // for every subsequent call this session.
    const std::optional<RenderGraphPersistentResourceCache::ResolvedTexture> resolved = m_persistentCache->Resolve(
        owner, name, desc, m_persistentCacheCurrentFrame, m_persistentCacheTimingMode);
    if (!resolved.has_value()) {
        return TextureHandle{};
    }
    token.entry = resolved->entry;
    token.entryEpoch = resolved->entryEpoch;
    return MintPersistentHandle(*resolved);
}
```

### 3.3 — `RenderGraph::ExecuteCompiledGraph()`'s tail hook

Insert, immediately after the existing
`RegisterDebugVolumeTextureSnapshots(...)` line:

```cpp
// editor-core-separation-27 campaign, PHASE8 (BIG STEP 3 of 4, Section
// 5.1/5.5) - honest layout recording, every regime, every call; batched
// resize flush, SynchronousImmediateReadback only.
for (const TextureHandle& h : input.persistentCacheTextures) {
    if (h.index >= physicalTextures.size() || !physicalTextures[h.index].resolved) {
        continue; // defensive - mirrors RegisterDebugTextureSnapshots()'s own identical guard.
    }
    m_persistentResourceCache.RecordFinalLayout(
        input.textures[h.index].name, physicalTextures[h.index].colorState.layout);
}
if (!isPipelined) {
    m_persistentResourceCache.FlushPendingResizes();
}
```

## Step 4: Required Tests

Extend `RenderGraphPersistentResourceCacheTests.cpp` with a NEW block
specifically exercising `RenderGraphBuilder::GetOrCreatePersistentTexture()`
end-to-end through `HeadlessRenderGraphFixture::RunSynchronousFrame()` —
this is where the source document's own single most important acceptance
criteria finally become directly testable:

1. **Same `VkImage` across 3 real frames, via the real builder API** (not
   `cache.Resolve()` directly, unlike PHASE4's own lower-level test):
   ```cpp
   fixture.RunSynchronousFrame([&](rg::RenderGraphBuilder& b) {
       const rg::TextureHandle h = b.GetOrCreatePersistentTexture(
           "Test", "Test", desc);
       // declare a trivial pass reading/writing h, or rely on Section 5.2's
       // own keep-alive guarantee with NO reader at all (see test 3 below).
       return std::vector<rg::TextureHandle>{};
   });
   ```
   repeated 3 times, capturing the real `VkImage` via
   `fixture.GetRenderGraph().DebugTextureSnapshotFor("Test::Test")->target.image`
   after each call — confirm all three are IDENTICAL.
2. **Token-based fast path produces the identical result AND is
   genuinely faster/cheaper** — repeat test 1 using a
   `PersistentTextureCacheToken` member kept alive across all 3 frames.
   Confirm (a) identical `VkImage` reuse, and (b) an instrumented
   confirmation of the fast path actually being taken on calls 2/3 —
   since this codebase has no built-in call-counting hook for
   `Resolve()` vs `ResolveFast()`, add a small, test-only counter (e.g. a
   static/thread-local hit counter incremented inside each method, guarded
   by a build flag/friend test hook, OR — simpler and less invasive —
   confirm indirectly via `DebugTokenIdentityMatches()`'s own debug-assert
   never firing across repeated calls with a STABLE token, which is a
   real, if indirect, proxy). Document exactly which proof mechanism was
   used in the completion report.
3. **Write-on-frame-N, read-correctly-on-frame-N+1 (the concrete
   "no discard" proof)**: declare a real compute pass on frame 1 that
   writes a known pixel pattern into the persistent texture (read-modify-
   write against itself, or a plain write with no reader — Section 5.2's
   own recommended canonical shape); on frame 2, declare a real pass that
   reads it back and records what it saw (e.g. via a readback into a
   small host-visible staging buffer, or via `GET /get_texture`-style
   pixel capture if driving this through a live Editor session is easier
   than plumbing a compute readback through the headless fixture — pick
   whichever is more reliable and say which one was used in the
   completion report). Confirm the SAME known pattern is observed on
   frame 2, never `VK_IMAGE_LAYOUT_UNDEFINED`-discarded garbage.
4. **Keep-alive in practice, through the REAL builder** (not just
   PHASE3's own hand-fabricated `CompiledGraphInput` test): a pass whose
   ONLY declared usage of a `GetOrCreatePersistentTexture()`-minted handle
   is a write, with NOTHING else reading/writing it this frame and the
   handle NOT present in `finalOutputs`, is confirmed to still run every
   frame (not culled) — this proves PHASE3's compiler fix and PHASE8's
   builder wiring genuinely connect end-to-end, not merely in isolation.
5. **Two different owners, same name, same desc — the single most
   important regression test in the whole campaign, now through the real
   builder API**: `b.GetOrCreatePersistentTexture("OwnerA", "History",
   desc)` and `b.GetOrCreatePersistentTexture("OwnerB", "History", desc)`
   in the SAME frame — confirm two DISTINCT `VkImage`s (re-confirms
   PHASE4's own lower-level test, this time through the full,
   production-shaped call path).
6. **`desc.hasDepth == true` / empty owner-name / `PipelinedDeferredReadback`
   resize / mismatched token identity** — re-confirm each of PHASE4/5/6's
   own debug-assert-driven refusals still fire correctly when reached
   through `GetOrCreatePersistentTexture()` itself (not just
   `Resolve()`/`ResolveFast()` directly).

## Step 5: Rules recap

See `PHASE0_MASTER_STRATEGY.md`, Section 3.3, in full. No new files. This
phase's own refactor (Step 3.1's `ResolveAgainstEntry()` extraction) MUST
re-run every PHASE4/5/6 test before adding any new one — confirm zero
regressions from the extraction itself before layering new behavior on
top. Incremental build + `ctest -R RenderGraphPersistentResourceCacheTest`
+ `ctest -R RenderGraphCompilerTest` (PHASE3's own tests, to re-confirm no
interaction). End with `PHASE8_COMPLETION_REPORT.md` + git commit.
