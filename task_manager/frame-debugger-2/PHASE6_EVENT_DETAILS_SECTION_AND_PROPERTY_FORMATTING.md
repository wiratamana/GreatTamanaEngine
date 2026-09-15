# PHASE6 — Event-details section, tab bar, and property formatting

Parent: `PHASE0_MASTER_STRATEGY.md`
Depends on: PHASE1 (`FrameDebuggerEventDetails`/`FindEventDetailsByIndex()`),
PHASE4 (`m_selectedEventIndex`), PHASE5 (the inspector pane it appends to).
Touches: `src/Editor/FrameDebuggerData.h/.cpp` (new field + formatting helpers +
tests), `src/Editor/Panels/FrameDebuggerPanel.h/.cpp`,
`tests/Editor/FrameDebuggerDataTests.cpp`.

**This is flagged in `PHASE0_MASTER_STRATEGY.md` (Step 3.5) as the
heaviest single phase in this campaign** — the most new widgets, the most
new pure helper functions, and the busiest region of the reference
screenshot. Read this whole document twice before starting; do not skip
the "always nullopt in production" framing below — it is easy to
accidentally invent fake data here if you lose track of it mid-
implementation.

## Step 1: The Goal

Build the BOTTOM portion of the right-hand inspector pane — the part of
the reference screenshot that is scoped to exactly ONE selected draw
event: the `Event #N: Draw Mesh` header, the Shader/Pass/Blend/ZClip/
ZTest/ZWrite/Cull/Stencil(Ref/Comp/Pass/Fail/ZFail) property rows, the
`Preview`/`ShaderProperties` tab bar, and (inside the ShaderProperties
tab) the Textures/Vectors/Matrices subsections. Write this section as a
**fully real, complete** renderer against a real
`FrameDebuggerEventDetails` value — but gate it behind
`m_selectedEventIndex`/`FindEventDetailsByIndex()`, which **always**
resolve to nothing this campaign (nothing can ever be selected — PHASE4's
tree is always empty), so in practice this section **always** renders a
single **"No event selected."** line. This is the correct, expected,
final state for this campaign — not a bug, not an oversight.

## Step 2: The Situation

- `FrameDebuggerEventDetails` (PHASE1) already has every PROPERTY-ROW
  field this section needs: `shaderName`, `passName`, `blendMode`,
  `zClip`, `zTest`, `zWrite`, `cull`, `stencilRef`, `stencilComp`,
  `stencilPass`, `stencilFail`, `stencilZFail`, `textures`, `vectors`,
  `matrices`. It does **not**, however, carry anything suitable for the
  section's own header line (the reference screenshot's
  `"Event #2117: Draw Mesh"`) — `shaderName` is a shader asset name (e.g.
  `"Standard"`), not an operation kind like `"Draw Mesh"`, and no other
  existing field means that either. Reusing `shaderName` there would be
  actively wrong once real data ever exists (a future event with, say,
  shader `"Standard"` and op-kind `"Clear"` would misleadingly print
  `"Event #N: Standard"`). This phase therefore appends ONE new field,
  `eventLabel`, to the end of the `FrameDebuggerEventDetails` struct (see
  3.1 below) specifically to back that header — deliberately appended at
  the very END of the struct's field list, never inserted in the middle,
  mirroring `BoneViewerWindow.h`'s own explicit "never insert a field in
  the middle of a struct some call site might positionally
  aggregate-initialize" precedent (see that file's `RigidBodyEntry::
  collisionGroupMask` comment) — safe regardless of whether any existing
  hand-built test snapshot happens to use named field assignment or
  positional aggregate init.
- `FindEventDetailsByIndex(snapshot, m_selectedEventIndex)` (PHASE1) is
  the exact function to call — it already does the correct recursive
  tree search and already returns `std::optional<FrameDebuggerEventDetails>`,
  matching this section's own `std::nullopt` -> "No event selected."
  contract exactly.
- `ImGui::BeginTabBar("...")`/`ImGui::BeginTabItem("...")`/`EndTabItem()`/
  `EndTabBar()` is Dear ImGui's standard tab-bar API — already used
  nowhere else in this codebase yet (confirmed: no `BeginTabBar` call
  exists anywhere under `src/` today), so this is this engine's first tab
  bar; keep it simple (two fixed tabs, "Preview" and "ShaderProperties",
  no closable/reorderable flags needed). Signatures, confirmed directly
  against the vendored `third_party/imgui/imgui.h` for this repo's actual
  fetched ImGui version: `bool BeginTabBar(const char* str_id,
  ImGuiTabBarFlags flags = 0)`, `bool BeginTabItem(const char* label,
  bool* p_open = NULL, ImGuiTabItemFlags flags = 0)`, `void
  EndTabItem()`, `void EndTabBar()` — `EndTabBar()` must only be called
  when `BeginTabBar()` returned true (exactly as this phase's own code in
  3.5 already does), and each `EndTabItem()` must only be called when its
  matching `BeginTabItem()` returned true (also already correct below).
- The reference screenshot's "Preview" tab shows a live rendered preview
  of that one draw call in isolation (Unity's real feature) — **this
  campaign's "Preview" tab is a plain "Not available yet." message**,
  since no real per-draw-call preview rendering exists (nor will it,
  this campaign — a genuinely different, much larger scope for a future
  campaign to size separately). Do not attempt anything resembling a
  real preview here.
- Formatting a `FrameDebuggerVectorProperty`/`FrameDebuggerMatrixProperty`
  into the reference screenshot's own display convention (`"(1, 1, 1, 1)"`
  for a vector; a 4-row grid of numbers for a matrix) is exactly the kind
  of small, pure, Tier-1-testable formatting logic `AGENTS.md`'s
  "Testability & Regression Safety" section asks for — mirror
  `RenderGraphPanel.cpp`'s own local `FormatGpuTiming()`/`JoinNames()`
  helpers' *shape* (both confirmed present in that file today), but
  promote these two into the genuinely-shared, already-Tier-1-tested
  `FrameDebuggerData.h/.cpp` (PHASE1's file) instead of a panel-local
  anonymous-namespace helper, specifically because they operate on pure
  data with no ImGui dependency at all and deserve real unit tests
  (`RenderGraphPanel.cpp`'s own helpers are NOT unit-tested, because they
  live in a Tier-2 `.cpp` file, not a Tier-1 one — this phase's own
  helpers should NOT repeat that gap, since `FrameDebuggerData.h/.cpp`
  already is that Tier-1 file).
- Both new `std::size_t`-using functions below need `<cstddef>` added to
  whichever file uses `std::size_t` explicitly — this codebase's own
  established convention (see `Panels/RenderGraphPanel.cpp`'s own
  `#include <cstddef>` line, needed there for exactly the same reason:
  explicit `std::size_t` casts/locals). Relying on some other transitively
  -included standard header to have already pulled it in is not this
  codebase's convention and must not be assumed here either.

## Step 3: The Plan

### 3.1 `FrameDebuggerData.h` — one new struct field, two new pure formatting functions

First, append ONE new field to the end of the existing
`FrameDebuggerEventDetails` struct (defined in PHASE1's own
`FrameDebuggerData.h`, right after its own last field, `matrices`):

```cpp
    // Short, display-ready label for this event's own OPERATION KIND
    // (e.g. "Draw Mesh", "Clear", "SetRenderTarget") - matches the
    // reference screenshot's own "Event #2117: Draw Mesh" header
    // convention. Deliberately its own copy rather than re-deriving it
    // from the owning FrameDebuggerEventNode::name (which may carry
    // extra detail, e.g. "Draw Mesh pf_fence_02") - this struct is
    // self-contained on purpose (see FrameDebuggerData.h's own top-of-
    // file "OWNED-string philosophy" note), and BuildEventDetailsSection()
    // (PHASE6) needs no access back into the tree at all to render its
    // own header. Always empty in practice this campaign (never
    // populated, like every other field here) - appended at the END of
    // this struct's field list, not inserted in the middle, mirroring
    // BoneViewerWindow.h's own explicit "never insert a field in the
    // middle of a struct some call site might positionally
    // aggregate-initialize" precedent.
    std::string eventLabel;
```

Then append, after `FindEventDetailsByIndex()`'s own declaration:

```cpp
// Formats a vector property's value as "(x, y, z, w)" - matches the
// reference screenshot's own "_Color  (1, 1, 1, 1)" display convention.
// Trims trailing zeros the same way std::to_string would NOT do on its
// own (e.g. "1" not "1.000000") by using a short, fixed "%g"-style
// formatting internally - see FrameDebuggerData.cpp for the exact
// formatting rule.
std::string FormatVectorProperty(const FrameDebuggerVectorProperty& vector);

// Formats a 4x4 matrix property as 4 space-joined rows of 4 numbers
// each, newline-separated - e.g. row-major
// "0.001 0 0 0\n0 0.0019 0 0\n0 0 0.00023 0.5\n0 0 0 1", matching the
// reference screenshot's own "unity_MatrixVP" grid. Caller decides how
// to lay the 4 lines out in ImGui (see BuildEventDetailsSection() below,
// which draws each row as its own ImGui::Text() call rather than one
// multi-line string, for cleaner monospace column alignment) - this
// function itself only needs to produce the 4 ready-to-split-on-'\n'
// lines of text; NEVER used as one giant multi-line ImGui::Text() call
// directly by that call site (ImGui text wrapping/alignment would look
// wrong that way).
std::string FormatMatrixProperty(const FrameDebuggerMatrixProperty& matrix);
```

### 3.2 `FrameDebuggerData.cpp` — implementations

```cpp
namespace {

// Trims a fixed-precision formatted float down to the shortest
// "does not lose the value" representation - e.g. "1.000000" -> "1",
// "0.500000" -> "0.5" - matching the reference screenshot's own compact
// "(1, 1, 1, 1)" style rather than a fixed 6-decimal dump.
std::string FormatCompactFloat(float value)
{
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%g", value);
    return std::string(buffer);
}

} // namespace

std::string FormatVectorProperty(const FrameDebuggerVectorProperty& vector)
{
    return "(" + FormatCompactFloat(vector.x) + ", " + FormatCompactFloat(vector.y) + ", "
        + FormatCompactFloat(vector.z) + ", " + FormatCompactFloat(vector.w) + ")";
}

std::string FormatMatrixProperty(const FrameDebuggerMatrixProperty& matrix)
{
    std::string result;
    for (int row = 0; row < 4; ++row) {
        if (row > 0) {
            result += "\n";
        }
        for (int col = 0; col < 4; ++col) {
            if (col > 0) {
                result += " ";
            }
            result += FormatCompactFloat(matrix.values[static_cast<std::size_t>(row * 4 + col)]);
        }
    }
    return result;
}
```

(`<cstdio>` needs adding to `FrameDebuggerData.cpp`'s includes for
`std::snprintf`, and `<cstddef>` needs adding for the explicit
`std::size_t` cast above — see Step 2's note on this codebase's own
`<cstddef>`-for-`std::size_t` convention; do not rely on it having been
pulled in transitively.)

### 3.3 `tests/Editor/FrameDebuggerDataTests.cpp` — new cases

- `FormatVectorPropertyTest`: `{1,1,1,1}` -> `"(1, 1, 1, 1)"`;
  `{0.5f, 0, 0, 0}` -> `"(0.5, 0, 0, 0)"`.
- `FormatMatrixPropertyTest`: hand-build a
  `FrameDebuggerMatrixProperty` with `values = {0.001f, 0, 0, 0, 0,
  0.0019f, 0, 0, 0, 0, 0.00023f, 0.5f, 0, 0, 0, 1}` (exactly the reference
  screenshot's own `unity_MatrixVP` numbers) and assert the formatted
  string's first line is `"0.001 0 0 0"` and its last line is
  `"0 0 0 1"` (avoid asserting the middle two rows' exact `%g` rounding
  character-for-character if that proves brittle across platforms/
  compilers — asserting the two easy/exact rows plus `std::count(result.begin(),
  result.end(), '\n') == 3` (4 rows) is sufficient and robust; this needs
  `#include <algorithm>` in the test file for `std::count`, alongside
  whatever `gtest`/`FrameDebuggerData.h` includes are already there).
- No new case is needed purely for the new `eventLabel` field itself (a
  plain `std::string` with no formatting function of its own — it
  default-constructs to `""` exactly like every other string field in
  this struct, which PHASE1's own `BuildPlaceholderFrameDebuggerSnapshotTest`
  already effectively covers for the "everything empty by default" shape).

### 3.4 `FrameDebuggerPanel.h` — new method

```cpp
private:
    ...
    void BuildEventDetailsSection(const std::optional<FrameDebuggerEventDetails>& details);
```

### 3.5 `FrameDebuggerPanel.cpp` — the event-details section

Add `BuildPropertyRow()` to the SAME anonymous namespace block PHASE4
already opened at the top of this file (the one holding `constexpr float
kSplitterWidth`), rather than opening a second, separate anonymous
namespace block — functionally identical either way, but keeps every
file-local helper grouped in one place:

```cpp
namespace {
constexpr float kSplitterWidth = 6.0f; // (already present from PHASE4)

void BuildPropertyRow(const char* label, const std::string& value)
{
    ImGui::TextUnformatted(label);
    ImGui::SameLine(150.0f);
    ImGui::TextUnformatted(value.c_str());
}

} // namespace

void FrameDebuggerPanel::BuildEventDetailsSection(const std::optional<FrameDebuggerEventDetails>& details)
{
    if (!details.has_value()) {
        // ALWAYS this branch in production this campaign - see this
        // file's own top-of-file comment and PHASE0_MASTER_STRATEGY.md's
        // Locked Design Decision #2. Not a bug; the correct final state.
        ImGui::TextDisabled("No event selected.");
        return;
    }

    // Unreachable in practice this campaign (details is always
    // std::nullopt - see FindEventDetailsByIndex()'s own doc comment in
    // FrameDebuggerData.h), but fully correct and ready for a future
    // real-capture campaign to exercise for free the moment
    // FrameDebuggerEventNode::details starts being populated for real.
    const FrameDebuggerEventDetails& d = *details;

    ImGui::Text("Event #%d: %s", d.eventIndex, d.eventLabel.c_str());
    ImGui::Separator();

    BuildPropertyRow("Shader", d.shaderName);
    BuildPropertyRow("Pass", d.passName);
    BuildPropertyRow("Blend", d.blendMode);
    BuildPropertyRow("ZClip", d.zClip);
    BuildPropertyRow("ZTest", d.zTest);
    BuildPropertyRow("ZWrite", d.zWrite);
    BuildPropertyRow("Cull", d.cull);
    BuildPropertyRow("Stencil Ref", d.stencilRef);
    BuildPropertyRow("Stencil Comp", d.stencilComp);
    BuildPropertyRow("Stencil Pass", d.stencilPass);
    BuildPropertyRow("Stencil Fail", d.stencilFail);
    BuildPropertyRow("Stencil ZFail", d.stencilZFail);

    ImGui::Spacing();

    if (ImGui::BeginTabBar("FrameDebuggerEventTabs")) {
        if (ImGui::BeginTabItem("Preview")) {
            // A real per-draw-call preview render is out of scope for
            // this campaign (see PHASE6's own Step 2) - a much larger,
            // separately-sized future feature.
            ImGui::TextDisabled("Not available yet.");
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("ShaderProperties")) {
            if (!d.textures.empty()) {
                ImGui::SeparatorText("Textures");
                for (const FrameDebuggerTextureProperty& texture : d.textures) {
                    BuildPropertyRow(texture.name.c_str(), texture.valueLabel);
                }
            }
            if (!d.vectors.empty()) {
                ImGui::SeparatorText("Vectors");
                for (const FrameDebuggerVectorProperty& vector : d.vectors) {
                    BuildPropertyRow(vector.name.c_str(), FormatVectorProperty(vector));
                }
            }
            if (!d.matrices.empty()) {
                ImGui::SeparatorText("Matrices");
                for (const FrameDebuggerMatrixProperty& matrix : d.matrices) {
                    ImGui::TextUnformatted(matrix.name.c_str());
                    const std::string formatted = FormatMatrixProperty(matrix);
                    // Split on '\n' and draw each row as its own
                    // ImGui::Text() call - see FormatMatrixProperty()'s
                    // own doc comment for why a single multi-line
                    // ImGui::Text() call is avoided here.
                    std::size_t start = 0;
                    while (start <= formatted.size()) {
                        const std::size_t newlinePos = formatted.find('\n', start);
                        const std::string rowText = formatted.substr(
                            start, newlinePos == std::string::npos ? std::string::npos : newlinePos - start);
                        ImGui::Text("    %s", rowText.c_str());
                        if (newlinePos == std::string::npos) {
                            break;
                        }
                        start = newlinePos + 1;
                    }
                }
            }
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
}
```

`<cstddef>` needs adding to `FrameDebuggerPanel.cpp`'s includes too (for
the explicit `std::size_t start`/`newlinePos` locals above), if not
already present from an earlier phase.

### 3.6 `FrameDebuggerPanel::Build()` — wire it in

Append, at the end of `BuildInspectorPane()` (PHASE5's file, right after
the `ImGui::Separator();` that already follows the texture-preview
child region):

```cpp
    const std::optional<FrameDebuggerEventDetails> details
        = FindEventDetailsByIndex(snapshot, m_selectedEventIndex);
    BuildEventDetailsSection(details);
```

(`BuildInspectorPane()`'s own signature already takes `const
FrameDebuggerSnapshot& snapshot` from PHASE5 — no signature change
needed, just this new tail call.)

### 3.7 Compile check

`cmake --build build --target GreatTamanaEngineTests` first (fast,
covers the two new pure formatting functions' own tests), confirm
`--gtest_filter=*FrameDebuggerData*` passes in full (including PHASE1's
original cases), THEN `cmake --build build --target GreatTamanaEngine`
for the panel-side change, then a live smoke test confirming (with
"Enable" checked): the inspector pane's bottom now reads "No event
selected." underneath the PHASE5 texture-preview box.

### 3.8 File-change inventory (this phase only)

Modified only: `src/Editor/FrameDebuggerData.h`,
`src/Editor/FrameDebuggerData.cpp`,
`tests/Editor/FrameDebuggerDataTests.cpp`,
`src/Editor/Panels/FrameDebuggerPanel.h`,
`src/Editor/Panels/FrameDebuggerPanel.cpp`.

Write `PHASE6_COMPLETION_REPORT.md` into `task_manager/frame-debugger-2/`
when done, then `git_add`/`git_commit`.
