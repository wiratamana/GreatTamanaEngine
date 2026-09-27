# PHASE4 — Shader Compilation Wiring — COMPLETION REPORT

**Campaign:** `editor-core-separation-11` — "Project Assembly" System
**Phase:** PHASE4 of 8
**Status:** DONE
**Date:** 2026-09-28

---

## Why this report exists alongside PHASE3's own completion report

`PHASE4_SHADER_COMPILATION_WIRING.md` itself says: *"PHASE3's own
`gte_add_project()` literally calls `gte_add_project_shaders(...)`, which
this phase defines — implement this immediately alongside/after PHASE3,
before considering PHASE3 truly complete."* `PHASE3_FOLDER_LAYOUT_AND_CMAKE_AUTODISCOVERY.md`
independently confirms the same instruction from its own side: *"do not
leave a permanent stub."* Both phase files agree PHASE4 must land in the
same work session as PHASE3 — it did. See `PHASE3_COMPLETION_REPORT.md` for
the full session narrative; this report exists purely so PHASE4 has its own
permanent, dedicated record, matching this repo's own one-report-per-phase
convention, and so PHASE5+ readers can confirm PHASE4 is genuinely done
without having to infer it from PHASE3's own report.

## What was built

`gte_add_project_shaders(TARGET ASSETS_DIR)`, defined in
`cmake/GteProject.cmake` (the same file PHASE3 created), exactly as this
phase file's own Step 1 snippet specifies, verbatim:

- Globs `${ASSETS_DIR}/*.vert`, `*.frag`, `*.comp` (`CONFIGURE_DEPENDS`).
- Re-bases each absolute globbed path to be relative to `CMAKE_SOURCE_DIR`
  via `file(RELATIVE_PATH ...)`, since `gte_add_shader()`'s own `SOURCE`
  parameter is documented/used as repo-root-relative, not
  calling-`CMakeLists.txt`-relative.
- Calls the engine's own, completely unmodified `gte_add_shader(TARGET
  SOURCE)` (`cmake/CompileShaders.cmake`) for each one.

Zero changes were made to `cmake/CompileShaders.cmake` itself — confirmed,
by direct reading, that its existing OUTPUT-based/POST_BUILD two-step
shape already parameterizes purely by `<target>`/`<source>`, needing no
modification to serve a Project Assembly target instead of `GreatTamanaEditor`.

### Test shader added

`Projects/ProjectAssemblyProbe/Assets/ProbeCompute.comp`:

```glsl
#version 450
layout(local_size_x = 1) in;
void main() {}
```

## Verification performed (live, mechanical — see `PHASE3_COMPLETION_REPORT.md`
for the exact commands/output; summarized here)

1. **First build** of `ProjectAssemblyProbe_Game`/`_Editor`: `glslc`
   compiled `ProbeCompute.comp` to
   `build/shaders/ProbeCompute.comp.spv` (the engine's own shared
   intermediate folder — confirmed this is fine/expected, a build-tree
   scratch location, not a deployed one), then staged a copy to
   `build/project_assemblies/shaders/ProbeCompute.comp.spv` (next to each
   `.dll`, in a `shaders/` sub-folder distinct from
   `build/shaders/` next to `GreatTamanaEditor.exe` itself — confirmed via
   `browse_dir` on both folders, no collision).
2. **No-change rebuild**: `cmake --build build --target
   ProjectAssemblyProbe_Game --target ProjectAssemblyProbe_Editor` →
   `ninja: no work to do` — confirms proper incremental-rebuild dependency
   tracking (nothing re-invokes `glslc` when nothing changed).
3. **Real-change rebuild**: appended a comment line to `ProbeCompute.comp`,
   rebuilt — `glslc` re-ran exactly once (`[1/2] Compiling shader
   Projects/ProjectAssemblyProbe/Assets/ProbeCompute.comp -> ...`), and
   `browse_dir` with `details: true` confirmed BOTH the intermediate
   (`build/shaders/ProbeCompute.comp.spv`) and the staged
   (`build/project_assemblies/shaders/ProbeCompute.comp.spv`) copies picked
   up the new timestamp — the POST_BUILD staging step ran silently
   alongside the visible recompile line, exactly as `cmake/CompileShaders.cmake`'s
   own two-custom-command design predicts.
4. **Survived a full delete/recreate of the entire `Projects/ProjectAssemblyProbe/`
   folder** (performed as part of PHASE3's own `CONFIGURE_DEPENDS` test —
   see that report) — the shader recompiled and re-staged correctly the
   moment the folder (including `ProbeCompute.comp`) was recreated and
   built again, with no manual reconfigure step.

## The exact staged relative path string — for PHASE8 to reuse verbatim

Per PHASE0's own Finding F (every existing internal shader load site uses a
plain, bare relative path, zero `gte::ExecutableDirectory()` prefix, CWD ==
exe dir):

```
"project_assemblies/shaders/ProbeCompute.comp.spv"
```

(relative to `GreatTamanaEditor.exe`'s own directory at runtime — confirmed
concretely present on disk at
`build/project_assemblies/shaders/ProbeCompute.comp.spv`, which is exactly
`<exe dir>/project_assemblies/shaders/ProbeCompute.comp.spv`).

## Definition of Done — checklist (this phase file's own list)

- [x] `gte_add_project_shaders()` defined in `cmake/GteProject.cmake`,
      correctly re-bases globbed absolute paths to `CMAKE_SOURCE_DIR`-relative
      before calling `gte_add_shader()`.
- [x] No PHASE3 temporary stub existed to delete — PHASE3 was implemented
      with this phase's real definition already in place from the start
      (see "Why this report exists" above).
- [x] The test project's `ProbeCompute.comp` compiles to SPIR-V and stages
      correctly next to `ProjectAssemblyProbe_Game.dll`/`ProjectAssemblyProbe_Editor.dll`,
      in a `shaders/` sub-folder distinct from the engine's own internal one.
- [x] Incremental rebuild behavior confirmed (recompiles only on real
      source change).
- [x] The exact staged relative path string is recorded above for PHASE8 to
      reuse verbatim.

## What this phase did NOT do (as instructed)

- Did NOT write any C++ code that loads/uses the compiled `.spv` — entirely
  PHASE8's concern.
- Did NOT add any new shader language feature, `#include` resolution
  behavior, or compilation flag beyond what `gte_add_shader()` already
  provides.

## Result

**PHASE4 is DONE**, built and verified together with PHASE3 in the same
session, per both phase files' own explicit instructions to do so. PHASE8
can now build a real `ComputePipeline` from
`"project_assemblies/shaders/ProbeCompute.comp.spv"` (or its own
replacement shader, following the same staging convention) with zero
further CMake wiring needed.
