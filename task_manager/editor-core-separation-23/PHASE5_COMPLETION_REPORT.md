# PHASE5 — COMPLETION REPORT: Ordering safety net (`RenderPassEvent` consistency) and lifetime/thread-safety hardening

## What was added

### 1. Permanent Tier-1 regression test — `tests/Renderer/RenderGraph/RenderGraphCompilerTests.cpp`

`TEST(RenderGraphCompilerTest, ProjectRenderFeatureStyleAfterEverythingPassesNeverContradict)`,
inserted immediately after `SameEventTierNeverProducesAContradiction` and immediately
before `CallingCompileWithAConsistentGraphNeverAborts`, exactly per the phase
file's own placement instruction. Reproduces the EXACT shape a Project
Assembly's own hand-wired callback produces — a private-target WRITE tagged
`RenderPassEvent::AfterEverything`, followed by a READ of that same handle
(standing in for the compositor's own subsequent `DispatchBlend()` call) ALSO
tagged `RenderPassEvent::AfterEverything` — and asserts
`DetectRenderPassEventContradictions()` reports zero findings for it. Used the
phase file's own sketch verbatim; `MakeTextureDesc()`/`NoOpExecute` and the
`AddRenderPass(name, PassKind, setup, execute, RenderPassDrawKind, RenderPassEvent)`
call shape already matched this file's own existing helpers exactly — no
adaptation was needed.

### 2. Debug-build-only main-thread-affinity assertion — `RenderFeatureCompositor.h`/`.cpp`

Searched the whole `src/` tree first (`search_in_dir` for `this_thread::get_id`,
`std::thread::id`, `MainThread`, and every `assert(` call site) — confirmed
**no existing main-thread-affinity helper/convention exists anywhere in this
codebase**; every "main thread" reference found is a doc-comment claim, never
an enforced runtime check. Per the phase file's own explicit ambiguity
checkpoint (Step 3.5), used `ask_questions` before proceeding — the user left
the decision to the implementer's judgment. **Resolved**: added the smallest
possible mechanism, scoped to this one class only (the phase file's own
default-suggested path), rather than inventing a new engine-wide utility that
nothing else currently needs.

- `RenderFeatureCompositor.h` gained `#include <thread>`, a new private member
  `const std::thread::id m_mainThreadId = std::this_thread::get_id();`
  (captured once, at construction time — this object is always constructed by
  `Core::RegisterBuiltinCapabilityOrchestrators()`, called from `Core`'s own
  constructor, which itself always runs on the engine's own main thread), and
  a new private method declaration, `void AssertCalledFromMainThread() const;`,
  placed immediately after `FindEntryByName()`.
- `RenderFeatureCompositor.cpp` gained the method body (immediately after
  `FindEntryByName()`'s own body):
  ```cpp
  void RenderFeatureCompositor::AssertCalledFromMainThread() const
  {
      assert(std::this_thread::get_id() == m_mainThreadId
          && "RenderFeatureCompositor::RegisterProjectFeature()/UnregisterProjectFeature() are documented "
             "main-thread-only contracts (exactly like ContributeRenderGraphPasses() itself) - this call came from a "
             "different thread.");
  }
  ```
  and a single `AssertCalledFromMainThread();` call was added at the very top
  of BOTH `RegisterProjectFeature()` and `UnregisterProjectFeature()` (before
  any other logic in either method), exactly per the phase file's own Step
  3.2. `<cassert>` was already included in this file — no new include needed
  for the macro itself.
- This is **debug-build only by construction**, with no extra `#ifndef NDEBUG`
  needed: `assert()` itself already compiles to a true no-op the instant
  `NDEBUG` is defined (a release build), matching every other `assert()` call
  site already in this codebase (none of them are separately wrapped in an
  explicit `#ifndef NDEBUG` either — confirmed by the same `search_in_dir`
  sweep above, e.g. `RenderGraphBarrierPlanner.cpp`, `RenderGraphBuilder.cpp`,
  `Pipeline.cpp`).
- `ContributeRenderGraphPasses()` itself was **not** modified — the phase file
  only asked for the check on the two new mutators (`RegisterProjectFeature()`/
  `UnregisterProjectFeature()`), since `m_mainThreadId` is captured once, at
  construction, and both mutators compare against that same fixed reference
  point; no separate assertion inside `ContributeRenderGraphPasses()` is
  needed to make that comparison meaningful.

### 3. Manual proof the assertion mechanism itself genuinely works

Per the phase file's own Step 3.2 instruction ("confirm via a quick,
deliberate, throwaway local test... then REMOVE that throwaway test before
committing"): this development machine cannot construct a real, headless
`gte::Core`/`RenderFeatureCompositor` at all (the same pre-existing
`VK_EXT_headless_surface`-unavailable gap PHASE2/PHASE3/PHASE4 already hit,
confirmed again by this phase's own targeted `ctest` run below — all 8
PHASE2 tests reporting `Skipped`, not `Passed`), so the two mutators
themselves cannot be directly exercised from a spawned thread on this
machine. Instead, a standalone, throwaway scratch program
(`scratch_phase5_thread_assert_check.cpp`, written outside `src/`/`tests/`,
compiled directly via `g++`, run, then fully deleted before this report was
written — confirmed gone via `git status`, which shows only the three real,
intended source files as modified) reproduced the EXACT mechanism this
class's own new code uses (a `std::thread::id` captured once, compared via
`assert()` from a later call):

- Called from the SAME thread that captured the reference id: the process
  printed `"OK: no abort, as expected."` and exited cleanly (exit code 0) —
  the assertion did not fire.
- Called from a SPAWNED `std::thread`: the process printed nothing further
  and stderr showed
  `Assertion failed: std::this_thread::get_id() == g_capturedId && "called from a different thread than the captured one", file scratch_phase5_thread_assert_check.cpp, line 16`
  — the assertion fired immediately, aborting before either of the two
  "reached here"/"OK" lines could print.

This proves the underlying mechanism (thread-id capture + `assert()`
comparison) genuinely detects a cross-thread call and aborts loudly, which is
exactly what `AssertCalledFromMainThread()` does inside the real class — the
only difference being which object/member holds the captured id.

## Live-lifetime confirmation (Step 3.3), re-derived fresh from the current source

Re-read `RenderFeatureCompositor::EnsurePrivateTargetState()` and the
`EnsureTextureSized()` helper it calls, live, in the CURRENT, PHASE1-4-modified
source (`RenderFeatureCompositor.cpp`, confirmed at today's actual line
numbers, which have shifted since PHASE0's own citation due to this
campaign's own accumulated edits):

```cpp
// RenderFeatureCompositor.cpp, line 531 (EnsureTextureSized):
void RenderFeatureCompositor::EnsureTextureSized(
    std::optional<RenderTexture>& texture, const char* internedName, VkExtent2D extent)
{
    ...
    if (!texture.has_value()) {
        texture.emplace(m_renderer.CreateRenderTexture(width, height, VK_FORMAT_R8G8B8A8_UNORM, internedName,
            /*depthDebugName=*/nullptr, /*allowStorageImageAccess=*/true));
        return;
    }
    ...
}

// RenderFeatureCompositor.cpp, line 553 (EnsurePrivateTargetState):
RenderFeatureCompositor::PrivateTargetState& RenderFeatureCompositor::EnsurePrivateTargetState(
    const char* internedName, VkExtent2D extent)
{
    ...
    PrivateTargetState state;
    state.opsDescriptorSet =
        ComputeDescriptorSet(m_renderer.AllocateComputeDescriptorSet(m_operationRegistry.OpsDescriptorSetLayout()));
    EnsureTextureSized(state.texture, internedName, extent);
    const auto inserted = m_privateTargetStates.emplace(internedName, std::move(state));
    return inserted.first->second;
}
```

**Confirmed explicitly**: the private `RenderTexture` (`PrivateTargetState::texture`)
is created via `m_renderer.CreateRenderTexture(...)` — `m_renderer` is a plain
`Renderer&` reference to the ONE engine-owned `Renderer` object, and
`RenderTexture`/`ComputeDescriptorSet` are both ordinary, engine-owned GPU
resource wrapper types compiled entirely into `gte_core.a` — **zero code or
vtable from this construction path is ever compiled into any Project
Assembly `.dll`'s own image.** This means the private texture's own lifetime
(and its paired `ComputeDescriptorSet`, `PrivateTargetState::opsDescriptorSet`,
same reasoning) survives a hot-reload cycle completely unaffected — it is not
touched by `ProjectAssemblyRegistrationLedger::UnregisterEverythingFor()`'s
new render-feature teardown loop (PHASE4) at all, by design (see PHASE4's own
completion report: "Deliberately do NOT touch `m_privateTargetStates`/
`m_blendStageStates` entries for this slot's own interned names").

**Only the `projectCallback` `std::function` itself is dangerous across a
hot-reload unload** — its captured state (whatever local variables the
Project Assembly's own lambda closed over, and, critically, the function
pointer to the lambda's own `operator()` body) lives inside that Project
Assembly's own `.dll` image and becomes a dangling call the instant
`FreeLibrary()` runs on it. **PHASE4's teardown wiring already removes it
before that happens**: `ProjectAssemblyRegistrationLedger::UnregisterEverythingFor()`'s
new `renderFeatureNames` loop (confirmed, PHASE4's own completion report,
placed strictly BEFORE the `renderPassNames` loop) calls
`core.UnregisterProjectRenderFeature(name)` for every render feature this
project ever registered, which forwards straight into
`RenderFeatureCompositor::UnregisterProjectFeature()` — this method's own body
(re-confirmed above, now carrying this phase's own `AssertCalledFromMainThread()`
call at its top) erases the owning `Entry` (including its `projectCallback`
member) from `m_postComposite`/`m_preUi` entirely, and `ProjectAssemblyHost`'s
own documented, non-negotiable teardown order
(`renderer.WaitForGpuIdle()` → ledger teardown → `FreeLibrary()`) guarantees
this erase always completes, on the GPU-idle main thread, strictly before the
owning `.dll`'s image is ever unmapped. This is not a "probably fine, PHASE0
already said so" assumption — it is re-derived here, fresh, from the current,
real, committed source, exactly as this phase's own file requires.

## Ambiguity encountered and resolved

The phase file's own Step 3.2/3.5 explicitly anticipated that no reusable
main-thread-affinity helper might exist in this codebase, and instructed
`ask_questions` in that case. Confirmed via a fresh, whole-`src/` sweep that
none exists. Used `ask_questions`; the user left the decision to the
implementer's judgment. Resolved by adding the smallest possible mechanism
scoped to `RenderFeatureCompositor` alone (option 1 of the three offered),
since nothing else in this engine currently needs this check duplicated in a
third place — see "What was added" section 2 above for the exact
implementation. No other genuine ambiguity was encountered during this
phase's own implementation; `RenderGraphCompilerTests.cpp`'s existing helper
functions (`MakeTextureDesc()`, `NoOpExecute`) and `AddRenderPass()`'s own
call shape matched the phase file's own sketch exactly, with no adaptation
needed.

## Build / test results

- Incremental build (`cmake --build build`, working directory
  `C:\Users\F5954\Documents\TAMANA\GreatTamanaEngine`): succeeded, zero
  errors — run once right after the new regression test was added (compiling
  only `RenderGraphCompilerTests.cpp`), and once more after the
  `RenderFeatureCompositor.h`/`.cpp` assertion changes (a full incremental
  rebuild touching `gte_core`, `gte_editor`, `GreatTamanaEditor.exe`, the test
  binary, and both Project Assembly `.dll`s — all relinked cleanly with zero
  errors).
- Targeted test: `ctest -C Debug --output-on-failure -R RenderGraphCompilerTest`
  (run from the `build` directory) — **34/34 tests report Passed** (100%),
  including the new `ProjectRenderFeatureStyleAfterEverythingPassesNeverContradict`
  test (test #998) — zero regression to every pre-existing test in this same
  file, including `SameEventTierNeverProducesAContradiction` and every
  `Compile()`-level test after it.
- Cross-check: `ctest -C Debug --output-on-failure -R RenderFeatureCompositorProjectFeature`
  (PHASE2's own 8 tests, re-run after this phase's own `.h`/`.cpp` changes to
  confirm zero regression) — **all 8 report the same, expected, pre-existing
  environment-gated Skip** (not a failure; this development machine's Vulkan
  driver/loader does not report `VK_EXT_headless_surface` available at
  `vkCreateInstance()` time, the same documented gap every prior phase in this
  campaign already hit) — `ctest`'s own summary line confirms
  `100% tests passed out of 8`.
- Manual, throwaway proof of the assert mechanism itself (see "What was added"
  section 3 above): confirmed working as expected, then fully deleted — `git status`
  after cleanup shows only the three real, intended files modified
  (`src/Core/Plugins/RenderFeatureCompositor.cpp`,
  `src/Core/Plugins/RenderFeatureCompositor.h`,
  `tests/Renderer/RenderGraph/RenderGraphCompilerTests.cpp`), no stray scratch
  file left behind.

## What this phase deliberately deferred to PHASE6 (per its own Step 3.4)

- The LIVE, HTTP-driven proof that `RegisterProjectFeature()`/
  `UnregisterProjectFeature()` are never actually called concurrently with
  `ContributeRenderGraphPasses()` across a REAL hot-reload cycle.
- The live proof that a slot reused across a register → unregister → register
  (different name) sequence shows correct visual output from its very first
  frame, with no stale prior-occupant content ever visible.

Both are explicitly PHASE6's job — this phase only built the STATIC safety
nets (the regression test in section 1, the assertion in section 2) those
live proofs will run against, exactly as the phase file's own Step 3.4
requires.

## What was deliberately left alone (per this phase's own scope)

- `RenderFeatureCompositor`'s existing `_v2`/`_v3` plugin behavior — untouched;
  this phase's only behavioral change is the new `AssertCalledFromMainThread()`
  call at the top of two methods that did not exist before PHASE2, plus a pure
  compile-time-additive test file.
- `Core.h`/`Core.cpp`, `ProjectAssemblyRegistrationLedger.h`/`.cpp`,
  `EditorCapabilities.h`, `EditorHotReloadDebugCapability.cpp`,
  `NetworkRoutes.cpp`/`.h` — untouched (PHASE3/PHASE4's own already-completed
  scope).
- Any real, hand-wired demo Project Assembly render feature and the live,
  real hot-reload cycle's dynamic HTTP-driven verification — PHASE6's job.

## End-of-phase checklist

1. ✅ Incremental build (`cmake --build build`) succeeds.
2. ✅ `ctest -C Debug --output-on-failure -R RenderGraphCompilerTest` passes
   (34/34 — the new test alongside every pre-existing one in that same file,
   confirming zero regression to the existing detector).
3. ✅ This report — the new regression test's result, the thread-affinity
   assertion's exact mechanism and where it lives, its manual proof-of-mechanism
   result, and the explicit, freshly-re-derived lifetime confirmation.
4. Pending: `git_add` + `git_commit` covering the test file change, the
   `RenderFeatureCompositor.h`/`.cpp` assertion change, and this report (done
   immediately after this report is written).
