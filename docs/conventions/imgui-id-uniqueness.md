# ImGui Widget ID Uniqueness

_Part of [GreatTamanaEngine](../../AGENTS.md)'s contributor conventions. See
[docs/README.md](../README.md) for the full documentation index._

## The problem this exists to prevent

A real, live bug shipped from trusting application data for Dear ImGui widget
ID uniqueness. Full forensic root-cause writeup:
`C:\Users\F5954\Documents\TAMANA\GreatTamanaEngin-Ideas\bugs\2026-09-25_RenderGraphPanel_Duplicate_Checkbox_ID_Highlight_Bug.txt`.
In short: `src/Editor/Panels/RenderGraphPanel.cpp`'s `BuildPassRow()` used to
build a checkbox's ImGui ID purely from `"##Enabled_" + pass.name`. The
"Offscreen Regime (Game View + Scene View)" table intentionally lists Game
View's and Scene View's render-graph passes together, in one flat list, in
one frame — and two Atmosphere passes are registered under a hard-coded
literal name that is never made unique per view (a permanent, intentional
design choice — see below), so the SAME name appeared twice in that one
table, twice in one frame, and both rows' checkboxes ended up sharing the
literal same ImGui ID. Dear ImGui's own built-in "ID Conflict" debug safety
net (`io.ConfigDebugHighlightIdConflicts`, default `true`,
`third_party/imgui/imgui.cpp`) then drew a red rectangle around both rows and
showed a "Programmer error: N visible items with conflicting ID!" tooltip the
instant either row was hovered — which looked exactly like a rendering bug,
and cost real investigation time to correctly diagnose.

The fix campaign that produced everything on this page is
`task_manager/editor-core-separation-10/PHASE0_MASTER_STRATEGY.md` (four
phases: detection foundation, the helper + the exact reported bug fixed,
an Editor-wide retrofit sweep, then this documentation + a full regression
pass). **The root cause — the two Atmosphere passes sharing one hard-coded
name — was deliberately, permanently left untouched by that campaign**; see
"What this does NOT fix" below.

## The two-layer system

### Layer 1 (structural prevention): `gte::ScopedUniqueId`

`src/Editor/ImGuiUniqueId.h`/`.cpp` — the ONE mandated way to enter a
per-iteration Dear ImGui ID scope for any widget built inside a loop over
runtime data. Construct it with the loop's own distinct iteration index —
never a data-derived string alone — plus an optional human-readable debug
key, and it manages `ImGui::PushID()`/`ImGui::PopID()` for you (an
index-based push for real uniqueness, then an optional cosmetic nested
push of the debug key purely for legibility under Dear ImGui's own Item
Picker and this system's own conflict-log messages).

```cpp
// GOOD - index is always distinct across the loop's own iterations, in the
// SAME frame, regardless of what any data string contains.
for (std::size_t i = 0; i < items.size(); ++i) {
    gte::ScopedUniqueId idScope(static_cast<int>(i), "MyPanel::BuildRow", items[i].name.c_str());
    ImGui::Checkbox("##Enabled", &items[i].enabled);
}

// BAD - never do this. "This name is always unique" is NEVER a safe
// assumption to build widget-ID uniqueness on top of - two rows with the
// same items[i].name silently collide, and Dear ImGui's own red-highlight
// safety net is the ONLY thing that will ever tell you (if you're lucky
// enough to hover the right widget).
ImGui::PushID(items[i].name.c_str());
ImGui::Checkbox(("##Enabled_" + items[i].name).c_str(), &items[i].enabled);
ImGui::PopID();
```

`ScopedUniqueId` is non-copyable and non-movable (it is a scope guard, not a
value type) and its destructor pops in the correct reverse order regardless
of how many pushes its constructor performed.

### Layer 2 (proactive detection, a safety net for future code that bypasses Layer 1)

`src/Editor/ImGuiIdConflictTracker.h`/`.cpp` is a small, standalone,
Tier-1-tested class (`tests/Editor/ImGuiIdConflictTrackerTests.cpp`) with
ZERO dependency on `<imgui.h>` or `Logger` — it operates purely on plain
`std::uint32_t` values a caller already resolved (`Reset()`,
`RegisterAndCheckConflict(std::uint32_t)`, `DistinctIdCount()`).

`src/Editor/ImGuiIdConflictGuard.h`/`.cpp` is the real, Editor-owned,
process-global, main-thread-only singleton built on top of it (mirroring
`LoggerLogSink::Instance()`'s exact shape) — deliberately untested by any
automated test, exactly like every other ImGui/Vulkan-owning class in this
codebase (the same pure-vs-owning split `AGENTS.md` already documents for
`GpuMemoryTracker` vs every Vulkan-owning class):

- `BeginFrame()` — called once per real UI frame from
  `ImGuiEditorLayer::NewFrame()`, immediately after `ImGui::NewFrame()` and
  before any panel builds a single widget. Resets the per-frame tracker;
  does NOT reset which conflicts are considered "ongoing" (see below).
- `CheckCurrentIdScope(const char* debugContext, const char* debugKey)` —
  called by `ScopedUniqueId`'s own constructor, exactly once per ID scope
  entered, immediately after the relevant `ImGui::PushID()` call(s).
  Resolves the CURRENT real ImGui ID stack position via `ImGui::GetID("")`
  and checks it against this frame's tracker.

A genuine conflict is logged via `GTE_LOG_ERROR("ImGuiIdConflict", ...)` —
visible in the Editor's "Log" panel and via
`GET /get_logs?category=ImGuiIdConflict` — but **only the first time a given
real ImGui ID starts conflicting**. If the exact same conflict is still
happening next frame too, it stays silent (no ~60-lines-per-second spam)
until the conflict actually clears for at least one frame and then
reappears — a genuinely NEW incident, worth a fresh log line. This is
tracked via a persistent `m_ongoingConflicts` set that survives across
frames, separate from the per-frame tracker `BeginFrame()` resets.

This detection layer is a logged warning only — it never crashes, asserts,
or throws. An autonomous AI-driven session must be able to keep running and
keep working even if a conflict is (incorrectly) detected somewhere.

### Layer 3 (unchanged, still a final backstop): Dear ImGui's own `io.ConfigDebugHighlightIdConflicts`

Left at its Dear ImGui default (`true`), untouched, by every phase of the
campaign that built the two layers above. This system is an ADDITIONAL,
earlier, logged safety net — never a replacement for Dear ImGui's own
built-in visual one. If a real conflict somehow still reaches Dear ImGui's
own detector after this system (should be structurally impossible for
anything routed through `ScopedUniqueId`, but never impossible for code
that isn't), the red highlight staying available is a deliberate, permanent
third line of defense.

## The mandatory rule for all future code

Any ImGui widget built inside a loop over runtime data MUST enter its ID
scope via `gte::ScopedUniqueId`, keyed by the loop's own distinct iteration
index. Do not assume any name/label/path/data string is unique enough on
its own to build a widget ID out of, no matter how unlikely a real-world
collision seems — see the "GOOD" vs "BAD" snippet above.

## What this does NOT fix (permanent, by design)

The two Atmosphere passes (`"AtmosphereSkyViewLutPass"`,
`"AtmosphereAerialPerspectiveVolumePass"`, both declared in
`src/Renderer/Atmosphere/AtmosphereLutRenderer.cpp`) keep their exact
current hard-coded names, forever. The "Offscreen Regime" table legitimately
showing two rows with an identical `pass.name` — and toggling one of those
rows' checkboxes deliberately flipping BOTH rows' shared enabled/disabled
state at once — is a permanent, intentional fact about this engine, both
before and after the campaign that built this page. Nothing in
`AtmosphereLutRenderer.cpp`, `AtmospherePassSequence.cpp`, `Core.cpp`'s
Atmosphere provider, or `RenderPassToggleRegistry`'s keying scheme was ever
touched by that campaign. The fix lives entirely at the ImGui-ID-scope
layer — deriving uniqueness from loop position, never from data content —
never at the data layer.

## A permanent record of every call site the campaign touched

Every existing `ImGui::PushID(...)` call site under `src/Editor/`, as of
this campaign, was migrated onto `ScopedUniqueId`. Use this as a checklist
if auditing "did this ever get applied consistently?" in the future — the
only real (non-comment) `ImGui::PushID`/`ImGui::PopID` calls anywhere under
`src/Editor/` today live inside `ImGuiUniqueId.cpp` itself (`ScopedUniqueId`'s
own implementation, which legitimately calls the raw functions so every
other file doesn't have to):

| File | Function(s) | Notes |
|---|---|---|
| `Panels/RenderGraphPanel.cpp` | `BuildPassRow()`/`BuildPassTable()`, `BuildDisabledBuiltInPassesSection()`, `BuildPluginRenderFeaturesSection()` | The exact reported bug's own file — fixed first, PHASE2 |
| `Panels/ProjectPanel.cpp` | `RenderLeftPaneFolder()`, `RenderBreadcrumb()`, `RenderRightPaneEntry()` | PHASE3 |
| `Panels/HierarchyPanel.cpp` | `RenderEntityNode()` | PHASE3 (was already index-based; migrated onto `ScopedUniqueId` for consistency and Layer 2 coverage) |
| `Panels/InspectorPanel.cpp` | Verlet chain/joint nested loops | PHASE3 (two distinctly-named nested scopes — `chainIdScope`/`jointIdScope`) |
| `BoneViewerWindow.cpp` | `RenderBoneTreeNode()`, `RenderFlatPartRow()`, `RenderVerletChainNode()` | PHASE3 |

New files this campaign added: `src/Editor/ImGuiIdConflictTracker.h/.cpp`,
`src/Editor/ImGuiIdConflictGuard.h/.cpp`, `src/Editor/ImGuiUniqueId.h/.cpp`,
`tests/Editor/ImGuiIdConflictTrackerTests.cpp`.

Full campaign writeup: `task_manager/editor-core-separation-10/PHASE0_MASTER_STRATEGY.md`,
each `PHASEn_COMPLETION_REPORT.md` in that same folder, and
`CAMPAIGN_COMPLETION_REPORT.md`.
