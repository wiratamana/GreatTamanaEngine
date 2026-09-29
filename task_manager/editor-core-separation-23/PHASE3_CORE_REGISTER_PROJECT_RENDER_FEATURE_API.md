# PHASE3 — `Core::RegisterProjectRenderFeature()` / `Core::UnregisterProjectRenderFeature()`

Parent: `PHASE0_MASTER_STRATEGY.md` (read it fully first).
Campaign folder: `task_manager/editor-core-separation-23/`
Design doc citation: `DESIGN_REQUIREMENTS_ONSCREEN_COMPOSITING_BIGSTEP_01_CORE_ENGINE_WIRING_2026-09-29.txt`,
Step 4 (re-read it in full before starting).
Previous phase report to read first: `PHASE2_COMPLETION_REPORT.md`.

## Step 1: The Goal (Where are we going?)

A Project Assembly's own `RegisterProject(gte::Core& core)` entry point must
be able to call one new, plain, thin-pass-through public method on `Core`
itself — `Core::RegisterProjectRenderFeature()` — mirroring
`RegisterProjectRenderPassProvider()`'s own existing shape and placement
exactly, reaching PHASE2's real `RenderFeatureCompositor::RegisterProjectFeature()`.
This is the FIRST call site anywhere in this engine that builds a
`GtePluginRenderFeatureDescriptor::name` (a fixed `char[64]`) by concatenating
free-form, un-length-checked project/feature name text — so this phase must
also introduce the one new safety rule that call site needs: reject (loudly,
non-fatally), never silently truncate, any name that would not fit.

## Step 2: The Situation (Where are we now?)

Re-confirm fresh:

- `src/Core/Core.h` lines 357/363:
  `void RegisterProjectRenderPassProvider(const char* debugName, rg::ProviderScope scope, rg::RenderPassProvider provider);`
  and `void UnregisterProjectRenderPassProvider(const char* debugName);` — the
  new methods this phase adds sit immediately after these two, same file,
  same "thin pass-through, real work happens elsewhere" shape.
- `Core::GetRenderFeatureCompositor() const noexcept` (line 423) returns
  `m_renderFeatureCompositorPtr` — non-null on any real, fully-constructed
  `Core` (confirmed, PHASE0 Step 2) — but this new method must still
  null-check it defensively (mirrors that accessor's own doc comment: "a
  build with the relevant orchestrator gated off entirely must not crash a
  Project Assembly that calls this").
- `plugins/gte_plugin_abi/RenderFeatureDescriptor.h`'s
  `MakeRenderFeatureDescriptor(const char* name, RenderFeatureStage stage, std::int32_t priority, RenderFeatureBlendMode blendMode)`
  (confirmed unchanged, PHASE0 Step 2) copies into a fixed `char[64]`
  (`descriptor.name`, 63 usable bytes + null terminator), SILENTLY truncating
  anything longer — this phase's new `Core` method must never let a
  too-long name reach this helper undetected; it length-checks first and
  refuses outright.
- `Core.h` today includes neither `plugins/gte_plugin_abi/RenderFeatureDescriptor.h`
  nor PHASE1's new `Plugins/ProjectRenderFeatureCallback.h` — both must be
  added.

## Step 3: The Plan (detailed strategy)

### 3.1 — New includes in `Core.h`

Near `Core.h`'s existing `#include "Plugins/PluginRenderOperationRegistry.h"`/
`#include "Plugins/PluginHost.h"` sibling includes, add:

```cpp
#include "Plugins/ProjectRenderFeatureCallback.h"
#include "../../plugins/gte_plugin_abi/RenderFeatureDescriptor.h"
```

Both are safe to include directly, unconditionally: `RenderFeatureDescriptor.h`
has zero dependencies beyond `<cstdint>`/`<cstddef>` (confirmed, PHASE0 Step
2); `ProjectRenderFeatureCallback.h` is PHASE1's own new, free-standing
header, with zero dependency back into `Core.h` or the still-forward-
declared-only `RenderFeatureCompositor.h`.

### 3.2 — New public methods, `Core.h`

Placed immediately after `UnregisterProjectRenderPassProvider()`:

```cpp
// Forwards straight to m_renderFeatureCompositorPtr's own
// RegisterProjectFeature()/UnregisterProjectFeature() - NOT
// m_offscreenRenderPipeline (the target RegisterProjectRenderPassProvider()
// forwards to) - RenderFeatureCompositor is a separate object, owned by one
// of Core's m_capabilityOrchestrators entries. Resolves the real, live
// pointer the SAME way GetRenderFeatureCompositor() already does, never
// constructs a second instance.
bool RegisterProjectRenderFeature(const char* debugName, RenderFeatureStage stage,
    RenderFeatureBlendMode blendMode, std::int32_t priority,
    ProjectRenderFeatureCallback callback);

void UnregisterProjectRenderFeature(const char* debugName);
```

### 3.3 — `Core.cpp` bodies

`RegisterProjectRenderFeature()`, in order:

```cpp
bool Core::RegisterProjectRenderFeature(const char* debugName, RenderFeatureStage stage,
    RenderFeatureBlendMode blendMode, std::int32_t priority, ProjectRenderFeatureCallback callback)
{
    if (m_renderFeatureCompositorPtr == nullptr) {
        GTE_LOG_WARNING("Core", "RegisterProjectRenderFeature('" + std::string(debugName != nullptr ? debugName : "<null>")
            + "') failed - no RenderFeatureCompositor orchestrator is registered in this build.");
        return false;
    }
    if (debugName == nullptr) {
        GTE_LOG_WARNING("Core", "RegisterProjectRenderFeature() failed - debugName is null.");
        return false;
    }
    // GtePluginRenderFeatureDescriptor::name is a fixed char[64] (63 usable
    // bytes + null terminator). This is the FIRST call site in this engine
    // that builds this string from free-form, un-length-checked input -
    // silently truncating here would defeat this whole system's own
    // "collision-checked by name" promise (two long names sharing the same
    // first 63 bytes would be reported identical with no diagnostic). Reject
    // outright instead - never truncate-and-proceed.
    if (std::strlen(debugName) > 63) {
        GTE_LOG_WARNING("Core", "RegisterProjectRenderFeature('" + std::string(debugName)
            + "') failed - name exceeds the 63-byte limit for GtePluginRenderFeatureDescriptor::name; "
            "shorten it (never silently truncated).");
        return false;
    }

    const GtePluginRenderFeatureDescriptor descriptor =
        MakeRenderFeatureDescriptor(debugName, stage, priority, blendMode);
    return m_renderFeatureCompositorPtr->RegisterProjectFeature(descriptor, std::move(callback));
}
```

`UnregisterProjectRenderFeature()` mirrors this, null-safe, forwarding
straight to `UnregisterProjectFeature()` — no length check needed (an
over-length name could never have been successfully registered in the first
place, so `FindEntryByName()` simply reports "not found," the same harmless
outcome as any other unknown name):

```cpp
void Core::UnregisterProjectRenderFeature(const char* debugName)
{
    if (m_renderFeatureCompositorPtr == nullptr || debugName == nullptr) {
        return;
    }
    m_renderFeatureCompositorPtr->UnregisterProjectFeature(debugName);
}
```

Confirm `<cstring>` (for `std::strlen`) is already included by `Core.cpp`, or
add it.

### 3.4 — Tier-1 tests

New file: `tests/Core/RegisterProjectRenderFeatureApiTests.cpp` (confirmed, via
a fresh search of `tests/Core/` at the time this phase file was written, that
NO existing test file covers `RegisterProjectRenderPassProvider()`'s own
Tier-1 tests today — re-confirm this yourself before assuming it, since this
is a fast-moving codebase, but expect to create a genuinely new file rather
than extend an existing one). Whichever path you take, the file must be added
to `tests/CMakeLists.txt`'s own test-file list — that list is hand-maintained,
NOT a glob (every sibling `tests/Core/*.cpp` file, e.g.
`Core/CoreHeadlessConstructionTests.cpp`, is individually listed there with
its own short comment) — add one new line for this file, mirroring that
comment style. Skipping this step means the new tests silently never compile
or run at all; the targeted `ctest -C Debug --output-on-failure -R
RegisterProjectRenderFeature` this phase's own End-of-phase step requires
would then report "No tests were found" rather than a real pass — do not
mistake that for success. Reuse
the `HeadlessSurfaceProvider` + real-`Core` fixture (PHASE0 Step 2). Cover:
  1. A `debugName` of exactly 63 bytes succeeds (confirm via
     `RenderFeatureCompositor::DebugSnapshot()` containing an entry whose
     `name` is exactly that 63-byte string, byte-for-byte).
  2. A `debugName` of 64 bytes succeeds too? NO — confirm the design doc's
     own boundary precisely: `std::strlen(debugName) > 63` is the rejection
     condition, meaning exactly 63 characters (`strlen == 63`) is the LARGEST
     accepted length (leaving room for the null terminator inside the
     64-byte buffer) — a 64-CHARACTER name (`strlen == 64`) must be REJECTED.
     Write both boundary cases explicitly (63 succeeds, 64 fails) — do not
     guess the off-by-one; derive it from `MakeRenderFeatureDescriptor()`'s
     own real copy loop (`for (; name[i] != '\0' && i + 1 < sizeof(descriptor.name); ++i)`,
     `sizeof(descriptor.name) == 64`) before writing these two tests.
  3. The 64-byte-or-longer case is rejected (`false`, logged) and produces
     NO entry at all in `DebugSnapshot()` — explicitly NOT a truncated one
     (register a 64-byte name, then confirm `DebugSnapshot()` contains zero
     entries whose name is a 63-byte PREFIX of it either — proving nothing
     silently truncated through).
  4. A duplicate/unwired-stage/slot-exhaustion failure from
     `RenderFeatureCompositor::RegisterProjectFeature()` propagates back as
     `false` from `Core::RegisterProjectRenderFeature()` unchanged (a thin
     pass-through, confirmed).
  5. `UnregisterProjectRenderFeature()` on a name that was never registered
     is a safe no-op (no crash), confirmed by calling it before anything else
     in a fresh test case.
  6. Null-safety: if a way exists in this test binary to exercise
     `m_renderFeatureCompositorPtr == nullptr` (unlikely without a headless/
     Player-only build variant — if no such variant is reachable from this
     test binary, `ask_questions` about whether this specific null-path
     needs a different kind of test, e.g. a smaller unit test around a
     hand-constructed `Core`-shaped stub, or whether it is acceptable to
     leave this one path covered only by code review + the defensive
     null-check itself).

### 3.5 — Ambiguity checkpoints

  - If `Core.h`/`Core.cpp` already has an existing, different length-limit
    convention for some OTHER `char[N]`-backed name field this campaign
    should be consistent with, `ask_questions` before diverging.
  - If test item 3.4.6 above cannot be exercised at all in this build,
    `ask_questions` rather than skipping it silently — record the answer in
    the completion report either way.

### 3.6 — End of phase

1. Incremental build (`cmake --build build`) succeeds.
2. `ctest -C Debug --output-on-failure -R RegisterProjectRenderFeature`
   passes (targeted filter).
3. Write `PHASE3_COMPLETION_REPORT.md`: the exact `Core.h`/`Core.cpp` diff,
   the two boundary-length test results (63 vs. 64), and confirmation the
   truncation-vs-rejection rule holds.
4. `git_add` + `git_commit` covering the `Core.h`/`Core.cpp` changes, the new
   test file, and the report.
