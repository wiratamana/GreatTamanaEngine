# PHASE9 — Scaffolding Idempotency Fix (R6) + Screen Post-Process Convenience API (Decision D3)

**Parent:** `PHASE0_MASTER_STRATEGY.md` (read it first). Independent of `PHASE1`-`PHASE8` (touches
`src/Editor/ScreenPassAutoWire.cpp`, `src/Core/Core.h/.cpp`,
`src/Core/Plugins/RenderFeatureCompositor.h/.cpp`, and
`src/Editor/EditorProjectLifecycleCapability.cpp` only) — may run any time; scheduled last among
the "new capability" phases so its own regression test runs against an otherwise-stable tree.

This phase has TWO independent deliverables — implement and verify them separately, in order:
**Part A (R6, the confirmed bug fix)** first, **Part B (Decision D3, the new convenience API)**
second.

---

## PART A — Fix the confirmed idempotency bug (Finding 10(b), R6)

### Step 1 — The Goal

`ScreenPassAutoWire.cpp`'s idempotency guard must also check whether the matching forward
declaration is already present (not just whether the call itself is active) before inserting a
new one — so re-scaffolding a pass name whose `Register...(core);` call was manually commented out
never produces a duplicate forward declaration.

### Step 2 — The Situation (confirmed, `src/Editor/ScreenPassAutoWire.cpp`, ~lines 171-201)

```cpp
// Step 3 (idempotency guard): scan every NON-COMMENT line for an already active call...
const std::string activeCallNeedle = registerFunctionName + "(core);";
for (const std::string& line : lines) {
    if (IsCommentLine(line)) continue;
    if (line.find(activeCallNeedle) != std::string::npos) {
        return true; // Already correctly, ACTIVELY wired - nothing to do.
    }
}

const std::string forwardDeclLine = "void " + registerFunctionName + "(gte::Core& core);";
const std::string callLine = "    " + registerFunctionName + "(core);";

// ... resolves BOTH insertion indices, inserts BOTH forwardDeclLine AND callLine unconditionally
// whenever the loop above didn't early-return true.
```

If a human comments out ONLY the call line (`"// " + registerFunctionName + "(core);"`, e.g. to
temporarily disable one effect — a workflow this file's own header comment explicitly anticipates)
while the forward declaration remains active and un-commented, the loop above does NOT find an
active call, falls through, and unconditionally inserts BOTH a new forward declaration (a genuine
DUPLICATE — the old one is still there, un-commented) and a new call line. This is the confirmed,
live bug.

### Step 3 — The Plan

1. Add a SECOND scan, mirroring the first exactly but against
   `"void " + registerFunctionName + "(gte::Core& core);"` (the forward-declaration needle),
   producing a second bool, e.g. `forwardDeclAlreadyActive`.
2. Change the insertion logic so EACH of the two lines is inserted INDEPENDENTLY, conditioned on
   its OWN already-active flag:
   - If `activeCallAlreadyPresent` is already true for BOTH (current early-return behavior):
     return `true` immediately, unchanged.
   - Otherwise, compute `forwardDeclInsertionIndex`/`bodyInsertionIndex` as today, but only push
     `forwardDeclLine` onto the `lines` vector if `!forwardDeclAlreadyActive`, and only push
     `callLine` if the call is not already active (which, having reached this point, it never is —
     the function already returned early if it was — so the call line is ALWAYS inserted once this
     point is reached; only the forward-declaration insertion becomes conditional).
   - Preserve the existing "insert at the larger index first" ordering discipline (Step 3.3 of the
     ORIGINAL implementation's own comment) for whichever of the two insertions actually happen —
     if only one of the two needs to happen, that logic degenerates to a single, simple insertion;
     write it so both the "insert both" and "insert only the call line" cases are handled
     correctly without duplicated logic (a single small helper, e.g.
     `InsertLineIfMissing(lines, insertionIndex, line, alreadyPresent)`, called twice, each
     independently deciding whether to actually mutate `lines`, is a clean way to express this —
     confirm the exact insertion-index math still holds correctly when only one of the two lines
     is actually inserted, since skipping one insertion changes how the OTHER insertion's own
     index should be interpreted relative to the original, pre-any-insertion `lines` vector — work
     through this carefully with a concrete example before trusting it, and write the new
     regression test in Step 4 FIRST, confirm it fails against the ORIGINAL buggy code by
     temporarily reverting your own fix, then re-apply the fix and confirm it passes).
3. Update this file's own header comment to describe the corrected idempotency contract (two
   independent presence checks, not one).

### Step 4 — Regression test (mandatory, must fail before the fix, pass after)

Add a new test to `tests/Editor/ScreenPassAutoWireTests.cpp` (the existing test file — confirm it
exists via `browse_dir`/`read_file`, per this class's own header comment naming it directly):
e.g. `TryAutoWireRegisterCallDoesNotDuplicateForwardDeclarationWhenOnlyTheCallIsCommentedOut` —
build a fixture `<ProjectName>Game.cpp` content with BOTH anchor comments present, an ALREADY
ACTIVE (un-commented) forward declaration for `RegisterFooScreenPass`, and a COMMENTED-OUT call
line (`"// RegisterFooScreenPass(core);"`), call `TryAutoWireRegisterCall()`, and assert the
resulting content contains the forward declaration EXACTLY ONCE (e.g. via counting occurrences of
the substring, not just `find()`'s first hit) while the call line WAS newly inserted (since the
existing one is commented out, hence "not active"). Confirm this test FAILS against the
pre-fix code (temporarily `git stash`/revert Part A's own fix, run this one test, confirm
failure, then reapply) before considering Part A done.

### Acceptance bar for Part A

- The new regression test fails before the fix, passes after.
- Every pre-existing `ScreenPassAutoWireTests.cpp` test still passes unmodified.
- Compile-check + targeted test run only (not a full suite — PHASE10's job).

---

## PART B — `Core::AddScreenPostProcessPass()` convenience API (Decision D3)

### Step 1 — The Goal

Cut the common "draw one clear/tint/simple effect over the screen" case down to ONE direct
function call a game author can hand-write inline inside their own `RegisterProject(gte::Core&
core)` function — no `stage`/`blendMode`/`priority` arguments to think about, no scaffolding tool
required (though the scaffolding tool MAY still be used and will now emit this simpler call
instead of the full `RegisterProjectRenderFeature()` ceremony) — while
`Core::RegisterProjectRenderFeature()` itself remains completely untouched, byte-for-byte, for
any advanced user who genuinely needs explicit stage/blend/priority control (R7).

### Step 2 — The Situation

`Core::RegisterProjectRenderFeature()`'s real signature (`Core.h` ~line 391, `Core.cpp` ~line 382,
confirmed):

```cpp
bool RegisterProjectRenderFeature(const char* debugName, RenderFeatureStage stage,
    RenderFeatureBlendMode blendMode, std::int32_t priority, ProjectRenderFeatureCallback callback);
```

Only `RenderFeatureStage::PostComposite` and `::PreUI` are actually wired into the live render
graph today (`plugins/gte_plugin_abi/RenderFeatureDescriptor.h`'s own explicit comment) — a
"screen post-process pass" concept, by definition, means `PostComposite` (the existing scaffold
template already hardcodes this exact value — confirmed,
`EditorProjectLifecycleCapability.cpp` ~line 300). `ComputeNextScreenPassPriority()`
(`ScreenPassPriorityAssignment.h`) is a SCAFFOLD-TIME, file-scanning mechanism — it cannot be
reused for a hand-written call that never goes through the scaffolding tool at all (it has no
"sibling `*ScreenPass.cpp` files on disk" to scan against in general). This new API needs its own,
independent, RUNTIME auto-priority mechanism: a simple monotonically-incrementing counter,
shared process-wide (not per-project), read and incremented once per call.

`RenderFeatureCompositor`'s own documented collision policy (`RenderFeatureDescriptor.h`'s
`priority` field doc comment) already tolerates a priority collision gracefully (a loud
`GTE_LOG_WARNING` + a stable, deterministic lexical tie-break, never a crash) — a simple
incrementing counter is therefore a SAFE choice even though it does not guarantee global
uniqueness against a caller who ALSO separately calls the full
`RegisterProjectRenderFeature()` with an explicit, colliding priority value; this is an accepted,
already-documented degrade, not a new risk this phase introduces.

### Step 3 — The Plan

1. **Add the new method to `Core`** (`Core.h`, placed directly after
   `RegisterProjectRenderFeature()`/`UnregisterProjectRenderFeature()`):
   ```cpp
   // better-render-pass-1 campaign, PHASE9 (Decision D3) - additive convenience wrapper over
   // RegisterProjectRenderFeature(): fixes stage to RenderFeatureStage::PostComposite (the one,
   // real "draw over the final composited screen" hook point this concept means), and
   // auto-assigns a collision-tolerant priority at RUNTIME (a simple, monotonically-incrementing
   // counter - see Core.cpp) when the caller does not supply one explicitly. Cuts the common case
   // down to ONE call: core.AddScreenPostProcessPass("Name", [](rg::RenderGraphBuilder& builder,
   // rg::TextureHandle target, VkExtent2D extent) { ... }); - no stage/priority argument required
   // at all. RegisterProjectRenderFeature() itself is UNCHANGED and remains available for any
   // caller needing explicit stage/blend/priority control (e.g. RenderFeatureStage::PreUI, or a
   // specific hand-chosen priority for managed cross-plugin compositing order).
   bool AddScreenPostProcessPass(const char* debugName, ProjectRenderFeatureCallback callback,
       RenderFeatureBlendMode blendMode = RenderFeatureBlendMode::AlphaOver,
       std::optional<std::int32_t> priority = std::nullopt);
   ```
2. **Implement in `Core.cpp`**, directly after `RegisterProjectRenderFeature()`:
   ```cpp
   bool Core::AddScreenPostProcessPass(const char* debugName, ProjectRenderFeatureCallback callback,
       RenderFeatureBlendMode blendMode, std::optional<std::int32_t> priority)
   {
       const std::int32_t resolvedPriority = priority.has_value() ? *priority : NextAutoScreenPostProcessPassPriority();
       return RegisterProjectRenderFeature(
           debugName, RenderFeatureStage::PostComposite, blendMode, resolvedPriority, std::move(callback));
   }
   ```
   `NextAutoScreenPostProcessPassPriority()` — a small private `Core` method (or a function-local
   `static std::int32_t` counter inside `AddScreenPostProcessPass()` itself, which is simpler and
   equally correct since `Core` is a singleton-per-process composition root in practice — confirm
   via `ask_questions` which style this codebase's own reviewers would prefer if genuinely
   unsure, though a function-local static is the lower-risk, smaller-diff choice) — starts at 0,
   post-increments on every call with no explicit `priority` supplied.
3. **`#include <optional>`** in `Core.h` if not already present (confirm before adding a
   duplicate include).
4. **Update the Editor's scaffold template** (`EditorProjectLifecycleCapability.cpp`'s
   `BuildScreenPostProcessPassCppContent()`, ~line 273-334) to generate a call to the new
   `core.AddScreenPostProcessPass(...)` instead of the full `core.RegisterProjectRenderFeature(...)`
   — the generated file becomes shorter (no `stage`/`blendMode` arguments to explain in the
   generated comment), while the priority-auto-assignment comment/behavior can stay EXACTLY as
   today (still resolved via `ComputeNextScreenPassPriority()` at SCAFFOLD TIME and passed as an
   explicit `priority` argument into the new call — this is still strictly better than leaving it
   to runtime auto-assignment for a SCAFFOLDED file specifically, since scaffold-time scanning
   already correctly avoids collisions among sibling scaffolded files; the new RUNTIME
   auto-assignment in Step 2 exists for the case where NO scaffolding tool was ever used at all).
   Confirm the generated `#include` list still needs `<array>` (the push-constant-free
   `TextureHandle`/`RenderGraphBuilder` based example), and that nothing else in the template
   references `stage`/`RenderFeatureStage` anymore once this edit lands.
5. **Add a Tier-1 test** — `tests/Core/CoreTests.cpp` (or wherever `Core`'s own existing tests
   live, if any; if `Core` has no direct unit tests due to its own construction requirements,
   confirm this via `browse_dir`/`search_in_dir` and, if so, document this as an accepted Tier-2
   gap rather than inventing a fragile fake-`Core` harness) proving
   `NextAutoScreenPostProcessPassPriority()`'s pure counter logic increments correctly across
   repeated calls and is never consulted when an explicit `priority` is supplied — if this logic
   cannot be cleanly isolated as a pure, Tier-1-testable unit given `Core`'s own real shape,
   extract it into its own tiny, free-standing pure function first (mirrors this whole codebase's
   own "extract pure logic before wiring it into a GPU/SDL-owning class" testability rule, per
   `AGENTS.md`), exactly like `ComputeGroupCount()`/`PushConstantSizeMatches()` were already
   extracted elsewhere in this campaign.
6. Compile-check (incremental).
7. **Live verification**: hand-write a tiny, temporary test call inside
   `Projects/ScreenPassAutoWireProbe/`'s own `RegisterProject()` (or the dedicated probe project
   this campaign's own PHASE10 will use — coordinate with that phase's own plan) using the new
   `core.AddScreenPostProcessPass(...)` API directly (bypassing the scaffolding tool entirely),
   confirm via `gte_send_request` that the effect renders, then decide (document either way)
   whether to leave this as a small, permanent, additional probe fixture proving the "hand-written,
   zero-scaffolding" path genuinely works, or revert it as a throwaway confidence check — a
   PERMANENT fixture proving this path is strongly preferred if it can be added with minimal risk,
   since it is the concrete, lasting proof Decision D3's own goal ("no second file to be
   text-edited at all," for a hand-written caller) is real, not merely theoretical.
8. Write `PHASE9_COMPLETION_REPORT.md` covering BOTH Part A and Part B, commit.

### Acceptance bar for Part B

- `Core::AddScreenPostProcessPass()` exists, compiles, and is proven (by a live verification at
  minimum, ideally also a Tier-1 test for its pure counter logic) to correctly register a working
  screen post-process pass with zero `stage`/`priority` argument required.
- `Core::RegisterProjectRenderFeature()` itself is provably byte-for-byte unchanged (`git diff`
  shows no edits to its own declaration/definition).
- The Editor's scaffold template generates the new, simpler call.
