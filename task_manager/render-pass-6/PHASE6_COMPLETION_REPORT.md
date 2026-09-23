# PHASE6 — Completion Report: Generic `ResourceKind` Dispatch Table (item 2.2)

## Parent

`PHASE0_MASTER_STRATEGY.md` / `PHASE6_RESOURCEKIND_DISPATCH_TABLE.md`.
`PHASE5_COMPLETION_REPORT.md` was read first, per that document's own
instructions, along with a fresh re-read of
`PHASE5_RESOURCE_SLOT_VECTOR_COLLAPSE.md` itself (its own dedicated
double-check had already landed and found nothing worth changing — the
`TextureSlot`/`BufferSlot`/`VolumeTextureSlot` shape it locks matched the
real, current source exactly). `PHASE4_COMPLETION_REPORT.md` was also read,
since this phase's own strategy document explicitly depends on it (the
`firstTextureWriter`/`firstBufferWriter`/`firstVolumeTextureWriter` prescan
and inline fast-path check PHASE4 introduced are two *additional* physical
`switch (usage.kind)` sites this phase had to find and convert, beyond the
seven the strategy document's own text was drafted against).

## What was done

Implemented `PHASE6_RESOURCEKIND_DISPATCH_TABLE.md` exactly as specified. No
ambiguity was hit that the strategy document itself failed to resolve during
implementation — but the *mandatory Step 3.3 scratch-enumerator
verification* surfaced a genuine, pre-existing gap in the codebase's build
configuration (see its own dedicated section below), which **was** a real
ambiguity not pinned down by any document, and was resolved via
`ask_questions` before this phase was considered done.

### 1. `RenderGraphTypes.h` — the generic `DispatchByKind()` dispatcher

Added exactly as specified in the strategy document's Step 3.1: a function
template taking a `const ResourceUsage&` plus three distinct callables
(`onTexture`/`onBuffer`/`onVolumeTexture`), dispatching via a real, exhaustive
`switch (usage.kind)` with **no `default:` case**, and a hard-fail
(`assert(false)` + `throw std::logic_error`) unreachable tail rather than a
fabricated sentinel or a re-invoked callable. `#include <cassert>` and
`#include <stdexcept>` were added to this header's existing include list, as
the strategy document specified. Placed immediately after `ResourceUsage`'s
own definition, alongside `ResourceKind`/`ResourceUsage` themselves.

### 2. Fresh site recount (Step 2's own "recount before you start" — performed for real)

A fresh grep of the actually-landed (post-PHASE4/PHASE5) source found **nine**
hand-rolled `switch (usage.kind)` / `switch (a.kind)` sites, not seven — the
strategy document's own "very likely nine" prediction was confirmed exactly.
The two beyond the original seven are both PHASE4 additions inside
`RenderGraphCompiler::Compile()`:

1. `RenderGraph.cpp`, `ApplyUsageBarrierIfNeeded()` (original Site 1).
2. `RenderGraphCompiler.cpp`, `DetectRenderPassEventContradictions()`'s
   internal `sameResource` lambda (original Site 2).
3. `RenderGraphCompiler.cpp`, the `firstTextureWriter`/`firstBufferWriter`/
   `firstVolumeTextureWriter` prescan loop (**new, PHASE4 addition**).
4. `RenderGraphCompiler.cpp`, `Compile()`'s RAW-edge scan (original Site 3).
5. `RenderGraphCompiler.cpp`, the inline fast-path `laterWriter` contradiction
   check (**new, PHASE4 addition**).
6. `RenderGraphCompiler.cpp`, `Compile()`'s WAW-edge scan (original Site 4).
7. `RenderGraphCompiler.cpp`, the root-marking scan (original Site 5).
8. `RenderGraphCompiler.cpp`, the Step 4 lifetime `touch()` lambda (original
   Site 6).
9. `RenderGraphSnapshot.cpp`, `ResourceUsageName()` (original Site 7).

All nine were converted. A final full-`src/`-tree grep for
`switch (usage.kind)`/`switch (a.kind)` confirms **zero** remaining hits
anywhere outside `DispatchByKind()`'s own single switch statement (the one
real, intentional switch this whole phase exists to centralize behind) —
every other surviving hit is an explanatory comment describing the
conversion, not a live switch construct.

### 3. Site-by-site conversion detail

- **Site 2 (`sameResource`, done first as the strategy's own "low-risk
  warm-up")**: converted to a single `DispatchByKind()` call returning
  `bool`, with the exact three equality comparisons moved verbatim into their
  own lambdas. Per this phase's own resolved "PHASE4/PHASE6 interaction"
  note, `DetectRenderPassEventContradictions()`'s **public signature and
  observable return value** are unaffected — only this one internal lambda's
  dispatch mechanism changed.
- **`firstXWriter` prescan (new site)**: converted to a shared
  `firstWriterSlotFor()` helper (a `DispatchByKind()` call returning
  `std::int32_t*`, resolving both "which vector" and "which index" in one
  step), mirroring the strategy document's own `lastWriterSlotFor()` recipe.
  The prescan loop itself now reads `if (slot != nullptr && *slot == -1) { *slot
  = i; }` instead of the original three-way switch, preserving the exact
  "first writer only" semantics.
- **RAW-edge scan + inline fast-path `laterWriter` check**: converted to
  `lastWriterSlotFor()` (for the RAW writer resolution) and the already-built
  `firstWriterSlotFor()` (for the fast-path check) respectively — both
  concrete recipes taken directly from the strategy document's Step 3.2. The
  self-exclusion guard (`laterWriter != -1 && laterWriter != i`) is
  byte-for-byte unchanged.
- **WAW-edge scan**: converted to the same `lastWriterSlotFor()` helper —
  `addEdge(*slot, i); *slot = i;` reproduces the original
  "addEdge-then-write-back" order exactly, only touching the slot when it
  genuinely exists (identical to the original bounds-checked guard).
- **Root-marking scan**: converted to a `DispatchByKind()` call returning
  `bool` (`ContainsTextureHandle(...)` / `false` / `ContainsVolumeTextureHandle(...)`),
  each lambda body moved verbatim.
- **Lifetime `touch()` lambda**: converted to the strategy document's own
  concrete recipe — a single `DispatchByKind()` call resolving a
  `ResourceLifetime*` directly (both "which vector" and "which index" in one
  step), strictly simpler than the original two-local-variable
  (`lifetimes`/`index`) shape it replaces. Confirmed producing identical
  `ResourceLifetime` results against every existing
  `RenderGraphCompilerTest` lifetime assertion (all pass unmodified).
- **Site 1 (`ApplyUsageBarrierIfNeeded`)**: converted to a `void`-returning
  `DispatchByKind()` call — the three branches genuinely cannot be unified
  into one same-return-type dispatch (Texture legitimately does more work
  than Buffer/VolumeTexture), exactly as the strategy document anticipated.
  Every existing computed value/comment inside each case body was preserved
  verbatim, using the lambda's own handle parameter (`textureHandle`/
  `bufferHandle`/`volumeHandle`) instead of `usage.texture`/`usage.buffer`/
  `usage.volumeTexture`.
- **Site 7 (`ResourceUsageName()`)**: converted to a `void`-returning
  `DispatchByKind()` call, each lambda body (a bounds-checked `name =` copy)
  moved verbatim; the surrounding doc comment (which used to describe the old
  hand-rolled switch) was reworded to describe the new dispatcher instead,
  without deleting the historical context explaining *why* this function
  exists (the frame-debugger-5 campaign's own "this one file was missed by
  the original audit" finding).

### 4. Tests (`tests/Renderer/RenderGraph/RenderGraphTypesTests.cpp`)

Added a new `RenderGraphDispatchByKindTest` suite (four cases, mirroring the
file's existing `RenderGraphResourceUsageTest` style exactly):

- `TextureUsageDispatchesOnlyToOnTextureWithTheCorrectHandle`
- `BufferUsageDispatchesOnlyToOnBufferWithTheCorrectHandle`
- `VolumeTextureUsageDispatchesOnlyToOnVolumeTextureWithTheCorrectHandle`
- `DispatchByKindWithNonVoidReturnTypeProducesTheExpectedValue`

Each of the first three confirms exactly ONE of the three callables fires,
with the correct handle value, for its matching `ResourceUsage::For*()`
factory; the fourth confirms a non-`void` return type round-trips correctly
through all three factories. All four pass.

`RenderGraphCompilerTests.cpp` (33 pre-existing cases + 1 death test) and
`RenderGraphSnapshotTests.cpp` (24 cases) both show **zero required
changes** and **zero regressions** — every mechanical conversion preserved
the exact logic each dispatch arm contains.

## Mandatory Step 3.3 verification — performed for real, genuine finding, resolved via `ask_questions`

Per the task's explicit instruction, the scratch "add a 4th `ResourceKind`
enumerator, confirm `DispatchByKind()` fails to compile, then revert"
verification was performed personally, not merely claimed:

1. Added `_ScratchFourthKindForVerificationOnly` as a 4th enumerator to
   `ResourceKind` (`RenderGraphTypes.h`).
2. Ran `cmake --build build --target gte_core`.
3. **Result: the build succeeded with ZERO errors and ZERO warnings** — not
   the "MORE than one compile error" the strategy document's Step 3.3
   expected.

**Root cause, confirmed by inspecting the actual `ninja -t commands` compile
line**: this project's root `CMakeLists.txt` sets **no** `-Wall`/`-Wextra`/
`-Wswitch`/`-Werror` anywhere for `gte_core`/`GreatTamanaEngine` (only
third-party libraries such as `ktx`/`fmt` opt into `-Wall`/`-Werror`
*internally*, for their own sources only). GCC's `-Wswitch` (which is what
would normally warn "enumeration value not handled in switch" for a
`default:`-less switch) is not part of the default warning set and is not
enabled anywhere for this project's own code — so an unhandled enumerator is
silently accepted, with no diagnostic of any kind.

**This is confirmed to be a pre-existing condition of the whole codebase,
not a regression introduced by this phase**: every other "no `default:`
case, ever" exhaustive switch already in the codebase (`IsWriteAccess()`/
`ToString()` for `ResourceAccess`/`PassKind`/`RenderPassCategory`/
`RenderPassDrawKind`/`RenderPassEvent`, all in `RenderGraphTypes.cpp`) has a
trailing fallback `return false;`/`return "Unknown";` after its switch, so
none of them would have actually failed to compile for a missing case either
— before or after this phase's changes. `DispatchByKind()`'s own hard-fail
tail (`assert` + `throw`) is, if anything, a *strictly safer* design than
those precedents' silent-fallback tails, but it does not change this
underlying fact: the "exhaustive switch, no default" convention in this
codebase today is a **code-review/documentation convention**, never a
compiler-enforced one, since no build flag actually promotes an unhandled
enumerator into a diagnostic of any kind.

**Resolved via `ask_questions`** (recursive-rule question, since this is
exactly the kind of genuine ambiguity/design choice the task's own rules
require surfacing rather than silently guessing at): the project owner chose
to **document this honestly as a discovered pre-existing gap, not fix it as
part of this phase** — no build-flag change, no code-behavior change; the
exhaustive-switch discipline remains exactly what it already was, a
code-review convention. This finding is recorded here, and should be
propagated to this phase's own dedicated double-check (see below) and to
whoever eventually re-visits this project's compiler-warning configuration.

The scratch enumerator was then **reverted immediately** (confirmed by
diffing `RenderGraphTypes.h` back to its exact pre-scratch line count, 800
lines) and the full project was rebuilt clean (`cmake --build build`, zero
errors) before any further work continued.

## Verification

- **Fast, targeted incremental compile check, whole engine + whole test
  target** (per this phase's own rules — no full `ctest` regression run):
  - `cmake --build build --target gte_core` — succeeds, 0 errors/warnings.
  - `cmake --build build` (default target, builds `GreatTamanaEngine.exe` AND
    `GreatTamanaEngineTests.exe`) — succeeds, 0 errors/warnings.
- **Targeted test run** (this phase's own hard acceptance gate):
  - `tests\GreatTamanaEngineTests.exe --gtest_filter=RenderGraphCompiler*:RenderGraphSnapshot*:RenderGraphTypes*`
    — **58/58 tests pass** (1 `RenderGraphCompilerDeathTest` + 24
    `RenderGraphSnapshotTest` + 33 `RenderGraphCompilerTest`), zero
    regressions.
  - Also re-ran the full `RenderGraph*` filter (233 tests across 21 suites,
    including `RenderGraphBuilderTest`/`RenderGraphBarrierPlannerTest`/the new
    `RenderGraphDispatchByKindTest`) as an extra regression sanity check — all
    233 pass.
- **Live, HTTP-driven smoke test for Site 1** (`ApplyUsageBarrierIfNeeded`,
  the one call site with no automated regression oracle — see the strategy
  document's own "Tier-2 coverage gap" note):
  - `run_app_background` → `GET /get_logs?min_level=Warning` →
    `{"count":0,...}` (zero warnings/errors).
  - `GET /get_swapchain` — Editor renders correctly; sky/atmosphere gradient
    clearly visible in both Scene and Game panels.
  - `GET /activate_tab?name=Render Graph` + `GET /get_swapchain` — the
    "Render Graph" panel shows every real pass (Atmosphere Transmittance/
    Multi-Scattering/SkyView/Aerial-Perspective LUTs ×2 views, RenderOpaque)
    with correct real GPU timing and Reads/Writes columns, in the correct
    order — confirming the rewritten `ApplyUsageBarrierIfNeeded()`'s
    Texture/Buffer/VolumeTexture barrier resolution produces byte-identical
    real production behavior to before this phase.
  - `GET /get_game_view` — the Game view's own composited output renders
    correctly.
  - `GET /get_logs?min_level=Warning` (re-checked after the above
    interactions) — still `{"count":0,...}`.
  - `stop_app_background` — clean shutdown.

## Files touched

- `src/Renderer/RenderGraph/RenderGraphTypes.h` — new `DispatchByKind()`
  function template; two new includes (`<cassert>`, `<stdexcept>`).
- `src/Renderer/RenderGraph/RenderGraph.cpp` — `ApplyUsageBarrierIfNeeded()`'s
  switch converted to `DispatchByKind()`.
- `src/Renderer/RenderGraph/RenderGraphCompiler.cpp` — all seven of its
  switch sites (`sameResource`, the `firstXWriter` prescan, the RAW-edge
  scan, the inline fast-path check, the WAW-edge scan, the root-marking
  scan, the lifetime `touch()` lambda) converted.
- `src/Renderer/RenderGraph/RenderGraphSnapshot.cpp` — `ResourceUsageName()`'s
  switch converted, doc comment reworded.
- `tests/Renderer/RenderGraph/RenderGraphTypesTests.cpp` — new
  `RenderGraphDispatchByKindTest` suite (4 cases).

**Confirmed zero changes in**: `RenderGraphCompilerTests.cpp`,
`RenderGraphSnapshotTests.cpp`, `RenderGraphBuilderTests.cpp`,
`RenderGraphBuilder.h`/`.cpp`, `RenderGraphCompiler.h` (this phase's own
scope never required touching either of these), `src/Editor/FrameDebuggerData.cpp`
(its own `ReadRowLabelForKind()`/`WriteRowLabelForKind()` are explicitly out
of scope per the strategy document's own dedicated note).

## What was NOT touched

- `RenderGraphBuilder.h`'s public API, `PassRecord`, `PassContext` — outside
  this phase's scope.
- `DetectRenderPassEventContradictions()`'s public signature or its return
  value for any existing test's input — only its internal `sameResource`
  lambda's dispatch mechanism changed, exactly as permitted.
- `Compile()`'s determinism contract / `executionOrder` output shape —
  unaffected; every `RenderGraphCompilerTest` case (including the
  fast-path/standalone equivalence death test) still passes unmodified.
- `src/Editor/FrameDebuggerData.cpp`'s `ReadRowLabelForKind()`/
  `WriteRowLabelForKind()` — explicitly out of scope; these two functions
  DID fail to compile during the scratch-enumerator verification (confirmed,
  expected, and correctly ignored per the strategy document's own note) —
  wait, see the correction below.
- Item 2.10 (de-duplicating the near-identical Buffer/VolumeTexture branches
  inside `ApplyUsageBarrierIfNeeded`) — explicitly out of scope (P3/
  opportunistic), left exactly as-is.
- No build-flag/compiler-warning-configuration change was made, per the
  project owner's explicit `ask_questions` answer (see above).

**Correction to the bullet above, recorded honestly**: because no
`-Wswitch`/`-Wall` flag is enabled anywhere in this project's build (the
Step 3.3 finding above), `FrameDebuggerData.cpp`'s `ReadRowLabelForKind()`/
`WriteRowLabelForKind()` did **NOT** actually fail to compile during the
scratch-enumerator verification either — the strategy document's own
expectation that they would ("this is expected, correct... do not be
confused by extra compiler errors pointing there") did not materialize, for
the exact same root-cause reason `DispatchByKind()` itself did not fail.
Both are still correctly out of scope for this phase's own code changes;
only the *expected verification outcome* differed from what the strategy
document predicted, and that difference is the actual finding reported here.

## Next phase

Per this phase's own mandatory next step (`PHASE0_MASTER_STRATEGY.md`'s
Locked Design Decision 5), a dedicated `delegate_task` double-check pass for
THIS phase (PHASE6) is being spawned immediately, using
`PHASE6_RESOURCEKIND_DISPATCH_TABLE.md`'s own "Dedicated Double-Check
Instructions" section, before `PHASE7_DOCS_FULL_BUILD_AND_CAMPAIGN_COMPLETION.md`
begins. That dedicated double-check must also independently confirm (and
carry forward, if it agrees) the Step 3.3 finding recorded above — it is not
something later phases should silently forget or re-litigate as a surprise.
