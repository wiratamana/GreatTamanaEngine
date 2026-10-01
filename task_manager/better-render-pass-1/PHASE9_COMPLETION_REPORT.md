# PHASE9 — Scaffolding Idempotency Fix (R6) + Screen Post-Process Convenience API (Decision D3) — COMPLETION REPORT

**Parent:** `PHASE0_MASTER_STRATEGY.md`. **Task doc:**
`PHASE9_SCAFFOLDING_FIX_AND_SCREEN_POST_PROCESS_CONVENIENCE_API.md`.

## Summary

Both independent deliverables are complete.

**Part A (R6)**: `src/Editor/ScreenPassAutoWire.cpp`'s idempotency guard now performs TWO
independent "already active?" presence checks (one for the call line, one for the forward
declaration) instead of one, so re-scaffolding a pass name whose `Register...(core);` call was
manually commented out — while its forward declaration stayed active — no longer inserts a
duplicate forward declaration. A new regression test,
`ScreenPassAutoWireTest.TryAutoWireRegisterCallDoesNotDuplicateForwardDeclarationWhenOnlyTheCallIsCommentedOut`,
was confirmed to FAIL against the original buggy code (temporarily restored, rebuilt, and run) and
PASS after the fix was re-applied.

**Part B (Decision D3)**: `Core::AddScreenPostProcessPass()` is a new, additive convenience method
that fixes `stage` to `RenderFeatureStage::PostComposite` and auto-assigns a collision-tolerant
priority at runtime (a process-wide, monotonically-incrementing counter,
`NextAutoScreenPostProcessPassPriority()`, extracted into its own small, pure, Tier-1-tested
header/source pair) when the caller omits one. `Core::RegisterProjectRenderFeature()` itself is
byte-for-byte unchanged (confirmed via `git diff` showing no edits to its own
declaration/definition). The Editor's scaffold template
(`EditorProjectLifecycleCapability.cpp`'s `BuildScreenPostProcessPassCppContent()`) now generates a
call to the new, simpler API. Live-verified end to end against a real, running
`GreatTamanaEditor.exe`: a brand-new, hand-written file
(`Projects/ScreenPassAutoWireProbe/Assets/BlueTintScreenPass.cpp`) calls
`core.AddScreenPostProcessPass(...)` directly — bypassing the scaffolding tool entirely, with no
`stage`/`priority` argument at all — and its hand-wired call into `RegisterProject()` produced a
real, live `[PostComposite] BlueTint.ScreenTint - blend AlphaOver [Project]` row in the Editor's
"Render Graph" panel, confirmed by screenshot. This fixture is kept as a **permanent** addition to
`Projects/ScreenPassAutoWireProbe/` (per the task doc's own stated preference), proving the
zero-scaffolding path genuinely works, not merely in theory.

No `ask_questions` call was needed — every design fork this phase ran into was already resolved by
the task doc itself (the function-local `static` counter choice, the `InsertPendingLines()` helper
shape, the test-file target, etc.).

## Part A — what changed

### `src/Editor/ScreenPassAutoWire.cpp`

- The idempotency guard (Step 3) now computes two independent booleans via a shared primitive,
  `AnyNonCommentLineContains(lines, needle)`: `callAlreadyActive` (same needle as before,
  `registerFunctionName + "(core);"`) and `forwardDeclAlreadyActive` (`"void " +
  registerFunctionName + "(gte::Core& core);"`).
- If `callAlreadyActive` is true, the function returns `true` immediately, completely unchanged
  from the original behavior (if the call is active, the forward declaration must already be
  active too, or the project would not compile — nothing further to check or insert).
- Otherwise, a new `PendingLineInsertion{ index, line }` struct + `InsertPendingLines()` helper
  replaces the original hand-written if/else "insert at the larger index first" pair. The call
  line is ALWAYS scheduled (the function already returned early if it were active); the forward
  declaration is scheduled ONLY if `!forwardDeclAlreadyActive`. `InsertPendingLines()`
  `std::stable_sort`s its pending-insertion list in DESCENDING index order before applying each
  insertion in turn — this generalizes the original "insert at the larger index first" discipline
  to correctly handle either ONE or BOTH insertions happening, with no duplicated branching logic,
  and preserves the original tie-break behavior (forward declaration ends up physically BEFORE the
  call line when both resolve to the same insertion index) by construction: the call line is always
  pushed onto the pending list FIRST, the forward declaration SECOND, and
  `std::stable_sort`'s tie-preserving order plus "last-processed-at-a-tied-index-wins-the-front"
  mechanics combine to reproduce the exact original ordering — worked through by hand with a
  concrete tied-index example before trusting it, matching the task doc's own explicit instruction
  to do so.
- The file's own header comment gained a new dedicated section describing the PHASE9 fix, its root
  cause, and the corrected two-independent-presence-check contract.

### `tests/Editor/ScreenPassAutoWireTests.cpp` (already-registered file, no new `CMakeLists.txt` entry needed)

- New fixture, `BothAnchorsPresentWithActiveForwardDeclAndCommentedOutCallContent()` — both anchors
  present, an ALREADY ACTIVE (un-commented) forward declaration, and a COMMENTED-OUT call line —
  the exact confirmed bug shape.
- New test,
  `TryAutoWireRegisterCallDoesNotDuplicateForwardDeclarationWhenOnlyTheCallIsCommentedOut` —
  asserts the forward declaration appears EXACTLY ONCE after the call, while a fresh, active call
  line IS inserted (the call itself was not active).
- **Mandatory fail-before/pass-after verification performed**: the fix was temporarily reverted
  (the original pre-PHASE9 `ScreenPassAutoWire.cpp` body was restored verbatim), rebuilt, and
  `ctest -R TryAutoWireRegisterCallDoesNotDuplicateForwardDeclarationWhenOnlyTheCallIsCommentedOut`
  was run — it FAILED exactly as expected (`CountOccurrences(...) == 2`, expected `1`). The fix was
  then re-applied, rebuilt, and the same test (plus the whole `ScreenPassAutoWireTest` suite) passed.

## Part B — what changed

### `src/Core/ScreenPostProcessPassPriorityAssignment.h`/`.cpp` (new files)

- `std::int32_t NextAutoScreenPostProcessPassPriority(std::int32_t& counter)` — returns `counter`,
  then post-increments it. Pure, free-standing, external linkage, in its own header/source pair
  specifically so a Tier-1 test can call it directly with a plain local `std::int32_t`, mirroring
  `ScreenPassPriorityAssignment.h`'s own established "own file, external linkage, for testability"
  precedent. Added to `CMakeLists.txt`'s `gte_core` source list (hand-maintained, not a
  `CONFIGURE_DEPENDS` glob — confirmed before assuming otherwise).

### `src/Core/Core.h`/`Core.cpp`

- New public method, placed directly after `UnregisterProjectRenderFeature()` per the task doc's
  own instruction:
  ```cpp
  bool AddScreenPostProcessPass(const char* debugName, ProjectRenderFeatureCallback callback,
      RenderFeatureBlendMode blendMode = RenderFeatureBlendMode::AlphaOver,
      std::optional<std::int32_t> priority = std::nullopt);
  ```
- Implementation: a function-local `static std::int32_t s_autoScreenPostProcessPassPriorityCounter
  = 0;` (the task doc's own "lower-risk, smaller-diff choice" over a dedicated private `Core`
  member — no `ask_questions` needed, the task doc already resolved this fork), feeding
  `NextAutoScreenPostProcessPassPriority()` only when `priority` is empty, then forwarding straight
  into the existing, byte-for-byte-unchanged `RegisterProjectRenderFeature()` with
  `RenderFeatureStage::PostComposite` hard-coded.
- `<optional>` was already included in `Core.h` (confirmed before adding a duplicate include).

### `src/Editor/EditorProjectLifecycleCapability.cpp`

- `BuildScreenPostProcessPassCppContent()`'s template now generates a call to
  `core.AddScreenPostProcessPass("<Name>.ScreenTint", <callback-lambda>,
  /*blendMode=*/gte::RenderFeatureBlendMode::AlphaOver, /*priority=*/<N>);` instead of the full
  `core.RegisterProjectRenderFeature(...)` ceremony — no `stage`/`RenderFeatureStage` argument
  anywhere in the generated file anymore. The priority is still resolved via the EXISTING,
  scaffold-time, file-scanning `ComputeNextScreenPassPriority()` (unchanged — this remains strictly
  better than the new RUNTIME auto-assignment for a scaffolded file specifically, per the task
  doc's own Step 3 reasoning) and passed through as an explicit `priority` argument.
- **One consequence confirmed and fixed**: because `priority` moved from the 4th-of-5-positional
  argument (with a trailing comma, followed by the callback lambda) to the LAST positional
  argument of `AddScreenPostProcessPass()`, the generated line now ends `/*priority=*/<N>);`
  (closing paren) instead of `/*priority=*/<N>,` (trailing comma). This broke three
  ALREADY-REGISTERED, pre-existing tests that asserted the old, literal `/*priority=*/0,`/
  `/*priority=*/1,` substring — found via a full targeted test run (not assumed), and fixed by
  updating those three assertions to the new, correct `/*priority=*/0);`/`/*priority=*/1);` shape
  (see "Pre-existing tests updated" below). This is the one genuine, confirmed deviation this phase
  needed beyond the task doc's own literal text — a necessary, mechanical consequence of changing
  the generated call's own argument order, not a design change.
- `<array>` remains needed/present (the push-constant-free `TextureHandle`/`RenderGraphBuilder`
  example still uses `std::array<float, 4>` for the clear color) — confirmed before assuming
  otherwise.
- Confirmed, via `search_in_dir`, that no reference to `RenderFeatureStage`/`stage` remains
  anywhere in the generated template content itself after this edit (only this file's own
  surrounding `.cpp` comments mention it, for historical context).

### Pre-existing tests updated (to match the new, correct generated-content shape — NOT new test coverage, pure consequence fixes)

- `tests/Editor/AssetScaffoldTemplateTests.cpp` —
  `ScreenPostProcessPassFirstScaffoldWithBothAnchorsAutoWiresAtPriorityZero` and
  `ScreenPostProcessPassSecondScaffoldSameProjectIncrementsPriorityAndKeepsFirstCallIntact`:
  `/*priority=*/0,`/`/*priority=*/1,` → `/*priority=*/0);`/`/*priority=*/1);`, plus a new assertion
  confirming `core.AddScreenPostProcessPass(` is present in the generated file.
- `tests/Network/CreateAssetEndpointEndToEndTests.cpp` —
  `ScreenPostProcessPassScaffoldWritesExpectedFileWithFallbackReminder`: same
  `/*priority=*/0,` → `/*priority=*/0);` fix.
- `tests/Editor/ScreenPassPriorityAssignmentTests.cpp` — **confirmed NOT needing any change**: its
  own `ScreenPassFileContent()` fixture is an independent, synthetic scratch-file builder testing
  `ComputeNextScreenPassPriority()`'s own pure token-scanning logic directly (which only cares about
  the literal `/*priority=*/` token followed by digits, never what comes after the digits) — it
  does not call or assert against the real template at all, so it was correctly left untouched.

### `tests/Core/ScreenPostProcessPassPriorityAssignmentTests.cpp` (new file, registered in `tests/CMakeLists.txt`)

Four Tier-1 tests for `NextAutoScreenPostProcessPassPriority()`'s pure counter logic: first call
returns 0 and increments to 1; repeated calls increment monotonically with no gaps; a non-zero
starting value is handled correctly (never assumed to start at 0); two independent counters never
interfere with each other.

### `tests/Core/RegisterProjectRenderFeatureApiTests.cpp` (already-registered file, extended)

Three new Tier-1 tests for `Core::AddScreenPostProcessPass()` itself, reusing this file's own
established headless-`Core` fixture (`GTE_SKIP_IF_NO_HEADLESS_CORE`): a call with no explicit
priority registers successfully with `stage == "PostComposite"`; an explicit priority/blendMode is
honored exactly (never overridden by the auto-assignment counter); two back-to-back auto-priority
calls get distinct, strictly-increasing priorities (an end-to-end proof the pure counter logic is
genuinely wired into `Core`, not just correct in isolation). **These three (plus all pre-existing
tests in this file) report `Skipped` on this development machine** — this machine's Vulkan
driver/loader does not support `VK_EXT_headless_surface`, the same pre-existing, already-documented
environment limitation every other test in this file already has (confirmed: every test in this
file, old and new, skips identically) — not a new gap this phase introduces.

## Live verification (Part B, Step 7 — permanent fixture, not reverted)

1. Added `Projects/ScreenPassAutoWireProbe/Assets/BlueTintScreenPass.cpp`, hand-written, calling
   `core.AddScreenPostProcessPass("BlueTint.ScreenTint", <lambda>)` with **zero** `stage`/`priority`
   argument at all (the lambda clears its private target to a translucent blue,
   `(0.0, 0.2, 1.0, 0.2)`, mirroring every sibling `*ScreenPass.cpp`'s minimal shape).
2. Hand-added (NOT via `TryAutoWireRegisterCall()` / the scaffolding tool) the matching forward
   declaration + call line into `ScreenPassAutoWireProbeGame.cpp`'s own `RegisterProject()`.
3. `cmake --build build` — succeeded (the Project Assembly `.dll` auto-discovers the new `.cpp` via
   its own `CONFIGURE_DEPENDS` glob; a CMake re-run + rebuild compiled and linked
   `BlueTintScreenPass.cpp.obj` into `ScreenPassAutoWireProbe_Game.dll` with zero errors).
4. Launched `GreatTamanaEditor.exe` via `run_app_background`.
5. `GET /get_logs?min_level=Error` → `{"count":0,...}` immediately after startup.
6. `GET /activate_tab?name=Render%20Graph` → `{"activated_tab":"Render Graph","success":true}`.
7. `GET /get_swapchain` → a real screenshot of the "Render Graph" panel's "Plugin Render Features"
   section showing `[PostComposite] BlueTint.ScreenTint - blend AlphaOver  [Project]` as a real,
   live, enabled row — alongside the project's three pre-existing, scaffolded
   `RedTint.ScreenTint`/`YellowTint.ScreenTint`/`IterE.ScreenTint` rows (each still tagged
   `[Project]`) — conclusive, direct proof `Core::AddScreenPostProcessPass()` genuinely registers a
   real render feature end to end, through a hand-written call that never touched the scaffolding
   tool at all.
8. `stop_app_background` — Editor closed cleanly.
9. **Decision**: per the task doc's own stated preference ("a PERMANENT fixture proving this path
   is strongly preferred if it can be added with minimal risk"), `BlueTintScreenPass.cpp` and the
   two hand-wired lines in `ScreenPassAutoWireProbeGame.cpp` were KEPT, not reverted. Note:
   `Projects/` is `.gitignore`d (confirmed via `git status` — this change does not appear in the
   tracked diff at all), so this permanent fixture lives only on this development machine's working
   tree, exactly like every other file under `Projects/ScreenPassAutoWireProbe/`.

## Design decisions resolved (no `ask_questions` needed)

1. **`InsertPendingLines()` shape** — a small `PendingLineInsertion{ index, line }` struct +
   `std::stable_sort`-then-insert helper, rather than the task doc's own suggested
   `InsertLineIfMissing(lines, index, line, alreadyPresent)` signature — functionally equivalent,
   chosen because it generalizes correctly to "zero, one, or two" pending insertions with a single
   code path (no `if (alreadyPresent) { ... } else { ... }` duplication at each of the two call
   sites), and its ordering/tie-break behavior was hand-verified against a concrete example before
   trusting it, per the task doc's own explicit instruction.
2. **Counter storage: function-local `static`, not a `Core` member** — the task doc explicitly
   named this as "the lower-risk, smaller-diff choice," and no genuine ambiguity remained once that
   was read.
3. **Priority still resolved via `ComputeNextScreenPassPriority()` for scaffolded files** — the task
   doc's own Step 4 already resolved this; the new runtime counter exists purely for the
   zero-scaffolding hand-written case.
4. **Test file targets** — `tests/Core/ScreenPostProcessPassPriorityAssignmentTests.cpp` (new) for
   the pure counter, and `tests/Core/RegisterProjectRenderFeatureApiTests.cpp` (already-registered,
   extended) for the live, headless-`Core`-backed API proof — both confirmed as the correct,
   already-established homes via `browse_dir`/`search_in_dir` before creating/extending either.
5. **The three pre-existing-test fixups** (`/*priority=*/0,` → `/*priority=*/0);`, etc.) were
   discovered by actually RUNNING the full targeted test sweep after the template change, not
   assumed — see "Pre-existing tests updated" above.

## Compile check and targeted test run

- `cmake --build build` (incremental, from the repository root) — succeeded end to end, multiple
  times across this phase's own edit/verify/revert/re-verify cycles, zero errors.
- `ctest -R "ScreenPassAutoWire|ScreenPostProcessPassPriorityAssignment|RegisterProjectRenderFeatureApi|AssetScaffoldTemplateTest|CreateAssetEndpointEndToEndTest|ScreenPassPriorityAssignment" --output-on-failure`
  (from `build/`) — **52 tests, 100% of executed tests passing** (10 `RegisterProjectRenderFeatureApiTest`
  cases `Skipped` — this machine's pre-existing, already-documented lack of
  `VK_EXT_headless_surface` support, not a new gap). This includes:
  - 8 `ScreenPassAutoWireTest` cases (1 brand-new regression test, 7 pre-existing, all passing).
  - 4 brand-new `ScreenPostProcessPassPriorityAssignmentTest` cases.
  - 7 `RegisterProjectRenderFeatureApiTest` cases pre-existing + 3 brand-new (all 10 `Skipped` on
    this machine, consistently, old and new alike).
  - 11 `AssetScaffoldTemplateTest` cases (2 fixed for the new generated-content shape, 9 unaffected,
    all passing).
  - 7 `CreateAssetEndpointEndToEndTest` cases (1 fixed for the new generated-content shape, 6
    unaffected, all passing).
  - 10 `ScreenPassPriorityAssignmentTest` cases (confirmed unaffected, all passing).
- The mandatory fail-before/pass-after cycle for the Part A regression test (see above) was run as
  its own separate, explicit verification step before considering Part A done.

Per this campaign's own process rule (Note 4/5), no full clean build or full `ctest` regression
pass was run in this phase — that is reserved for PHASE10.

## Acceptance bar — final check

### Part A
- [x] The new regression test fails before the fix, passes after (explicitly, separately verified).
- [x] Every pre-existing `ScreenPassAutoWireTests.cpp` test still passes unmodified.
- [x] Compile-check + targeted test run only (not a full suite).

### Part B
- [x] `Core::AddScreenPostProcessPass()` exists, compiles, and is proven — by BOTH a live, HTTP-driven
      verification AND a Tier-1 test for its pure counter logic — to correctly register a working
      screen post-process pass with zero `stage`/`priority` argument required.
- [x] `Core::RegisterProjectRenderFeature()` itself is provably byte-for-byte unchanged (`git diff`
      shows no edits to its own declaration/definition — only a new method added immediately after
      it).
- [x] The Editor's scaffold template generates the new, simpler call.

## Files changed this phase

- `src/Editor/ScreenPassAutoWire.cpp` — idempotency-guard fix (Part A).
- `tests/Editor/ScreenPassAutoWireTests.cpp` — new fixture + regression test (Part A).
- `src/Core/ScreenPostProcessPassPriorityAssignment.h`/`.cpp` — new, pure counter-increment helper
  (Part B).
- `src/Core/Core.h`/`Core.cpp` — new `AddScreenPostProcessPass()` method (Part B).
- `src/Editor/EditorProjectLifecycleCapability.cpp` — scaffold template updated to call the new API
  (Part B).
- `tests/Core/ScreenPostProcessPassPriorityAssignmentTests.cpp` — new Tier-1 tests for the pure
  counter (Part B).
- `tests/Core/RegisterProjectRenderFeatureApiTests.cpp` — three new Tier-1 tests for
  `AddScreenPostProcessPass()` (Part B).
- `tests/Editor/AssetScaffoldTemplateTests.cpp` — two pre-existing assertions fixed for the new
  generated-content shape (consequence of Part B's template change).
- `tests/Network/CreateAssetEndpointEndToEndTests.cpp` — one pre-existing assertion fixed for the
  same reason.
- `CMakeLists.txt` — registered the two new `src/Core/ScreenPostProcessPassPriorityAssignment.*`
  files in `gte_core`'s hand-maintained source list.
- `tests/CMakeLists.txt` — registered the new
  `tests/Core/ScreenPostProcessPassPriorityAssignmentTests.cpp` file.
- `Projects/ScreenPassAutoWireProbe/Assets/BlueTintScreenPass.cpp` (new, permanent, `.gitignore`d) +
  `ScreenPassAutoWireProbeGame.cpp` (hand-wired, `.gitignore`d) — the permanent, live-verification
  fixture for Part B's zero-scaffolding path.

This closes PHASE9, independent of PHASE1-8 (touched only `src/Editor/ScreenPassAutoWire.cpp`,
`src/Core/Core.h/.cpp`, a new `src/Core/ScreenPostProcessPassPriorityAssignment.h/.cpp` pair, and
`src/Editor/EditorProjectLifecycleCapability.cpp`, plus tests, exactly as scoped — no change to
`RenderGraphCompiler`/`RenderGraphBarrierPlanner`/`RenderGraphSnapshot`/`RenderPassEvent`/
`RenderPassCategory`/`RenderPassDrawKind`/`ViewScope`/`RenderPassTagMask`, and
`Core::RegisterProjectRenderFeature()` remains fully available, byte-for-byte unchanged). PHASE10 is
the only remaining phase, and the only one allowed to run a full clean build + full `ctest`
regression pass.
