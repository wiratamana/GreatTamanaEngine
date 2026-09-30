# PHASE1 — COMPLETION REPORT: Core-Owned Debug Metadata Interfaces (write + read)

Campaign: `editor-core-separation-25` ("Core/Editor Separation: PassRecord Debug
Metadata Sink"), Branch: `feature/editor-core-separation`.

## What was read first

- `readme.md`, `AGENTS.md` (full).
- `task_manager/editor-core-separation-25/PHASE0_MASTER_STRATEGY.md` (full).
- `task_manager/editor-core-separation-25/PHASE1_CORE_DEBUG_METADATA_INTERFACES.md`
  (full — this phase's own plan).
- The source design document (read-only, separate repo):
  `C:\Users\F5954\Documents\TAMANA\GreatTamanaEngin-Ideas\project-assembly-impl-5\
  BIG_STEP_1_CORE_EDITOR_SEPARATION_PASSRECORD_METADATA_SINK_2026-09-29.txt`
  (full — all 9 sections).

No ambiguity was found beyond what PHASE0/PHASE1 already resolved, so
`ask_questions` was not needed for this phase.

## What was done

### 1. New file: `src/Renderer/RenderGraph/RenderGraphDebugMetadataSink.h`

A brand-new, self-contained, header-only file (no matching `.cpp` — pure
interface declarations + one plain struct, exactly as the phase plan
specifies). It defines, inside `namespace gte::rg`:

- **`PassDebugMetadata`** — a plain, 3-field, copyable value struct:
  `RenderPassCategory category`, `RenderPassDrawKind drawKind`,
  `RenderPassTagMask tags` (each defaulted to the same defaults `PassRecord`
  itself uses today).
- **`IPassDebugMetadataSink`** — Core-owned, pure-virtual, **exactly 2**
  methods:
  - `virtual void OnPassDeclared(std::size_t declarationIndexThisFrame, RenderPassCategory category, RenderPassDrawKind drawKind, RenderPassTagMask tags) = 0;`
  - `virtual void BeginFrame() = 0;`
- **`IPassDebugMetadataProvider`** — Core-owned, pure-virtual, **exactly 1**
  method:
  - `virtual bool QueryPassDebugMetadata(std::size_t declarationIndex, PassDebugMetadata& outMetadata) const = 0;`

The file only `#include`s `RenderGraphTypes.h` (for `RenderPassCategory`/
`RenderPassDrawKind`/`RenderPassTagMask`, all three already defined there,
none redefined or relocated) plus `<cstddef>` for `std::size_t`. Doc comments
mirror this folder's own established style (`RenderGraphDebugTextureRegistry.h`
was read as the precedent before writing), and explain both deliberate
differences from `GpuMemoryTracker::DebugNameObserver`'s precedent (the extra
`BeginFrame()` lifecycle method, and the separate read-only provider
interface) plus the reasoning behind Locked Decision 1's resolution.

Nothing else in the file does anything — no logic, no state, no default
implementations. Neither interface is implemented anywhere yet; that is
PHASE2's job (`FrameDebuggerPassMetadataRecorder`).

### 2. CMake registration

Added exactly one new line to the root `CMakeLists.txt`'s explicit `gte_core`
source list, immediately next to the existing
`src/Renderer/RenderGraph/RenderGraphDebugTextureRegistry.h` entry (now at
line 800/801):

```
    src/Renderer/RenderGraph/RenderGraphDebugTextureRegistry.h
    src/Renderer/RenderGraph/RenderGraphDebugMetadataSink.h
    src/Renderer/RenderGraph/RenderGraphDebugTextureRegistry.cpp
```

No `tests/CMakeLists.txt` change — this phase adds no test file (per the
phase plan: PHASE2 is the first real implementer and owns its own test
file).

## Verification performed

1. **Incremental build**: `cmake --build build` (working directory: project
   root) — CMake re-configured cleanly (picked up the new `CMakeLists.txt`
   source-list entry), then Ninja reported **`ninja: no work to do`** — this
   is the CORRECT, expected outcome for a purely-additive header nothing yet
   `#include`s (confirmed against the phase file's own Step 2 note: "the
   simplest real proof is PHASE2's own test file... compiling cleanly").
   Since PHASE2 does not exist yet this phase, an extra, PHASE1-local,
   scratch/throwaway probe was used instead, exactly as the phase file's own
   Step 3.3 alternative instructs:
   - Wrote a scratch file, `build/scratch_probe_phase1.cpp`, that
     `#include`s the new header and derives one small concrete class from
     each of the two interfaces (implementing every pure-virtual method).
   - Compiled it directly with the `gcc` tool (`use_gpp: true`,
     `-std=c++20`), using this project's real include paths taken directly
     from `build/build.ninja`'s own recorded `INCLUDES` for
     `RenderGraphSnapshot.cpp.obj` (`src`, `third_party/volk`, `include`,
     `third_party/vma`).
   - Result: **zero errors, zero warnings** (empty stdout/stderr).
   - The scratch `.cpp` and its `.o` were both deleted immediately
     afterward (confirmed never committed — see `git_status` below).
2. **Exactly-2 / exactly-1 method count**, confirmed by direct code
   inspection of the header just written (quoted above): `IPassDebugMetadataSink`
   has exactly `OnPassDeclared()` + `BeginFrame()`, no more, no less;
   `IPassDebugMetadataProvider` has exactly `QueryPassDebugMetadata()`, no
   more, no less. (This is a literal, later PHASE5/Section-8
   acceptance-criteria checkbox — confirmed correct here so it does not need
   revisiting.)
3. **`git_status`** after all changes:
   ```
   Changes not staged for commit:
       modified:   CMakeLists.txt
   Untracked files:
       src/Renderer/RenderGraph/RenderGraphDebugMetadataSink.h
   ```
   Exactly the two files this phase was supposed to touch — nothing else in
   the tree differs.

No targeted `ctest` run was performed for this phase — the phase file
explicitly states no new test file is required in isolation (PHASE2 is the
first real implementer and owns the first dedicated test file), and this
change touches no existing test or existing behavior of any kind.

## Scope discipline confirmed

- Zero existing file's *behavior* changed — only one new line added to
  `CMakeLists.txt`'s source list (build-system registration only) and one
  brand-new header created.
- `PassRecord`, `AddRenderPass()`, `AddPass()`, `AddComputePass()`,
  `BuildRenderGraphSnapshot()`, `ViewScope` — none of these were touched.
  They remain PHASE3's job.
- No `.cpp` file was created for the new header (correctly header-only, per
  the phase plan).
- `RenderPassCategory`/`RenderPassDrawKind`/`RenderPassTagMask` themselves
  were not redefined, relocated, or modified in any way — only referenced by
  name from the new header, exactly as the phase plan requires (Non-Goal 1
  of the source document).

## Next phase

PHASE2 (`PHASE2_EDITOR_FRAME_DEBUGGER_PASS_METADATA_RECORDER.md`) implements
both interfaces concretely under `src/Editor/` and adds the first dedicated
Tier-1 test file exercising them.
