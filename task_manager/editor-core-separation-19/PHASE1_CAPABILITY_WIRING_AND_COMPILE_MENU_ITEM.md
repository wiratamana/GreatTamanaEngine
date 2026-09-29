# PHASE1 — Capability Wiring + Real "Compile" Menu Item

Parent: `PHASE0_MASTER_STRATEGY.md` (read it first — Section "New, real gap
this Phase0 found" explains WHY this phase is bigger than the source `.txt`
file assumed).

## Step 1: The Goal

Every existing "Project" menu item (`New Project...`, `Open Project...`)
already does real work. After this phase, `Compile` does too — enabled/
disabled correctly, labeled with the active project's real name, triggering
the exact same `TriggerCompileOnly()` the HTTP route already calls, with a
status toast and an optional "(compiling...)" label suffix. Zero behavior
change to any EXISTING menu item, route, or capability method.

## Step 2: The Situation

See `PHASE0_MASTER_STRATEGY.md`, Step 2, in full — every citation there
(exact file, exact line numbers) is the ground truth this phase edits
against. The short version: the compile MECHANISM (`TriggerProjectAssemblyCompile`/
`TriggerCompileOnly`/the HTTP route) is 100% real and untouched by this
phase; what's missing is (1) a public "is a build in flight for this
project" query, (2) a THIRD `IEditorLayer` capability setter so
`ImGuiEditorLayer` can even SEE an `IHotReloadDebugCapability*` inside
`DockLayout.cpp`'s menu-bar code, and (3) the actual menu item body itself.

## Step 3: The Plan (exact edits, in dependency order)

### 3.1 — `src/Core/Plugins/ProjectAssemblyBuildRunner.h`

Add, immediately after `TriggerProjectAssemblyCompile()`'s own declaration
(currently ending at line 125):

```cpp
// editor-core-separation-19 campaign (On-Engine Project Workflow plan,
// BIG-STEP 5), PHASE1. Read-only query over the SAME g_inFlightProjects set
// TryMarkInFlight()/ClearInFlight() (ProjectAssemblyBuildRunner.cpp's own
// anonymous namespace) already guard TriggerProjectAssemblyCompile()/
// TryRunProjectAssemblyBuildSynchronously() with - never mutates anything,
// safe to call from ANY thread (guarded by the same g_inFlightMutex).
// Lets a UI surface (the "Project > Compile" menu item, DockLayout.cpp) show
// a "(compiling...)" hint without a new, separate, independently-drifting
// tracking mechanism of its own.
bool IsProjectAssemblyBuildInFlight(const std::string& projectName);
```

### 3.2 — `src/Core/Plugins/ProjectAssemblyBuildRunner.cpp`

Add the implementation as a new, ORDINARY (non-anonymous-namespace)
function, placed anywhere below the anonymous-namespace block that ends at
line 49 (`ClearInFlight()`'s closing brace) — placing it near
`TriggerProjectAssemblyCompile()`'s own definition, so it sits next to its
sibling in the same logical section, is the cleanest spot but not load-
bearing; what IS load-bearing is that it stays OUTSIDE the anonymous
namespace (so it is callable from other translation units) yet in the SAME
`.cpp` file (so `g_inFlightMutex`/`g_inFlightProjects` are directly visible
with no extra include/forward declaration):

```cpp
bool IsProjectAssemblyBuildInFlight(const std::string& projectName)
{
    std::lock_guard<std::mutex> lock(g_inFlightMutex);
    return g_inFlightProjects.count(projectName) != 0;
}
```

### 3.3 — `src/Core/EditorCapabilities.h`

Inside `IHotReloadDebugCapability` (lines 173-256), add a new pure virtual
immediately after `TriggerCompileOnly()`'s own declaration (currently ending
at line 228, right before `TriggerHotReload()`):

```cpp
    // editor-core-separation-19 campaign (On-Engine Project Workflow plan,
    // BIG-STEP 5), PHASE1. Thin wrapper over
    // Core/Plugins/ProjectAssemblyBuildRunner.h's own
    // IsProjectAssemblyBuildInFlight() - lets a UI surface show a
    // "(compiling...)" hint for the CURRENTLY ACTIVE project without
    // depending on ProjectAssemblyBuildRunner.h directly (mirrors this
    // whole interface's existing "gte_core-tier code never needs to see
    // the real implementation header" contract).
    virtual bool IsCompileInFlight(const std::string& projectName) const = 0;
```

### 3.4 — `src/Editor/EditorHotReloadDebugCapability.h`

Add, immediately after `bool TriggerCompileOnly(const std::string& projectName) override;`
(line 36):

```cpp
    bool IsCompileInFlight(const std::string& projectName) const override;
```

### 3.5 — `src/Editor/EditorHotReloadDebugCapability.cpp`

Add, immediately after `TriggerCompileOnly()`'s own closing brace (currently
ending at line 108). `#include "../Core/Plugins/ProjectAssemblyBuildRunner.h"`
is already present in this `.cpp` (confirmed, line 5 — `TriggerCompileOnly()`
itself already calls `TriggerProjectAssemblyCompile()`/`ResolveCMakeBuildDirectory()`
from that same header) — do not add a duplicate include:

```cpp
bool EditorHotReloadDebugCapability::IsCompileInFlight(const std::string& projectName) const
{
    return IsProjectAssemblyBuildInFlight(projectName);
}
```

### 3.6 — `src/Editor/EditorLayer.h` (TWO edits, in this exact order — do not skip edit 1)

**Edit 1 — add a brand-new forward declaration first.** Confirmed, by direct
inspection (`search_in_dir` for `IHotReloadDebugCapability` across this
whole header), that this type is NOT forward-declared, and NOT `#include`d,
anywhere in `EditorLayer.h` today — unlike `IProjectLifecycleCapability`
(line 88) and `IAssetScaffoldingCapability` (line 94), which already are.
**This is a real, load-bearing gap, not a "should already be true" detail —
skipping this edit means Edit 2 below names an undeclared type and the
build fails to compile.** Add, immediately after `class IAssetScaffoldingCapability;`
(currently line 94):

```cpp
// editor-core-separation-19 campaign (On-Engine Project Workflow plan,
// BIG-STEP 5), PHASE1 - forward-declared only, mirrors
// "class IAssetScaffoldingCapability;" immediately above: this header only
// ever stores/passes a POINTER to it, never dereferences one itself.
class IHotReloadDebugCapability;
```

**Edit 2 — add the new pure virtual setter**, immediately after
`SetAssetScaffoldingCapability()` (currently ending at line 738), mirroring
that method's own doc-comment style exactly:

```cpp
    // editor-core-separation-19 campaign (On-Engine Project Workflow plan,
    // BIG-STEP 5), PHASE1 - hands the real ImGui implementation a live
    // IHotReloadDebugCapability* (Core/EditorCapabilities.h) so
    // DockLayout.cpp's own "Project > Compile" menu item can call
    // TriggerCompileOnly()/IsCompileInFlight() directly - the exact SAME
    // methods POST /project_assembly/debug/compile_only already calls
    // (LDD-PW5's "one function, two callers" rule, mirroring
    // SetProjectLifecycleCapability's own precedent above). Called exactly
    // ONCE, from EditorHost's own constructor body. Always a safe no-op for
    // NullEditorLayer (a release build has no menu bar to wire this into at
    // all).
    virtual void SetHotReloadDebugCapability(IHotReloadDebugCapability* capability) = 0;
```

A bare forward declaration is genuinely enough here — both this header and
`NullEditorLayer.cpp`/`ImGuiEditorLayer.cpp`'s own override bodies only ever
store or pass the POINTER, never dereference it (the real call —
`hotReloadDebugCapability->TriggerCompileOnly(...)`/`->IsCompileInFlight(...)`
— happens only in `DockLayout.cpp`, which gets the FULL type via its own new
`#include "../Core/EditorCapabilities.h"`, Section 3.11 below).

### 3.7 — `src/Editor/NullEditorLayer.cpp`

Add, immediately after `SetAssetScaffoldingCapability()`'s own no-op
override (line 107):

```cpp
    void SetHotReloadDebugCapability(IHotReloadDebugCapability* /*capability*/) override { }
```

(No new include needed here — `NullEditorLayer.cpp` only `#include`s
`EditorLayer.h`, and Section 3.6's Edit 1 makes the forward-declared type
visible through that single include, exactly like every other capability
pointer type this file already stores.)

### 3.8 — `src/Editor/ImGuiEditorLayer.cpp`

Three edits:

1. Member variable — add, immediately after `m_assetScaffoldingCapability`
   (currently ending at line 1212):
   ```cpp
    // editor-core-separation-19 campaign (On-Engine Project Workflow plan,
    // BIG-STEP 5), PHASE1 - non-owning, see
    // IEditorLayer::SetHotReloadDebugCapability()'s own doc comment for the
    // lifetime contract (EditorHost's own s_editorHotReloadDebugCapability
    // static outlives this object). Read directly by
    // DockLayout.cpp's BuildDockspaceAndMenuBar() call below - unlike
    // m_projectLifecycleCapability/m_assetScaffoldingCapability, this one
    // is NOT handed to a separate floating-window Build() call, since the
    // "Compile" menu item lives INSIDE the menu bar itself, not a later,
    // separate window.
    IHotReloadDebugCapability* m_hotReloadDebugCapability = nullptr;
   ```
2. Override method — add, immediately after the `SetAssetScaffoldingCapability()`
   override body (lines 964-967):
   ```cpp
    void SetHotReloadDebugCapability(IHotReloadDebugCapability* capability) override
    {
        m_hotReloadDebugCapability = capability;
    }
   ```
3. Call-site update — change line 529 from:
   ```cpp
   BuildDockspaceAndMenuBar(m_ctx, game, renderer);
   ```
   to:
   ```cpp
   BuildDockspaceAndMenuBar(m_ctx, game, renderer, m_hotReloadDebugCapability);
   ```
   (Confirmed, by `search_in_dir` across the whole codebase, that line 529 is
   the ONLY real call site of `BuildDockspaceAndMenuBar()` anywhere — every
   other match is a comment/mention, not a call — so no other call site
   needs its own 4th argument added.)

No new `#include` is needed in this file for `IHotReloadDebugCapability` —
same reasoning as Section 3.7: the forward declaration reached through
`EditorLayer.h` is enough, since this file only stores/passes the pointer,
never dereferences it.

### 3.9 — `src/Editor/EditorHost.cpp`

Add, immediately after
`m_editorLayer->SetAssetScaffoldingCapability(&s_editorProjectLifecycleCapability);`
(line 252):

```cpp
    // editor-core-separation-19 campaign (On-Engine Project Workflow plan,
    // BIG-STEP 5), PHASE1 - gives the real ImGui implementation
    // (ImGuiEditorLayer) a live IHotReloadDebugCapability* so its own
    // "Project > Compile" menu item can call TriggerCompileOnly()/
    // IsCompileInFlight() directly. Safe here: s_editorHotReloadDebugCapability
    // (this same file's own namespace-scope static) already exists via
    // ordinary static initialization, strictly before this constructor body
    // ever runs - the exact SAME pointer already passed as m_networkServer's
    // 8th constructor argument (line 214).
    m_editorLayer->SetHotReloadDebugCapability(&s_editorHotReloadDebugCapability);
```

### 3.10 — `src/Editor/DockLayout.h`

Add a forward declaration and extend the function signature:

```cpp
namespace gte {

struct EditorContext;
class Game;
class Renderer;
class IHotReloadDebugCapability; // editor-core-separation-19 campaign, PHASE1.

void BuildDockspaceAndMenuBar(
    EditorContext& ctx, Game& game, Renderer& renderer, IHotReloadDebugCapability* hotReloadDebugCapability);
```

### 3.11 — `src/Editor/DockLayout.cpp` (TWO SEPARATE, NON-ADJACENT edits inside the "Project" menu block — read this whole section before touching anything)

The real, current "Project" menu block (confirmed today) is laid out like
this, in this exact order:

```
line 175: if (ImGui::BeginMenu("Project")) {
176-178:  "New Project..." menu item (unrelated, do not touch)
179-187:  a STALE comment block (starts "// editor-core-separation-16
          campaign - reserved, disabled placeholder for the 'Compile'..."
          and ends with a lone "//" spacer line) - describes the OLD,
          disabled Compile placeholder. This is the ONLY thing this
          section deletes from this stretch of the file.
188-191:  a comment describing "Open Project..." being real (STILL
          ACCURATE - keep this untouched)
192-194:  the real, working "Open Project..." menu item (KEEP UNTOUCHED)
195:      ImGui::Separator(); (KEEP UNTOUCHED)
196:      if (ImGui::MenuItem("Compile", nullptr, false, false)) {} <- the
          ONE line that becomes the real Compile menu item
197:      ImGui::EndMenu();
```

**Do NOT treat lines 179-196 as one single contiguous block to replace** —
lines 188-195 sit BETWEEN the two things this phase actually touches, and
they are real, working, currently-shipping "Open Project..." code that this
phase must not disturb. Doing one big contiguous replace here would silently
delete the working "Open Project..." feature. Do this as two clearly
separate edits instead:

**Edit A — delete the stale comment block (lines 179-187 only)**, i.e.
everything from `// editor-core-separation-16 campaign - reserved, disabled`
through the lone `//` line right before `// editor-core-separation-17
campaign (On-Engine Project Workflow plan, BIG-STEP 3), PHASE4 - "Open
Project..."`. Delete it entirely (it describes a state that no longer
exists once this phase lands) and insert nothing in its place — leave
`ctx.newProjectWindowOpen = true; }` immediately followed by the
`// editor-core-separation-17 campaign ... "Open Project..."` comment, with
nothing in between.

**Edit B — replace the placeholder Compile line only** (originally line
196, `if (ImGui::MenuItem("Compile", nullptr, false, false)) {}` — re-locate
it by its own exact text after Edit A has shifted line numbers, do not
assume the number 196 still applies) with:

```cpp
    // editor-core-separation-19 campaign (On-Engine Project Workflow plan,
    // BIG-STEP 5), PHASE1 - real now. Reads the SAME ActiveProjectAssemblyState
    // singleton BIG-STEP 2/3/4 already read/wrote (LDD-PW2 - exactly one
    // active project). The item stays ENABLED (never disabled) while a
    // build is already in flight for this project - clicking it again must
    // still be possible, to observe the "already in progress" status
    // message (PHASE0_MASTER_STRATEGY.md, Section 3.4) - only its own label
    // gains a "(compiling...)" suffix as a purely informational cue.
    {
        const ActiveProjectAssemblyInfo active = ActiveProjectAssemblyState::Instance().GetActive();
        const bool buildInFlight = (hotReloadDebugCapability != nullptr && active.hasActiveProject)
            && hotReloadDebugCapability->IsCompileInFlight(active.name);
        std::string compileLabel = active.hasActiveProject ? ("Compile '" + active.name + "'") : "Compile";
        if (buildInFlight) {
            compileLabel += " (compiling...)";
        }
        const bool compileEnabled = active.hasActiveProject && hotReloadDebugCapability != nullptr;
        if (ImGui::MenuItem(compileLabel.c_str(), nullptr, false, compileEnabled)) {
            const bool started = hotReloadDebugCapability->TriggerCompileOnly(active.name);
            ctx.projectWorkflowStatusMessage = started
                ? ("Compiling '" + active.name + "' - watch the Log panel for progress.")
                : ("A build for '" + active.name + "' is already in progress.");
            ctx.projectWorkflowStatusIsError = !started;
            ctx.projectWorkflowStatusSetTime = std::chrono::steady_clock::now();
        }
    }
```

Note this deliberately does NOT add its own `ImGui::Separator();` — the
existing one (originally line 195, right after "Open Project...") already
sits immediately before this block and must be left exactly where it is;
adding a second one here would be a redundant, purely cosmetic bug.
(`<chrono>` is already `#include`d by this file, confirmed at line 16 — no
new include needed for `std::chrono::steady_clock::now()`.)

Also add, near the top of the file (alongside the existing `EditorContext.h`/
`EditorPanelRegistry.h`/`PlaybackControls.h`/`SceneIO.h` include block):
```cpp
#include "../Core/EditorCapabilities.h" // IHotReloadDebugCapability - dereferenced below, needs the full type.
#include "ActiveProjectAssemblyState.h"
```
And update the function DEFINITION's own signature (currently line 112) to
match the header exactly:
```cpp
void BuildDockspaceAndMenuBar(
    EditorContext& ctx, Game& game, Renderer& renderer, IHotReloadDebugCapability* hotReloadDebugCapability)
```

## Step 4 — Scoped compile check (do this before moving to Phase2)

After all edits above, run an INCREMENTAL build (never a full clean
rebuild yet — Note 4/5 of this campaign's own delegation instructions) —
e.g. `cmake --build build --target GreatTamanaEditor` — and fix any error
before proceeding. Pay special attention to:
- Every OTHER `IEditorLayer` subclass/override site (there are exactly two:
  `ImGuiEditorLayer` and `NullEditorLayer` — both edited above; a build
  failure naming any THIRD implementer would mean this phase missed one,
  re-`search_in_dir` for `: public IEditorLayer` to double check before
  writing any code, not after).
- `DockLayout.cpp`'s own single call site inside `ImGuiEditorLayer.cpp` —
  confirm no OTHER call site to `BuildDockspaceAndMenuBar()` exists anywhere
  else in the codebase (`search_in_dir` for the function name) that would
  also need its own 4th argument added.
- After Section 3.6's Edit 1, a fresh `search_in_dir` for
  `IHotReloadDebugCapability` inside `EditorLayer.h` should now show at
  least 2 hits (the new forward declaration, plus the new pure virtual) —
  if it still shows zero, Edit 1 was skipped and the very next build
  attempt will fail with an incomplete/undeclared-type error naming
  `SetHotReloadDebugCapability`.
- After Section 3.11's Edit A, re-read the "Open Project..." block (its own
  comment + `if (ImGui::MenuItem("Open Project...")) { ctx.openProjectWindowOpen = true; }`
  + the pre-existing `ImGui::Separator();`) and confirm it is STILL PRESENT,
  byte-for-byte unchanged, immediately before the new Compile block — this
  is the one concrete way to catch it if Edit A/B were accidentally merged
  into one over-broad deletion.

Once it compiles cleanly, `git_add`/`git_commit` this phase's own files with
a message identifying it as PHASE1 of this campaign, then hand off to
`PHASE2_TESTS_LIVE_VERIFICATION_AND_CAMPAIGN_CLOSEOUT.md`.
