# PHASE4 — Editor "Log" Panel UI

Parent: `PHASE0_MASTER_STRATEGY.md`. Depends on: `PHASE1_CORE_LOGGER_MODULE.md`
(this phase does not depend on `PHASE2`/`PHASE3`'s own changes, but read
their completion reports anyway for any API drift noted there).

**Use `ask_questions` for any genuine ambiguity this document doesn't already
resolve.** If this phase itself delegates any further sub-task, that
delegation prompt must repeat this same instruction.

---

## Step 1: The Goal (Where are we going?)

Give the Editor a "Log" panel, docked alongside "Memory"/"Profiler"/
"Render Graph"/"Jobs"/"Atmosphere": a scrollable, per-level-colored list of
every currently retained log entry, a filter row (per-level toggle
checkboxes, a category text filter, a keyword text filter), an entry-count
label, an auto-scroll toggle, and a Clear button.

## Step 2: The Situation (Where are we now?)

- `src/Editor/Panels/JobsPanel.h`/`.cpp` is the closest existing precedent:
  a small, **stateful** (not the more common stateless-free-function shape
  used by e.g. `MemoryPanel.h`), non-polymorphic class with a single
  `void Build(EditorContext& ctx, ...)` method, called by name (no
  `IEditorPanel` interface) from `Editor/ImGuiEditorLayer.cpp`'s fixed
  per-frame panel sequence. The Log panel needs its OWN cross-frame state
  (which levels are toggled on, the current category/keyword filter text,
  the auto-scroll flag) — copy `JobsPanel`'s shape, not `MemoryPanel`'s.
- The existing pure-data/pure-formatting sibling for a stateful panel is
  `src/Editor/JobsPanelData.h`/`.cpp` (and, for a stateless panel,
  `src/Editor/ProfilerPanelData.h`/`.cpp`/`src/Editor/MemoryPanelData.h`/
  `.cpp`) — every panel's actual ImGui-drawing `.cpp` under `Panels/` stays
  a "thin wrapper" around a same-named `*Data.h`/`.cpp` pair that is itself
  completely ImGui-free and Tier-1-tested (see `AGENTS.md`,
  "Testability & Regression Safety").
- `src/Editor/EditorPanelCatalog.h`'s `kKnownEditorPanelNames` array
  currently ends with:
  ```cpp
  inline constexpr const char* kKnownEditorPanelNames[] = {
      "Hierarchy", "Inspector", "Scene", "Game", "Memory", "Profiler",
      "Render Graph", "Jobs", "Atmosphere",
  #if GTE_ENABLE_PROJECT_PANEL
      "Project",
  #endif
  };
  ```
  Add `"Log"` as one more unconditional entry (it has no
  `GTE_ENABLE_PROJECT_PANEL`-style dependency of its own — it depends only
  on `GTE_ENABLE_EDITOR`, which this whole file already implicitly assumes
  is ON, same as every other entry here besides the conditional `"Project"`
  one) — append it right after `"Atmosphere"`, before the
  `#if GTE_ENABLE_PROJECT_PANEL` block.
- `src/Editor/DockLayout.cpp`'s `BuildDefaultDockLayout()` currently ends
  its unconditional `bottom`-docked block with:
  ```cpp
  ImGui::DockBuilderDockWindow("Jobs", bottom);
  // "Atmosphere" (...)
  ImGui::DockBuilderDockWindow("Atmosphere", bottom);
  #if GTE_ENABLE_PROJECT_PANEL
      ImGui::DockBuilderDockWindow("Project", bottom);
  #endif
  ```
  Add `ImGui::DockBuilderDockWindow("Log", bottom);` right after the
  `"Atmosphere"` line (before the `#if GTE_ENABLE_PROJECT_PANEL` block),
  with a short doc comment mirroring the existing ones' style ("same
  bottom-strip treatment as Memory/Profiler/Render Graph/Jobs/Atmosphere").
- `src/Editor/ImGuiEditorLayer.cpp`'s `BuildUI()` currently calls (in this
  order, around its existing panel-building block):
  ```cpp
  ...
  m_jobsPanel.Build(m_ctx, game);
  ...
  m_frameDebuggerPanel.Build(m_ctx, renderer, renderGraph, m_gameView, m_gameViewComposited);
  #if GTE_ENABLE_PROJECT_PANEL
      m_projectPanel.Build(m_ctx);
      ...
  #endif
  ```
  Add `m_logPanel.Build(m_ctx);` right after `m_jobsPanel.Build(m_ctx,
  game);` (any consistent placement in this sequence is fine — panels are
  independent of each other and of call order).
- `src/Editor/ImGuiEditorLayer.cpp`'s matching class (find the class that
  owns `m_jobsPanel`, `m_profilerPanel`, etc., as plain members) needs a new
  `LogPanel m_logPanel;` member alongside them.
- Because `GET /activate_tab`/`GET /list_tabs` (already shipped, Networking
  section) read `kKnownEditorPanelNames` generically, adding `"Log"` there
  automatically makes `GET /activate_tab?name=Log` work with **zero**
  additional network-layer code — do not add anything Log-panel-specific
  to `Network/`, `EditorLayer.h`, or `NullEditorLayer.cpp` in this phase.
- **Important, and deliberately simplifying, observation**: `Logger` (Phase
  1) has no independent enable/disable switch of its own beyond
  `GTE_ENABLE_EDITOR` — and this panel only exists at all when
  `GTE_ENABLE_EDITOR` is ON. Therefore `Logger::IsEnabled()` is **always
  `true`** everywhere this panel's own code runs — unlike, say, the
  Profiler panel's `kCpuScopeInstrumentationCompiledIn` (which reflects a
  SEPARATE, independently-toggleable switch, `GTE_ENABLE_PROFILER`, that
  really can be OFF while `GTE_ENABLE_EDITOR` is ON). **Do not** add a
  "logging disabled in this build" empty-state message to this panel — that
  state is unreachable here; just show "No log entries yet." when the
  buffer is empty.

## Step 3: The Plan

### 3.1 `src/Editor/LogPanelData.h`/`.cpp` (pure, ImGui-free, Tier 1)

```cpp
#pragma once

#include "Logger.h"

#include <string>
#include <vector>

namespace gte {

// The "Log" panel's own cross-frame filter/UI state - a plain data struct
// so it can be unit-tested independently of ImGui (mirrors
// JobsPanel.h's own m_paused/m_frozenPoints style, just factored out as
// its own named type here since there's more of it).
struct LogPanelFilterState {
    bool showDebug = true;
    bool showInfo = true;
    bool showWarning = true;
    bool showError = true;
    std::string categoryFilter; // Empty = any. Exact, case-sensitive match (mirrors Logger's own convention).
    std::string keywordFilter;  // Empty = any. Case-insensitive substring match on the message.
    bool autoScroll = true;
};

// Translates `state`'s category/keyword text filters into a
// Logger::LogQueryFilter's matching fields (leaving `hasMinLevel`
// deliberately false/unset - see FilterByEnabledLevels() below for why
// level filtering is NOT expressed as a single Logger-side min-level
// threshold here) plus `sinceId` (0 to fetch the whole current buffer, or
// a caller-supplied cursor for an incremental re-query).
LogQueryFilter BuildLogQueryFilter(const LogPanelFilterState& state, std::uint64_t sinceId);

// Logger::Query() only supports a single ordinal "minimum level"
// threshold - but this panel's UI is FOUR INDEPENDENT per-level
// checkboxes (e.g. "show Debug and Error but not Info/Warning" is a
// valid, arbitrary combination no single threshold can express). This
// function applies that arbitrary subset AFTER Logger::Query() has
// already applied every other filter - it is the ONE place this
// distinction is resolved, so LogPanel.cpp itself never needs to touch
// LogEntry::level directly.
std::vector<LogEntry> FilterByEnabledLevels(const std::vector<LogEntry>& entries, const LogPanelFilterState& state);

// Plain RGBA (0..1 float) - LogPanel.cpp is the only place this gets
// turned into an ImVec4, keeping this function itself ImGui-free (same
// rule ProfilerPanelData.h's own functions already follow).
struct LevelColor { float r; float g; float b; float a; };
LevelColor ColorForLevel(LogLevel level) noexcept;

// "[12.345s][Frame 42][Warning][Renderer] message text" - the exact,
// single, shared line format both LogPanel.cpp's rendering AND this
// file's own test use, so they can never silently drift apart (mirrors
// ProfilerPanelData.h's FormatDuration()/FormatFrameTimeSummary()
// precedent).
std::string FormatLogEntryLine(const LogEntry& entry);

} // namespace gte
```

`BuildLogQueryFilter`/`FilterByEnabledLevels`/`ColorForLevel`/
`FormatLogEntryLine` are plain, free functions — no ImGui, no live
`Logger::` calls inside `LogPanelData.cpp` itself (the panel's own
`.cpp`, not this one, is what actually calls `Logger::Query()`/
`Logger::Clear()`), matching every existing `*PanelData.h` file's own
"Tier 1 despite living under `src/Editor/`" convention.

### 3.2 `src/Editor/Panels/LogPanel.h`/`.cpp`

```cpp
#pragma once

#include "../LogPanelData.h"

#include <cstdint>

namespace gte {

struct EditorContext;

// The "Log" panel (logger-1 campaign) - a Unity Console-style scrolling,
// filterable, colored list of every entry Editor/Logger.h currently
// retains. A small, stateful, non-polymorphic class (mirrors JobsPanel.h
// exactly), called by name from ImGuiEditorLayer::BuildUI() - no
// IEditorPanel interface introduced (see AGENTS.md, "Editor Module
// Structure").
class LogPanel {
public:
    void Build(EditorContext& ctx);

private:
    LogPanelFilterState m_filterState;
};

} // namespace gte
```

`Build(ctx)`:

1. `ImGui::Begin("Log")` (`ImGui::End()` at the bottom, unconditionally —
   mirror every other panel's exact `Begin`/`End` shape, including how they
   handle a collapsed/closed window if relevant — check `JobsPanel.cpp`'s
   own `Build()` for the precise boilerplate to copy).
2. Filter row: four `ImGui::Checkbox("Debug", &m_filterState.showDebug)`-
   style toggles (same line, via `ImGui::SameLine()`) for the four levels;
   a category text filter and a keyword text filter; an
   `ImGui::Checkbox("Auto-scroll", &m_filterState.autoScroll)`.
   **Important — this codebase does NOT vendor `misc/cpp/imgui_stdlib.h`**
   (confirmed: no `imgui_stdlib` file anywhere under `third_party/imgui/`,
   and Dear ImGui's own vendored `imgui.h`/`imgui_widgets.cpp` both carry the
   upstream comment "If you want to use InputText() with std::string... use
   the wrapper in misc/cpp/imgui_stdlib.h/.cpp!" as a reminder that plain
   `ImGui::InputText()` does NOT accept a `std::string&` directly), so
   `ImGui::InputText("Category", &m_filterState.categoryFilter)` will NOT
   compile. Mirror this codebase's own ALREADY-ESTABLISHED, ONLY existing
   `InputText`-bound-to-`std::string` precedent instead —
   `Panels/InspectorPanel.cpp`'s entity-name field (its "Name" field, inside
   `BuildEntityInspector()`): a local, fixed-size `char` buffer, refilled
   from the `std::string` field every frame via `std::snprintf` BEFORE the
   `ImGui::InputText()` call, then copied back into the `std::string` field
   only when `ImGui::InputText()` returns `true` (a real edit happened this
   frame). Concretely, for each of the two text filters (a 128-byte buffer
   is generously large for a filter string):
   ```cpp
   char categoryBuffer[128];
   std::snprintf(categoryBuffer, sizeof(categoryBuffer), "%s", m_filterState.categoryFilter.c_str());
   if (ImGui::InputText("Category", categoryBuffer, sizeof(categoryBuffer))) {
       m_filterState.categoryFilter = categoryBuffer;
   }
   ImGui::SameLine();
   char keywordBuffer[128];
   std::snprintf(keywordBuffer, sizeof(keywordBuffer), "%s", m_filterState.keywordFilter.c_str());
   if (ImGui::InputText("Keyword", keywordBuffer, sizeof(keywordBuffer))) {
       m_filterState.keywordFilter = keywordBuffer;
   }
   ```
   (`LogPanel.cpp` needs `#include <cstdio>` for `std::snprintf` — every
   other field in `LogPanelFilterState` stays a plain `std::string`; this
   fixed-buffer bridging is purely local to `LogPanel.cpp`'s own `Build()`,
   never leaking into `LogPanelData.h`'s own ImGui-free types.)
3. A "Clear" button: `if (ImGui::Button("Clear")) { Logger::Clear(); }` —
   called DIRECTLY, no bridge, same justification as the network route in
   `PHASE3` (this runs on the main thread anyway, exactly like every other
   Editor panel's own direct engine calls, e.g. `ProjectPanel`'s direct
   filesystem calls).
4. Query + filter: `const LogQueryFilter filter = BuildLogQueryFilter(m_filterState, /*sinceId=*/0);`
   (this panel always re-queries the FULL current buffer every frame — at
   only up to 2000 entries this is cheap; there is no need for this panel
   itself to use an incremental `since_id` cursor the way an external HTTP
   poller does) `const std::vector<LogEntry> matched =
   FilterByEnabledLevels(Logger::Query(filter), m_filterState);`.
5. An entry-count label, e.g.
   `ImGui::Text("%zu entries", matched.size());`.
6. A scrolling child region (`ImGui::BeginChild("LogScrollRegion", ...,
   true)` / `ImGui::EndChild()`) rendering one `ImGui::TextColored(color,
   "%s", FormatLogEntryLine(entry).c_str());` per matched entry (`color`
   built from `ColorForLevel(entry.level)` via a local
   `ImVec4(c.r, c.g, c.b, c.a)`), and, when `m_filterState.autoScroll` is
   true and the scroll position was already at the bottom before this
   frame's new content, `ImGui::SetScrollHereY(1.0f)` after the last line
   (mirror any existing engine console-style auto-scroll idiom if one
   already exists anywhere in this codebase's ImGui code — if not, this is
   the same standard Dear ImGui idiom used in its own `imgui_demo.cpp`
   "Console" example: track `ImGui::GetScrollY() >= ImGui::GetScrollMaxY()`
   BEFORE adding new content).
7. When `matched.empty()`, show `ImGui::TextDisabled("No log entries yet.");`
   instead of an empty scroll region.

### 3.3 Catalog / dock-layout / composition wiring

- `EditorPanelCatalog.h`: add `"Log",` to `kKnownEditorPanelNames` (see
  Step 2 for the exact insertion point).
- `DockLayout.cpp`: add `ImGui::DockBuilderDockWindow("Log", bottom);` (see
  Step 2 for the exact insertion point and comment style to mirror).
- `ImGuiEditorLayer.h` (or wherever the class members for `m_jobsPanel`
  etc. are actually declared — confirm the exact header, since some panels'
  state might be declared directly in `ImGuiEditorLayer.cpp`'s own class
  body rather than a separate header): add `LogPanel m_logPanel;`
  (`#include "Panels/LogPanel.h"` alongside the existing panel includes).
- `ImGuiEditorLayer.cpp`: add `m_logPanel.Build(m_ctx);` in `BuildUI()`
  (see Step 2 for the exact placement).

### 3.4 CMake wiring

- Root `CMakeLists.txt`'s `if(GTE_ENABLE_EDITOR)` block: add
  `src/Editor/LogPanelData.h`/`src/Editor/LogPanelData.cpp` (alongside the
  existing `src/Editor/JobsPanelData.h`/`.cpp` lines) and
  `src/Editor/Panels/LogPanel.h`/`src/Editor/Panels/LogPanel.cpp`
  (alongside the existing `src/Editor/Panels/JobsPanel.h`/`.cpp` lines).
- `tests/CMakeLists.txt`'s `if(GTE_ENABLE_EDITOR)` block: add
  `Editor/LogPanelDataTests.cpp` (alongside the existing
  `Editor/JobsPanelDataTests.cpp` line).

### 3.5 `tests/Editor/LogPanelDataTests.cpp`

Pure Tier-1 tests, no ImGui/live Logger state needed beyond hand-built
`LogEntry`/`LogPanelFilterState` values:

- `BuildLogQueryFilter`: category/keyword text flows through into the
  resulting `LogQueryFilter` unchanged; `hasMinLevel` is always `false`
  (confirming level filtering is NOT delegated to `Logger::Query()` here);
  the `sinceId` parameter passed in is reflected verbatim.
- `FilterByEnabledLevels`: given a mixed-level vector of entries and a
  filter state with an ARBITRARY, non-threshold subset of levels enabled
  (e.g. `showDebug=true, showInfo=false, showWarning=false,
  showError=true`), returns exactly the Debug and Error entries, in their
  original relative order, with Info/Warning entries excluded — this is
  the one test that most directly proves the "arbitrary subset, not an
  ordinal threshold" property this function exists for.
- `ColorForLevel`: every one of the four levels returns a distinct color
  (no two levels sharing the exact same RGBA).
- `FormatLogEntryLine`: exact string-shape assertion for at least one
  known input (pin the format so a future accidental reformat is caught by
  this test).

## Definition of Done for this phase

- Fast, targeted incremental compile check (default `GTE_ENABLE_EDITOR=ON`
  configuration); run just this phase's new tests
  (`--gtest_filter=*LogPanelData*`).
- Optional but encouraged, since the tool is available: `run_app_background`
  the built engine, `gte_send_request` a screenshot of the Editor (e.g.
  `/get_swapchain` or `/get_game_view`) to visually confirm the "Log" tab
  is present/docked correctly and shows real entries from `PHASE2`'s own
  startup log lines, then `stop_app_background`.
- Write `PHASE4_COMPLETION_REPORT.md` next to this file.
- `git add`/`git commit` (this phase's own changes + the report) with a
  clear message.
