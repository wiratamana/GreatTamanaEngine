# PHASE5 — Event-details section: verify + finish against real data

## Parent -> `PHASE0_MASTER_STRATEGY.md` (READ THIS FIRST — Locked Design Decision #6).
## Depends on: `PHASE1`..`PHASE4` (already landed).

## Step 1: The Goal

Confirm (and finish, wherever a small gap is found) that clicking a real leaf event in the tree
shows genuinely real Shader/Pass/Blend/Z-state/Stencil rows and real Textures/Vectors/Matrices
subsections — the smallest phase in this campaign by design, because `frame-debugger-2`'s own
PHASE6 already fully built this entire section's ImGui code against the exact real struct shape
this campaign populates; the only thing that was ever missing was real DATA, which PHASE1-4 just
supplied.

## Step 2: The Situation

- `FrameDebuggerPanel::BuildEventDetailsSection(const std::optional<FrameDebuggerEventDetails>&
  details)` (`src/Editor/Panels/FrameDebuggerPanel.cpp`) already renders: an `eventLabel` header,
  twelve Shader/Pass/Blend/Z-state/Stencil property rows, a `Preview`/`ShaderProperties` tab bar,
  and conditionally-shown Textures/Vectors/Matrices subsections — falling back to "No event
  selected." only when `details` is `std::nullopt`.
- `m_selectedEventIndex` is already correctly set by `RenderEventNode()`'s existing click handling
  (`ImGui::Selectable(...)` sets it) — this has been fully wired since `frame-debugger-2`'s own
  PHASE4, and needed zero changes even once PHASE4 of THIS campaign started producing real tree
  data (exactly as that campaign's own documentation predicted it would).
- `FindEventDetailsByIndex(snapshot, m_selectedEventIndex)` is already a correct, real, recursive
  lookup (`FrameDebuggerData.cpp`) — it will now actually return real values instead of always
  `std::nullopt`, with zero code changes of its own required.

## Step 3: The Plan

### 3.1 Verification pass (do this FIRST, before writing any code)

Build the engine, open the Frame Debugger, Enable + Capture a real frame (per PHASE3/PHASE4), and
click through EVERY real leaf event produced (the `"GameView"` leaf, and each `"GPU Skinning"`
leaf if any skinned models are in the current test scene). For each, confirm:
- The header reads the real `eventLabel` (`"Draw Mesh"` / `"Compute Dispatch"`), not empty.
- Shader/Pass/Blend/Z-state/Stencil rows show PHASE1/PHASE2's real strings, not blank/placeholder
  text.
- The Textures subsection is populated (Game View leaf) or genuinely absent/empty (GPU Skinning
  leaf, which has none — confirm the existing conditional-display logic already hides an empty
  Textures list correctly, per `frame-debugger-2`'s own "conditionally-shown... subsections"
  design; if it does not, that is this phase's own small bug to fix).
- The Vectors subsection shows the real clear-color + real `DrawStats` numbers formatted via
  `FormatVectorProperty()`.
- The Matrices subsection shows the real view-projection matrix formatted via
  `FormatMatrixProperty()`, laid out as 4 real rows of 4 numbers (visually inspect that it is NOT
  transposed/garbled — cross-check a couple of cells by hand against the real camera transform in
  use, e.g. confirm the translation column/row lands where `Mat4`'s own documented layout says it
  should).

### 3.2 Fix whatever the verification pass actually finds

Any genuine discrepancy found in 3.1 gets a small, targeted fix here — do not restructure
`BuildEventDetailsSection()`'s overall shape; a real bug here is almost certainly either (a) a data
population bug back in PHASE2's builder (fix it there, in `FrameDebuggerData.cpp`, not in the
Panel), or (b) a genuinely cosmetic ImGui layout nit local to `BuildEventDetailsSection()` itself.
Document which of the two in `PHASE5_COMPLETION_REPORT.md`.

### 3.3 What this phase explicitly does NOT do

- Does not add any new field to `FrameDebuggerEventDetails`/`FrameDebuggerEventNode` unless the
  verification pass proves one is genuinely missing to express something PHASE1/PHASE2 already
  captures but has nowhere to put — if that happens, treat it as a small, explicitly-noted
  addendum to PHASE2's own struct population, not a redesign.
- Does not touch Channels/Levels (PHASE6) or any HTTP endpoint (PHASE7).

### 3.4 Compile check

Fast compile check (`GreatTamanaEngine` target) plus the existing
`--gtest_filter=*FrameDebugger*` Tier-1 suite (should already be green, unchanged, unless 3.2 found
a data-population bug requiring a test update).

### 3.5 File-change inventory

Likely small/none beyond `PHASE5_COMPLETION_REPORT.md` itself, UNLESS the verification pass in
3.1 finds a genuine bug — in that case, whichever of `src/Editor/FrameDebuggerData.cpp`/
`src/Editor/Panels/FrameDebuggerPanel.cpp` actually owns the bug, plus a regression test if the fix
is in Tier-1 code (`FrameDebuggerData.cpp`), per `AGENTS.md`'s own testability rule.

Write `PHASE5_COMPLETION_REPORT.md` once done, explicitly stating whether any real bug was found
and fixed, or whether verification passed clean on the first try.
