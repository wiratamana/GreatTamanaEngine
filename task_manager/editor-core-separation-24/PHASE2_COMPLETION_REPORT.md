# PHASE2 — Completion Report: Project Template Auto-Wire Anchors

Parent: `PHASE0_MASTER_STRATEGY.md`. Phase file:
`PHASE2_PROJECT_TEMPLATE_AUTO_WIRE_ANCHORS.md`.

## What changed

Exactly one function body changed, exactly as the phase file's own Step 3
prescribes, byte-for-byte, verified against the real file contents
immediately before editing (matched the phase file's own Step 2 citation —
lines 59-82 — with zero drift found).

**`src/Editor/EditorProjectLifecycleCapability.cpp`** —
`BuildGameStubCppContent()`'s body was replaced in place (now lines 59-94,
grew from 24 to 36 lines because of the two new anchor-comment blocks):

1. The `RegisterProject` parameter changed from `gte::Core& /*core*/`
   (commented out, unused) to `gte::Core& core` (named) — required so a
   future auto-wired call can actually reference `core` by name and compile.
2. A new `// GTE_AUTO_REGISTER_FORWARD_DECLARATIONS - do not remove or edit
   this line.` comment block was inserted right after the two `#include`
   lines, before `namespace {`.
3. A new `// GTE_AUTO_REGISTER_ANCHOR - do not remove or edit this line.`
   comment block was inserted as the sole body of `RegisterProject()`,
   replacing the old "Intentionally empty..." comment.

Both anchor literals were typed to match the phase file's own Step 3 text
exactly, character for character (`GTE_AUTO_REGISTER_FORWARD_DECLARATIONS -
do not remove or edit this line.` / `GTE_AUTO_REGISTER_ANCHOR - do not
remove or edit this line.`) — PHASE4's `TryAutoWireRegisterCall()` will do a
plain `std::string::find()` for these exact strings, so any drift here would
silently break auto-wiring for every future project.

**Nothing else changed.** `CreateNewProjectAssembly()`'s own call site
(`WriteTextFile(assetsDirectory / (name + "Game.cpp"),
BuildGameStubCppContent(name));`, line ~374 before this edit) was not
touched — it already just calls `BuildGameStubCppContent(name)` and writes
whatever string comes back, no change needed there.

## Judgment call on the optional header-comment wording (Step 3, "Worth
noting, not a required change")

**Decision: left unchanged, deliberately.** The generated file's own header
comment (line 4: `"...to scaffold a starting Render Pass / Compute Shader /
Vertex+Fragment Shader pair."`) still lists only the three pre-existing
scaffold kinds and does not mention "Screen Post-Process Pass". The phase
file explicitly said the source design document's own Step 2B gives this
template byte-for-byte and does not require that comment to change, and
explicitly left it as the implementer's own judgment call. Chose to leave it
exactly as specified rather than improvise wording not present in the design
document — this keeps the generated output an exact, literal match to the
design doc's own Step 2B text, with zero risk of a typo/wording drift in a
part of the template that was optional to touch at all. Not a functional
issue either way (it's a comment, not machine-read by anything).

## Step 4: Honest, explicit consequence (restated as required)

Every project created BEFORE this phase shipped — including
`Projects/ProjectAssemblyProbe/` and any other pre-existing project — has
NEITHER anchor comment, and its `RegisterProject` parameter is still
`gte::Core& /*core*/` (commented out). Per PHASE4's own "missing anchor = do
nothing, fall back to the reminder message" contract (once PHASE4/PHASE6
land), scaffolding a new Screen Post-Process Pass into one of these OLD
projects will still be 100% safe — it simply falls back to the original,
fully-manual "remember to add this line yourself" behavior for that one
project, forever, unless a human manually adds the two anchors and
un-comments `core` themselves. This phase does NOT retroactively rewrite any
existing project's `<ProjectName>Game.cpp` — confirmed: no file under
`Projects/` was touched by this phase, only the in-memory C++ template
function used for FUTURE `POST /project_assembly/create_project` calls.

## Verification (Step 5)

1. **Incremental build** (`cmake --build build`) — succeeded, 7/7 steps,
   zero errors, zero new warnings (`gte_editor`/`GreatTamanaEditor.exe`/
   `GreatTamanaEngineTests.exe`/`ProjectAssemblyProbe_Editor.dll`/
   `ProjectAssemblyProbe_Game.dll` all relinked cleanly).

2. **Live check** — launched the freshly-built `GreatTamanaEditor.exe`
   (`run_app_background`), called
   `POST /project_assembly/create_project?name=TemplateAnchorSmokeTest`
   (HTTP 200, `created_source_directory` confirmed pointing at
   `Projects/TemplateAnchorSmokeTest`), then `read_file`'d the generated
   `Projects/TemplateAnchorSmokeTest/Assets/TemplateAnchorSmokeTestGame.cpp`
   back and confirmed BYTE-FOR-BYTE:
   - Both anchor comments present, verbatim, in the correct positions.
   - `void RegisterProject(gte::Core& core)` — parameter named, not
     commented out.
   - Everything else (header comment, includes, namespace wrapper,
     `GTE_DEFINE_PROJECT_EXPORTS_GAME(RegisterProject)`) unchanged from the
     original template shape.

   Confirmed the generated project actually compiles: stopped the running
   Editor, re-ran `cmake -S . -B build` (required — a brand-new
   `Projects/<Name>/Libraries/CMakeLists.txt` is only discovered by the
   root `CMakeLists.txt`'s `add_subdirectory()` auto-discovery loop at
   CONFIGURE time, not build time), then
   `cmake --build build --target TemplateAnchorSmokeTest_Game` — succeeded,
   producing `project_assemblies/TemplateAnchorSmokeTest_Game.dll` with
   zero errors (an empty `RegisterProject()` body — just the anchor
   comment — is indeed perfectly valid C++, as the phase file predicted).

3. **Cleanup** — deleted the throwaway
   `Projects/TemplateAnchorSmokeTest/` folder and its built
   `project_assemblies/TemplateAnchorSmokeTest_Game.dll` artifact (this is
   NOT the permanent PHASE8 fixture — that one is created fresh, later, in
   PHASE8, specifically to prove the full auto-wire feature end to end).
   Re-ran `cmake -S . -B build` again to drop the now-deleted project's
   subdirectory from the build graph, then a final `cmake --build build` —
   `ninja: no work to do.`, confirming a clean, fully-caught-up build tree
   with no dangling reference to the deleted throwaway target.
   `git status` confirms only the one intended source file is modified
   (`Projects/` is `.gitignore`d, so the throwaway project's creation and
   deletion left zero git-visible trace either way).

4. **No new Tier-1 test** — confirmed correct per the phase file's own Step
   5 item 4: this is a pure string-literal template change, and PHASE8's own
   live verification (which this phase's own Step 5.2 above already
   previewed) proves the template's real output byte-for-byte against a
   real generated file. `tests/Editor/AssetScaffoldTemplateTests.cpp` was
   left untouched — optional for this phase, not added.

## Files touched

- `src/Editor/EditorProjectLifecycleCapability.cpp`
- `task_manager/editor-core-separation-24/PHASE2_COMPLETION_REPORT.md` (this
  file)

No full clean build / full `ctest` regression pass was run (Locked Decision
2, PHASE0 — that is PHASE8's own job).
