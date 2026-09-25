# PHASE1 — Built-In Pass Toggle Registry + `RenderPipeline` Choke Point

**Parent:** `PHASE0_MASTER_STRATEGY.md` (read it FIRST — especially Locked
Product Decisions #5/#6/#9 and Locked Architecture Decisions #11/#12). Use
`ask_questions` for any genuine ambiguity not already resolved there.

## Step 1: The Goal

Ship a new, generic, `debugName`-keyed registry
(`gte::rg::RenderPassToggleRegistry`) that can answer "is pass X currently
enabled" and be told "turn pass X on/off", wire it into the ONE real choke
point every built-in pass (offscreen AND present regime alike) funnels
through every frame (`RenderPipeline::DeclareOnePhase()`), and have `Core` own
and expose it. By the end of this phase, disabling a pass through the new
registry's `SetEnabled()` genuinely stops that pass from ever reaching
`RenderGraphBuilder::AddRenderPass()` the very next frame — but nothing in
production code calls `SetEnabled()` yet (that's PHASE4/PHASE5's job); this
phase is pure, additive plumbing, Tier-1 tested.

## Step 2: The Situation

Read these exact files in full before writing any code (do not rely on
summaries below — they are accurate but not exhaustive):

- `src/Renderer/RenderGraph/RenderPipeline.h` — the WHOLE file. Pay special
  attention to:
  - `RenderPassDesc` (the `id`/`debugName`/`order` fields).
  - `RenderPipeline::SetLegacyViewScopeTranslator()` (public method) — THE
    exact injection pattern this phase's own new
    `SetPassToggleRegistry()` mirrors: a `nullptr`/empty-by-default member,
    settable once, consulted inside `DeclareOnePhase()`, falling back to
    "unchanged old behavior" whenever never set.
  - `RenderPipeline::DeclareOnePhase()` (private method) — the exact flush
    loop (`for (RenderPassDesc& desc : m_scratchCollected) { ... builder.AddRenderPass(...); }`)
    this phase adds ONE new `continue`-based skip check to, immediately
    before the existing `builder.AddRenderPass(...)` call.
- `src/Core/Core.cpp` — search for `SetLegacyViewScopeTranslator(` to find
  the EXACT existing call site(s) that already wire
  `m_offscreenRenderPipeline`/`m_presentRenderPipeline` with their translator
  — this phase's own 2 new `SetPassToggleRegistry(...)` calls go
  IMMEDIATELY alongside those, in the same constructor-time wiring block.
  Do NOT touch any of the ~10 `Register(...)` lambda bodies elsewhere in
  this file — this phase does not need to, and must not (Locked Product
  Decision #5).
- `src/Core/Core.h` — find where `m_offscreenRenderPipeline`/
  `m_presentRenderPipeline` are declared as members, to place the new
  `rg::RenderPassToggleRegistry m_renderPassToggleRegistry;` member sensibly
  nearby (a plain, owned value member — never a pointer/`std::unique_ptr`,
  since this registry has no Vulkan/heavy dependency and needs no lazy
  construction).
- `tests/Renderer/RenderGraph/RenderPipelineTests.cpp` — the existing
  Tier-1 test file for `RenderPipeline` (already exists — confirmed via
  `browse_dir`). Read its existing test fixture/helper shape (how it
  constructs a `RenderPipeline` and a fake `RenderPassFrameContext`/
  `RenderGraphBuilder` for a test) — this phase's own new test(s) proving
  "a disabled desc is skipped" must reuse that SAME fixture shape, not
  invent a new one.
- `tests/CMakeLists.txt` — find the existing line registering
  `Renderer/RenderGraph/RenderPipelineTests.cpp` (search for it) to see the
  exact style used for adding a new sibling test file
  (`Renderer/RenderGraph/RenderPassToggleRegistryTests.cpp`, this phase) and
  the root `CMakeLists.txt`'s own `gte_core` source list (search for
  `RenderPipeline.h`/`.cpp` to find where to insert the two new
  `RenderPassToggleRegistry.h`/`.cpp` lines, immediately alongside).

## Step 3: The Plan

### Step 3.1 — New file: `src/Renderer/RenderGraph/RenderPassToggleRegistry.h`

```cpp
#pragma once

// editor-core-separation-8 campaign, PHASE1 - a small, generic, gte_core-tier
// registry answering "is this built-in pass, by its own debugName string,
// currently enabled" - the missing piece requirement #1 (from the
// "Better Render Graph Editor" story) needs for BUILT-IN passes specifically
// (see PHASE0_MASTER_STRATEGY.md's Locked Product Decision #5/#6 for why this
// is keyed by debugName string rather than the existing-but-never-stamped
// RenderPassId).
//
// Deliberately NOT thread-safe (no mutex) - this class is touched EXCLUSIVELY
// from the main thread: RenderPipeline::DeclareOnePhase()'s own declare loop
// (every frame), the "Render Graph" panel's own checkbox handling (PHASE4),
// and RenderGraphControlCommandBridge's own main-thread pump (PHASE5) - see
// PHASE0_MASTER_STRATEGY.md's Locked Architecture Decision #12.
//
// Auto-discovery, not a hardcoded pass-name list (Locked Product Decision
// #6): NoteDeclaredAndCheckEnabled() is the ONLY place a NEW entry is ever
// created with everDeclaredThisSession == true - the first time
// RenderPipeline::DeclareOnePhase() actually sees a given debugName this
// session, it is auto-registered here, defaulting to enabled = true (zero
// behavior change for any pass nobody has touched). SetEnabled() may ALSO
// create a brand-new entry (with everDeclaredThisSession == false) if a
// caller pre-disables a pass that has genuinely never run yet this session -
// a legal, if unusual, forward-looking state.

#include <string>
#include <unordered_map>
#include <vector>

namespace gte::rg {

// One known pass's toggle state - returned by ListAll(), and (PHASE5) is
// exactly what GET /render_graph/passes reports per entry.
struct RenderPassToggleState {
    std::string name;
    bool enabled = true;
    // True once RenderPipeline::DeclareOnePhase() has actually seen this
    // pass's RenderPassDesc at least once this session (see this class's own
    // header comment above for the "SetEnabled() may pre-create an entry
    // with this false" case).
    bool everDeclaredThisSession = false;
};

class RenderPassToggleRegistry {
public:
    RenderPassToggleRegistry() = default;

    // Called by RenderPipeline::DeclareOnePhase(), once per collected
    // RenderPassDesc, EVERY frame - for BOTH ProviderTiming phases, for BOTH
    // regimes (offscreen AND present share the SAME registry instance - see
    // Core.cpp's wiring). Auto-discovers `name` the first time it is ever
    // seen (creates a new entry, enabled = true, everDeclaredThisSession =
    // true); on every subsequent call for an already-known name, simply
    // (re)stamps everDeclaredThisSession = true (a harmless, idempotent
    // no-op once already true) and returns the CURRENT enabled state
    // unchanged. Returns true (enabled) for an empty `name` defensively -
    // never disables anything by accident on a malformed/null debugName.
    bool NoteDeclaredAndCheckEnabled(const std::string& name);

    // Called by Core's own mutator method (Core::SetBuiltInRenderPassEnabled(),
    // PHASE1 too - see Step 3.3 below), in turn called from EITHER the
    // "Render Graph" panel's own checkbox (PHASE4, main thread, synchronous,
    // same frame) OR RenderGraphControlCommandBridge's pump (PHASE5, also
    // main-thread-only, never concurrent with the frame that calls
    // NoteDeclaredAndCheckEnabled() above - see PHASE5's own pump placement).
    // Upserts `name` if never seen before (may create an entry with
    // everDeclaredThisSession == false - see this class's own header
    // comment). Returns false, WITHOUT applying the change, if `name` is on
    // the permanent deny-list (IsDenyListed()) - true otherwise (including
    // when `enabled` already equalled the current value - a harmless no-op
    // "success").
    bool SetEnabled(const std::string& name, bool enabled);

    // Current enabled state for `name` - true (the safe default) if `name`
    // has never been seen by either method above. Used by the panel (PHASE4)
    // to initialize each checkbox's displayed value.
    bool IsEnabled(const std::string& name) const;

    // Every currently-known entry, sorted by `name` (lexical) for
    // deterministic iteration order in both the ImGui panel (PHASE4) and the
    // GET /render_graph/passes JSON body (PHASE5) - an unordered_map has no
    // useful iteration order of its own to expose directly.
    std::vector<RenderPassToggleState> ListAll() const;

    // True for a small, fixed, permanent set of pass names that can NEVER be
    // disabled via SetEnabled() - currently: "Present" only (the pass that
    // finally writes the swapchain image; disabling it leaves the Editor
    // rendering nothing to the screen with no in-process recovery). A plain
    // static function (not a data member) so it has zero interaction with
    // any registry instance's own state - deliberately extensible (add
    // another `name ==` comparison here later) without ever needing to
    // touch SetEnabled()'s own body.
    static bool IsDenyListed(const std::string& name) noexcept;

private:
    std::unordered_map<std::string, RenderPassToggleState> m_entries;
};

} // namespace gte::rg
```

### Step 3.2 — New file: `src/Renderer/RenderGraph/RenderPassToggleRegistry.cpp`

Straightforward, ~40-line implementation matching the header's contract
exactly:

```cpp
#include "RenderPassToggleRegistry.h"

#include <algorithm>

namespace gte::rg {

bool RenderPassToggleRegistry::NoteDeclaredAndCheckEnabled(const std::string& name)
{
    if (name.empty()) {
        return true;
    }
    auto it = m_entries.find(name);
    if (it == m_entries.end()) {
        RenderPassToggleState state;
        state.name = name;
        state.enabled = true;
        state.everDeclaredThisSession = true;
        it = m_entries.emplace(name, std::move(state)).first;
        return it->second.enabled;
    }
    it->second.everDeclaredThisSession = true;
    return it->second.enabled;
}

bool RenderPassToggleRegistry::SetEnabled(const std::string& name, bool enabled)
{
    if (IsDenyListed(name)) {
        return false;
    }
    auto it = m_entries.find(name);
    if (it == m_entries.end()) {
        RenderPassToggleState state;
        state.name = name;
        state.enabled = enabled;
        state.everDeclaredThisSession = false;
        m_entries.emplace(name, std::move(state));
        return true;
    }
    it->second.enabled = enabled;
    return true;
}

bool RenderPassToggleRegistry::IsEnabled(const std::string& name) const
{
    const auto it = m_entries.find(name);
    return it == m_entries.end() ? true : it->second.enabled;
}

std::vector<RenderPassToggleState> RenderPassToggleRegistry::ListAll() const
{
    std::vector<RenderPassToggleState> result;
    result.reserve(m_entries.size());
    for (const auto& [name, state] : m_entries) {
        result.push_back(state);
    }
    std::sort(result.begin(), result.end(),
        [](const RenderPassToggleState& a, const RenderPassToggleState& b) { return a.name < b.name; });
    return result;
}

bool RenderPassToggleRegistry::IsDenyListed(const std::string& name) noexcept
{
    return name == "Present";
}

} // namespace gte::rg
```

### Step 3.3 — `RenderPipeline.h` changes

1. `#include "RenderPassToggleRegistry.h"` near the top (alongside the
   existing `#include "RenderGraphTypes.h"`).
2. Public method, placed immediately AFTER `SetLegacyViewScopeTranslator()`
   (same visual grouping):

```cpp
// editor-core-separation-8 campaign, PHASE1 - see RenderPassToggleRegistry.h's
// own header comment for the full contract. Defaults to nullptr - every
// existing call site that never calls this keeps its exact prior behavior
// (every pass always enabled), mirroring SetLegacyViewScopeTranslator()'s own
// identical "unset = old behavior" discipline immediately above.
void SetPassToggleRegistry(RenderPassToggleRegistry* registry) noexcept
{
    m_passToggleRegistry = registry;
}
```

3. New private member, next to `m_legacyViewScopeTranslator`:

```cpp
RenderPassToggleRegistry* m_passToggleRegistry = nullptr;
```

4. Inside `DeclareOnePhase()`'s existing flush loop, immediately BEFORE the
   existing `builder.AddRenderPass(...)` call, add:

```cpp
if (m_passToggleRegistry != nullptr && desc.debugName != nullptr
    && !m_passToggleRegistry->NoteDeclaredAndCheckEnabled(desc.debugName)) {
    continue; // Disabled - skipped entirely, exactly as if never declared.
}
```

**IMPORTANT — get the exact insertion point right.** Read the CURRENT,
real body of `DeclareOnePhase()` before editing (line numbers drift between
campaigns) — the check above must sit INSIDE the `for (RenderPassDesc& desc :
m_scratchCollected)` loop, AFTER the `translatedViewScope` is computed (order
does not matter functionally, but keep it visually grouped with the other
per-desc logic already there) and BEFORE the existing
`builder.AddRenderPass(desc.debugName, ...)` call — never after it (that would
declare the pass and THEN pretend to skip it, which does nothing).

### Step 3.4 — `Core.h`/`Core.cpp` wiring

`Core.h`: add a new private member, near `m_offscreenRenderPipeline`/
`m_presentRenderPipeline`:

```cpp
rg::RenderPassToggleRegistry m_renderPassToggleRegistry;
```

and a new PUBLIC accessor, near `GetGpuDrivenBatchDebugInfo()`/
`GetRenderFeatureCompositor()`:

```cpp
// editor-core-separation-8 campaign, PHASE1 - the ONE registry instance
// shared by BOTH m_offscreenRenderPipeline and m_presentRenderPipeline (see
// the constructor-time wiring, Core.cpp). Non-const, non-null (a plain owned
// member, never a pointer) - the "Render Graph" panel (PHASE4) and
// RenderGraphControlCommandBridge's pump (PHASE5) both mutate THROUGH this
// exact reference, on the main thread only (see
// RenderPassToggleRegistry.h's own header comment for why no mutex is
// needed).
rg::RenderPassToggleRegistry& GetRenderPassToggleRegistryMutable() noexcept
{
    return m_renderPassToggleRegistry;
}
```

`Core.cpp`: find the EXACT existing call site(s) of
`m_offscreenRenderPipeline.SetLegacyViewScopeTranslator(...)` and
`m_presentRenderPipeline.SetLegacyViewScopeTranslator(...)` (`search_in_dir`
for `SetLegacyViewScopeTranslator(` first, to find the real, current line
numbers) and add, immediately alongside each:

```cpp
m_offscreenRenderPipeline.SetPassToggleRegistry(&m_renderPassToggleRegistry);
```
```cpp
m_presentRenderPipeline.SetPassToggleRegistry(&m_renderPassToggleRegistry);
```

### Step 3.4b — the 2 confirmed exceptions: `"AtmosphereComposite"` and `"GpuSkinning"` bypass `DeclareOnePhase()` entirely

**This step exists because this phase's own planning caught a real error the
Investigation itself made and PHASE0_MASTER_STRATEGY.md's own Step 2.1 now
documents in full — READ IT before writing any code here.** Not every
built-in provider lambda in `Core.cpp` defers its pass through
`rg::RenderPassDesc`/`out.push_back(...)`; `"AtmosphereSharedLut"`,
`"AtmosphereViewLut"`, `"AtmosphereComposite"`, and `"Present"` each call
`frame.builder.AddRenderPass(...)` — or a helper function that does —
DIRECTLY, inside their own provider lambda, and push NOTHING into `out`.
Step 3.3's new check inside `RenderPipeline::DeclareOnePhase()` can never
see, and therefore can never disable, any of these four. Since
`"AtmosphereComposite"` is explicitly named in PHASE0_MASTER_STRATEGY.md's
own Step 1 goal as a pass this whole campaign must be able to turn on/off,
and `"GpuSkinning"` is too (as a single, whole-stage on/off switch — its own
individual per-mesh dispatches are ALREADY toggleable one-by-one through
Step 3.3's mechanism today, since `"GpuSkinning"`'s provider IS
deferred-style, just under dynamic per-dispatch names rather than the
literal string `"GpuSkinning"` — see PHASE0's own Step 2.1), this phase adds
exactly 2 new early-return guard lines, one each inside the
`"AtmosphereComposite"` and `"GpuSkinning"` provider lambda bodies in
`Core.cpp` (`Core::RegisterOffscreenRenderPipelineProviders()`) — the ONLY 2
of the ~10 existing `Register()` lambda bodies this whole campaign touches.

`"AtmosphereComposite"`'s lambda (`search_in_dir` for
`Register("AtmosphereComposite"` to find the real, current line — read the
WHOLE lambda body first): immediately after its existing
`if (viewData == nullptr || viewData->renderTexture == nullptr) { return; }`
guard, add:

```cpp
if (!m_renderPassToggleRegistry.NoteDeclaredAndCheckEnabled("AtmosphereComposite")) {
    return;
}
```

This runs once per active view (the provider is `ProviderScope::PerActiveView`),
exactly mirroring the shared-name-across-views consequence PHASE0's own Step
2.1 already documents for `"RenderOpaque"`/`"DrawSkyBackground"` — disabling
`"AtmosphereComposite"` disables it for BOTH Game View and Scene View at
once. When disabled, neither `frame.finalTextureOutputs.push_back(composited)`
nor the `frame.blackboard.Publish<rg::TextureHandle>(...)` call after it runs
this frame — confirmed safe: `RenderFeatureCompositor::ContributeRenderGraphPasses()`
already has an existing, pre-this-campaign `!resolved.has_value()` early
return for exactly this "no composited output published yet this frame"
case (e.g. the very first frame), so `"PluginRenderFeatures"` degrades
gracefully with zero new code needed there.

`"GpuSkinning"`'s lambda (`search_in_dir` for `Register("GpuSkinning"` to
find the real, current line — read the WHOLE lambda body first): immediately
inside the lambda, BEFORE the existing
`for (std::size_t i = 0; i < m_gpuSkinningRequestsThisFrame.size(); ++i)`
loop, add:

```cpp
if (!m_renderPassToggleRegistry.NoteDeclaredAndCheckEnabled("GpuSkinning")) {
    return;
}
```

This is a single, whole-stage on/off switch — when disabled, the entire
per-request loop below is skipped (no individual dispatch descs are ever
pushed to `out` this frame), and the trailing
`frame.blackboard.Publish<std::vector<rg::BufferHandle>>(kGpuSkinningOutputsKey, ...)`
call is skipped too. Confirmed safe: `"RenderOpaque"`'s own
`frame.blackboard.Fetch<std::vector<rg::BufferHandle>>(kGpuSkinningOutputsKey)`
already uses `.value_or(std::vector<rg::BufferHandle>{})` for exactly this
"nothing published this frame" case (e.g. a frame where nothing is
animating), so this degrades gracefully with zero new code needed there —
every currently-animating mesh simply keeps showing its last-skinned (frozen)
pose while disabled, never a crash.

**`"AtmosphereSharedLut"`/`"AtmosphereViewLut"`/`"Present"` deliberately do
NOT get this same treatment in this campaign** — `"Present"` is permanently
deny-listed anyway (`RenderPassToggleRegistry::IsDenyListed()`, Step 3.1/3.2
above already refuses to ever disable it, independent of whether it is ever
consulted at all), and `"AtmosphereSharedLut"`/`"AtmosphereViewLut"` are
never named in PHASE0_MASTER_STRATEGY.md's own Step 1 goal — adding a guard
for them is explicitly out of scope for this phase; do not add one
speculatively.

### Step 3.5 — Build system registration

- Root `CMakeLists.txt`: add
  `src/Renderer/RenderGraph/RenderPassToggleRegistry.h` and `.cpp` to the
  `gte_core` source list, immediately alongside the existing
  `RenderPipeline.h`/`.cpp` lines (`search_in_dir` for `RenderPipeline.cpp`
  in this file first, to find the exact insertion point).
- `tests/CMakeLists.txt`: add
  `Renderer/RenderGraph/RenderPassToggleRegistryTests.cpp` (new, Step 3.6
  below) alongside the existing `RenderPipelineTests.cpp` line.

### Step 3.6 — New test file: `tests/Renderer/RenderGraph/RenderPassToggleRegistryTests.cpp`

Cover, at minimum (mirror this repo's existing GoogleTest style — `read_file`
any sibling test in the same folder first for the exact include/macro
conventions):

- `NoteDeclaredAndCheckEnabled()` on a brand-new name returns `true` and the
  entry now appears in `ListAll()` with `everDeclaredThisSession == true`.
- `NoteDeclaredAndCheckEnabled()` called TWICE for the same name (simulating
  two frames) does not create a duplicate entry, and reflects a `SetEnabled()`
  change made in between.
- `SetEnabled("X", false)` then `NoteDeclaredAndCheckEnabled("X")` returns
  `false` (a disabled pass is genuinely skippable).
- `SetEnabled("Present", false)` returns `false` and does NOT change
  `IsEnabled("Present")` (still `true`) — the deny-list actually refuses.
- `SetEnabled()` on a name never yet declared creates an entry with
  `everDeclaredThisSession == false` in `ListAll()`.
- `IsEnabled()` on a completely unknown name returns `true` (the safe
  default).
- `ListAll()` returns entries sorted by name.

### Step 3.7 — New/extended test: proving `RenderPipeline` actually skips a disabled desc

Add ONE new `TEST(...)` inside the EXISTING
`tests/Renderer/RenderGraph/RenderPipelineTests.cpp` (do not create a second
file for this — it belongs with `RenderPipeline`'s own existing tests,
reusing whatever fake `RenderGraphBuilder`/provider-registration test
fixture that file already has): register a trivial provider under a known
`debugName`, call `SetPassToggleRegistry(&registry)` with the pass disabled
via `registry.SetEnabled("TestPass", false)` BEFORE calling `DeclareInto()`,
and assert the resulting builder never received that pass's
`AddRenderPass()` call (however this file's existing tests already observe
that — e.g. counting declared passes on a fake builder, or asserting on the
compiled graph's own pass list). A SECOND, symmetric test proves the
default (`SetPassToggleRegistry()` never called) behaves EXACTLY like
before this phase — every existing test in this file must keep passing
unmodified.

### Verification

1. Incremental build: `cmake --build build`.
2. Run the new `RenderPassToggleRegistryTests.cpp` cases AND the extended
   `RenderPipelineTests.cpp` cases (e.g.
   `ctest -C Debug -R "RenderPassToggleRegistry|RenderPipeline" --output-on-failure`
   from `build/`, or however this repo runs a filtered subset — `read_file`
   the previous campaign's own completion reports for the exact command
   style this repo prefers for a targeted run).
3. `git_status` — confirm the diff touches EXACTLY: the 2 new files
   (`RenderPassToggleRegistry.h/.cpp`), `RenderPipeline.h`, `Core.h`,
   `Core.cpp`, root `CMakeLists.txt`, `tests/CMakeLists.txt`, the new test
   file, and the extended `RenderPipelineTests.cpp`. Nothing else.

### What this phase does NOT do

- Does not add any UI or HTTP surface — `SetEnabled()` is only ever called
  from this phase's own tests. `NoteDeclaredAndCheckEnabled()` runs every
  frame in production from this phase onward, but since nothing ever calls
  `SetEnabled()` in production yet, every pass stays enabled — zero
  observable production behavior change.
- Does not touch 8 of the ~10 existing `Register(...)` lambda bodies in
  `Core.cpp` (Locked Product Decision #5) — touches exactly 2
  (`"AtmosphereComposite"`, `"GpuSkinning"`), each with one added
  early-return guard line (Step 3.4b), confirmed necessary since both bypass
  `DeclareOnePhase()`'s own flush loop entirely.
- Does not touch `RenderPassId`/`RenderPassDesc::id` at all.
- Does not run a full build or full `ctest` pass (Workflow Rule 1 — Phase 6
  only).

### Completion

Write `PHASE1_COMPLETION_REPORT.md` (confirm the exact final shape of every
new function, the exact `Core.cpp` line(s) the 2 new
`SetPassToggleRegistry()` calls landed at, the exact `Core.cpp` line(s) the 2
new Step 3.4b early-return guards landed at inside `"AtmosphereComposite"`/
`"GpuSkinning"`, and the new test file's own pass/fail summary), then
`git_add` + `git_commit`.
