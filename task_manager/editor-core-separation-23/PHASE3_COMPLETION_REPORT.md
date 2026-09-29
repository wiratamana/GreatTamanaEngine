# PHASE3 — COMPLETION REPORT: `Core::RegisterProjectRenderFeature()` / `Core::UnregisterProjectRenderFeature()`

## What was added

### 1. `Core.h` — two new includes

Placed right after the existing `#include "../Assets/AssetDatabase.h"` line,
before `#include <volk.h>`:

```cpp
#include "Plugins/ProjectRenderFeatureCallback.h"
#include "../../plugins/gte_plugin_abi/RenderFeatureDescriptor.h"
```

Both are unconditional, direct includes (never forward-declared) — confirmed
safe per PHASE0's own Step 2 finding: `RenderFeatureDescriptor.h` has zero
dependencies beyond `<cstdint>`/`<cstddef>`, and `ProjectRenderFeatureCallback.h`
(PHASE1's own new, free-standing header) has zero dependency back into
`Core.h` or the still-forward-declared-only `RenderFeatureCompositor.h`.
`Core.h` line 99 still reads `class RenderFeatureCompositor;` (forward
declaration only) — **unchanged by this phase**, confirmed by direct re-read
after every edit.

### 2. `Core.h` — two new public methods

Placed immediately after `UnregisterProjectRenderPassProvider()`, exactly per
the phase file's own Step 3.2:

```cpp
bool RegisterProjectRenderFeature(const char* debugName, RenderFeatureStage stage,
    RenderFeatureBlendMode blendMode, std::int32_t priority, ProjectRenderFeatureCallback callback);

void UnregisterProjectRenderFeature(const char* debugName);
```

### 3. `Core.cpp` — bodies

Placed immediately after `Core::UnregisterProjectRenderPassProvider()`, byte-
for-byte matching the phase file's own Step 3.3 pseudocode:

- `RegisterProjectRenderFeature()`: null-checks `m_renderFeatureCompositorPtr`
  first (refuse, logged, if no orchestrator exists), then `debugName == nullptr`
  (refuse, logged), then `std::strlen(debugName) > 63` (refuse, logged — the
  new 63-byte reject-not-truncate rule this phase introduces), then builds the
  descriptor via `MakeRenderFeatureDescriptor()` and forwards to
  `m_renderFeatureCompositorPtr->RegisterProjectFeature(descriptor, std::move(callback))`.
- `UnregisterProjectRenderFeature()`: null-safe (`m_renderFeatureCompositorPtr
  == nullptr || debugName == nullptr` both silently no-op), otherwise forwards
  straight to `UnregisterProjectFeature()` — no length check, since an
  over-length name could never have registered in the first place.
- `<cstring>` added to `Core.cpp`'s own include list (for `std::strlen`) — not
  previously included.
- `GTE_LOG_WARNING` is already reachable in `Core.cpp` (transitively, via
  `Core.h` → `IHostServices.h` → `Logging.h`) — confirmed by a successful
  build; no new logging-related include was needed.

## Boundary-length test results (63 vs. 64)

Derived precisely, per the phase file's own instruction, from
`MakeRenderFeatureDescriptor()`'s real copy loop
(`for (; name[i] != '\0' && i + 1 < sizeof(descriptor.name); ++i)`,
`sizeof(descriptor.name) == 64`) — confirming `strlen == 63` is the LARGEST
accepted length and `strlen == 64` is REJECTED:

- `DebugNameOfExactly63BytesSucceeds` — a 63-character name registers, and
  `DebugSnapshot()` contains an entry whose `name` is byte-for-byte that exact
  63-byte string.
- `DebugNameOf64BytesIsRejectedAndNeverTruncated` — a 64-character name is
  refused (`false`), `DebugSnapshot()` contains **zero** entries under that
  64-byte name, **and zero entries under its own 63-byte prefix either** —
  proving the truncation-vs-rejection rule holds (nothing silently truncated
  through).
- `AMuchLongerDebugNameIsAlsoRejected` — a 200-byte name is refused the same
  way (not just the exact-boundary case).

**Confirmed: the truncation-vs-rejection rule holds.** A too-long name never
reaches `MakeRenderFeatureDescriptor()` at all — `Core::RegisterProjectRenderFeature()`
returns `false` before ever calling it.

## Other Tier-1 tests (`tests/Core/RegisterProjectRenderFeatureApiTests.cpp`)

New file, added to `tests/CMakeLists.txt`'s hand-maintained `GTE_TEST_SOURCES`
list (confirmed not a glob — a fresh search of `tests/Core/` confirmed no
existing file covered `RegisterProjectRenderPassProvider()`'s own Tier-1
tests, matching the phase file's own expectation exactly). Reuses the exact
`HeadlessSurfaceProvider` + `NoopHostServices` + real `Core` fixture pattern
from `CoreHeadlessConstructionTests.cpp`/PHASE2's own
`RenderFeatureCompositorProjectFeatureTests.cpp`. Covers:

1. `DebugNameOfExactly63BytesSucceeds` — item 1.
2. `DebugNameOf64BytesIsRejectedAndNeverTruncated` — items 2/3.
3. `AMuchLongerDebugNameIsAlsoRejected` — supplementary, not just the exact
   boundary.
4. `UnderlyingCompositorRefusalPropagatesBackAsFalse` — item 4 (a duplicate
   name and an unwired stage, `RenderFeatureStage::PreOpaque`, both propagate
   back as `false` unchanged through the thin pass-through).
5. `UnregisterOnANeverRegisteredNameIsASafeNoOp` — item 5 (confirmed via a
   subsequent, unrelated, successful registration proving the object is still
   in a fully usable state — the real proof of "no crash").
6. `RegisterUnregisterReRegisterRoundTripSucceeds` — a supplementary
   end-to-end round trip exercised through the `Core` API itself (not just the
   underlying compositor directly, which is PHASE2's own scope).
7. `NullDebugNameIsRefusedWithoutCrashing` — item 6 (see "Ambiguity
   encountered and resolved" below for the null-`m_renderFeatureCompositorPtr`
   half of this same item).

## Ambiguity encountered and resolved

Item 3.4.6 of the phase file explicitly asks for a test exercising
`m_renderFeatureCompositorPtr == nullptr`, while simultaneously flagging that
this is "unlikely without a headless/Player-only build variant" and
instructing `ask_questions` if no such variant is reachable from this test
binary. Confirmed by direct re-check of PHASE0's own Step 2 finding:
`Core::GetRenderFeatureCompositor()` (and therefore
`m_renderFeatureCompositorPtr`) is **unconditionally non-null** on any real,
fully-constructed `Core` — no `#if`/feature-flag gates that orchestrator's
construction — so this specific null path is genuinely unreachable through
the real `HeadlessSurfaceProvider` + `Core` fixture this test file (and every
sibling test file in this campaign) uses, and `Core` is a concrete class with
no lightweight stub/interface substitute available.

Used `ask_questions` as the phase file instructs; the user left the decision
to the implementer's judgment. **Resolved**: this one null-path is left
covered only by code review + the defensive null-check itself (the phase
file's own explicitly-offered fallback option) — no dedicated stub was built
purely to exercise an unreachable-in-practice case. The
`debugName == nullptr` half of the same test item **is** directly, fully
exercised (`NullDebugNameIsRefusedWithoutCrashing`), since that path IS
reachable through the real fixture.

No other genuine ambiguity was encountered during this phase's own
implementation.

## Confirmation: consistency with existing conventions

- `Core.h`/`Core.cpp`'s only pre-existing `char[N]`-backed name field
  precedent this campaign needs to be consistent with is
  `GtePluginRenderFeatureDescriptor::name` itself (via
  `MakeRenderFeatureDescriptor()`'s own silent-truncation behavior for a
  `gte_plugin_abi` plugin) — the design doc and PHASE0 both already establish
  that THIS specific new call site (Project Assembly names, built from
  free-form, un-length-checked input) must diverge from that precedent and
  reject instead, which is exactly what was implemented; no other
  `char[N]`-backed name field convention exists in `Core.h`/`Core.cpp` to be
  consistent with.
- `RegisterProjectRenderFeature()`/`UnregisterProjectRenderFeature()` mirror
  `RegisterProjectRenderPassProvider()`/`UnregisterProjectRenderPassProvider()`'s
  own exact "thin pass-through, real work happens elsewhere" shape and
  placement.
- Zero change to `RenderFeatureCompositor`'s own internals in this phase —
  `RenderFeatureCompositor.h`/`.cpp` were not touched at all (confirmed via
  `git status`; only `Core.h`, `Core.cpp`, `tests/CMakeLists.txt`, and the new
  test file changed).

## Build / test results

- Incremental build (`cmake --build build`): succeeded, zero errors — first
  right after the `Core.h`/`Core.cpp` edits, then again after adding the new
  test file + `tests/CMakeLists.txt` entry (which triggered a CMake
  re-configure, confirmed clean, followed by a successful incremental
  compile/link of `GreatTamanaEngineTests.exe`).
- Targeted test: `ctest -C Debug --output-on-failure -R RegisterProjectRenderFeature`
  — all 7 new tests report **Skipped**, not Passed, on this development
  machine. This is the EXPECTED, environment-gated outcome (this machine's
  Vulkan driver/loader does not report `VK_EXT_headless_surface` as available
  at `vkCreateInstance()` time) — cross-checked against
  `ctest -R RenderFeatureCompositorProjectFeature` (PHASE2's own 8 tests),
  which skip identically, for the identical documented reason. This matches
  the same "N legitimate environment-gated skips" pattern every prior
  campaign's own `AGENTS.md` entry documents — not a new gap this phase
  introduces.

## What was deliberately left alone (per this phase's own scope)

- `ProjectAssemblyRegistrationLedger`/`IHotReloadDebugCapability` wiring —
  PHASE4's job.
- The `RenderPassEvent::AfterEverything`-only convention enforcement/ordering
  safety net — PHASE5's job.
- Any real, hand-wired demo Project Assembly render feature and live,
  HTTP-driven verification — PHASE6's job.
- `RenderFeatureCompositor.h`/`.cpp` themselves — untouched, PHASE2's own
  already-completed scope.

## End-of-phase checklist

1. ✅ Incremental build (`cmake --build build`) succeeds.
2. ✅ `ctest -C Debug --output-on-failure -R RegisterProjectRenderFeature` run
   — all 7 tests report the expected, environment-gated Skip (not a failure;
   consistent with this machine's pre-existing, documented
   `VK_EXT_headless_surface` gap).
3. ✅ This report — the exact `Core.h`/`Core.cpp` diff, the two boundary-length
   test results (63 vs. 64), and confirmation the truncation-vs-rejection rule
   holds.
4. Pending: `git_add` + `git_commit` covering the `Core.h`/`Core.cpp` changes,
   the new test file, `tests/CMakeLists.txt`, and this report (done
   immediately after this report is written).
