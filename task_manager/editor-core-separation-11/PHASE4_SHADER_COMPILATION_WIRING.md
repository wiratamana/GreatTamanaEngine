# PHASE4 of 8 — SHADER COMPILATION WIRING

Read `PHASE0_MASTER_STRATEGY.md` first (§2.4, Finding F, for the shader-path
convention this phase's own test relies on).
**Depends on:** PHASE3 (the `_Game`/`_Editor` CMake targets must already
exist before this phase's function can be called against them). PHASE3's own
`gte_add_project()` literally calls `gte_add_project_shaders(...)`, which
this phase defines — implement this immediately alongside/after PHASE3,
before considering PHASE3 truly complete.
**Blocks:** nothing downstream depends specifically on this phase finishing
first, but PHASE8's own real compute pass needs a real, compiled `.spv` to
build a pipeline from — this phase is what produces one.

This is the smallest, lowest-risk phase in the whole campaign. The engine's
existing shader pipeline is already fully generic — this phase reuses it
completely unmodified (confirmed by directly reading the real, current
`cmake/CompileShaders.cmake`: zero changes needed to it).

## Why this is safe, confirmed concretely

The engine's own internal shaders compile via `gte_add_shader(<target>
<source-file-relative-to-CMAKE_SOURCE_DIR>)`, already defined in
`cmake/CompileShaders.cmake` and already called today for the engine's own
targets (e.g. `gte_add_shader(GreatTamanaEditor src/Shaders/Triangle.vert)`,
confirmed, root `CMakeLists.txt` line 1321). This function:

1. Compiles `<source>` to SPIR-V via `glslc` into
   `${CMAKE_BINARY_DIR}/shaders/<name>.spv` (an OUTPUT-based
   `add_custom_command`, giving proper incremental-rebuild tracking).
2. Copies that compiled `.spv` into `$<TARGET_FILE_DIR:${TARGET}>/shaders/<name>.spv`
   — staged NEXT TO whatever `<target>`'s own built binary lands (a
   POST_BUILD `add_custom_command`).

Both steps are already parameterized purely by `<target>` and `<source>`.
Calling this again for `${NAME}_Game`/`${NAME}_Editor` instead of
`GreatTamanaEditor` needs ZERO modification to this file — the existing
`$<TARGET_FILE_DIR:${TARGET}>` generator expression correctly resolves to
`GTE_PROJECT_ASSEMBLY_OUTPUT_DIR` (PHASE3) for these new targets
automatically, since that IS their `RUNTIME_OUTPUT_DIRECTORY`.

The one restriction to be aware of, unrelated to this mechanism:
`gte_add_shader()`'s `SOURCE` parameter is documented and used as "relative
to `CMAKE_SOURCE_DIR`" (the repo root), NOT relative to the calling
`CMakeLists.txt`'s own folder. A Project Assembly's shader files live under
`Projects/<Name>/Assets/`, which is OUTSIDE `src/` but still a real, valid
sub-path of `CMAKE_SOURCE_DIR` — this still works unmodified, it is simply a
longer relative path. Verify this explicitly in Step 3 below rather than
trusting it purely by re-reading the existing function's comment.

## STEP 1 — define `gte_add_project_shaders()` in `cmake/GteProject.cmake`
(the same file PHASE3 created)

Confirm `cmake/CompileShaders.cmake` is `include()`-d SOMEWHERE in root
`CMakeLists.txt` before `gte_add_project()` is actually CALLED (i.e. before
PHASE3's auto-discovery `add_subdirectory()` loop runs) — confirmed,
`include(CompileShaders)` at line 127, well before the discovery block near
the end of the file. A CMake `function()` body is only evaluated when
CALLED, not when defined, so the relative include ORDER of
`GteProject.cmake` vs. `CompileShaders.cmake` does not matter, only that
`CompileShaders.cmake` has been included SOMEWHERE before the call happens.
State this explicitly in your own code comment so a future reader does not
"fix" the include order unnecessarily.

```cmake
# editor-core-separation-11 campaign (Project Assembly system), PHASE4. Thin
# wrapper around the engine's OWN, already-generic gte_add_shader()
# (cmake/CompileShaders.cmake) - globs every .vert/.frag/.comp under a
# project's own Assets/ folder and compiles each one exactly the way the
# engine's own internal shaders already are. No new shader-compilation
# mechanism is invented here.
function(gte_add_project_shaders TARGET ASSETS_DIR)
    file(GLOB_RECURSE PROJECT_SHADER_SOURCES CONFIGURE_DEPENDS
        "${ASSETS_DIR}/*.vert" "${ASSETS_DIR}/*.frag" "${ASSETS_DIR}/*.comp")

    foreach(SHADER_SOURCE ${PROJECT_SHADER_SOURCES})
        # gte_add_shader()'s own SOURCE parameter is documented/used as
        # relative to CMAKE_SOURCE_DIR (repo root) - PROJECT_SHADER_SOURCES
        # above is a list of ABSOLUTE paths (file(GLOB_RECURSE) always
        # returns absolute paths), so it must be re-expressed as relative to
        # CMAKE_SOURCE_DIR before being handed to gte_add_shader(), exactly
        # like every existing internal call site already is. Confirmed by
        # directly reading cmake/CompileShaders.cmake's own body: it does
        # set(SOURCE_ABSOLUTE "${CMAKE_SOURCE_DIR}/${SOURCE}") internally -
        # i.e. it ALWAYS prepends CMAKE_SOURCE_DIR, so passing an
        # already-absolute path here would silently produce a broken,
        # doubled path (e.g. "C:/repo/C:/repo/Projects/...") that fails
        # loudly (file not found) - do not skip this re-basing step.
        file(RELATIVE_PATH SHADER_SOURCE_RELATIVE "${CMAKE_SOURCE_DIR}" "${SHADER_SOURCE}")
        gte_add_shader(${TARGET} "${SHADER_SOURCE_RELATIVE}")
    endforeach()
endfunction()
```

## STEP 2 — remove any temporary stub

If PHASE3 was implemented with a temporary no-op stub for
`gte_add_project_shaders()`, delete it now — this phase's real definition
fully replaces it.

## STEP 3 — prove it concretely, using the same test project PHASE3 created

Add one real shader file:

```
Projects/ProjectAssemblyProbe/Assets/ProbeCompute.comp
```

A minimal, syntactically valid compute shader is enough (this phase only
proves the COMPILE step, not that the shader is ever dispatched — that is
PHASE8's job):

```glsl
#version 450
layout(local_size_x = 1) in;
void main() {}
```

Rebuild `ProjectAssemblyProbe_Game` and confirm:

1. `build/shaders/ProbeCompute.comp.spv` exists (the OUTPUT-based
   intermediate compile target — this lands in the ENGINE's own shared
   intermediate folder, `${CMAKE_BINARY_DIR}/shaders/`, which is fine and
   expected — it is a build-tree scratch location, not a staged/deployed
   one).
2. `build/project_assemblies/shaders/ProbeCompute.comp.spv` exists (the
   POST_BUILD-staged copy, next to `ProjectAssemblyProbe_Game.dll` — NOT
   `build/shaders/`, which is the ENGINE's own internal shader staging
   folder next to `GreatTamanaEditor.exe`; confirm these are genuinely two
   separate destination folders that do not collide).
3. Touch (re-save) `ProbeCompute.comp`, rebuild again, confirm ONLY the
   shader recompiles (incremental-rebuild tracking works) — check build tool
   output/timestamps for both a no-change rebuild (nothing re-invokes
   `glslc`) and a real-change rebuild (it does).

**This exact staged path — `project_assemblies/shaders/ProbeCompute.comp.spv`
relative to `GreatTamanaEditor.exe`'s own directory — is the literal string
PHASE8's `ComputePipeline` construction call must use** (PHASE0 §2.4, Finding
F: every existing internal shader load site uses a plain, bare relative
path with zero `gte::ExecutableDirectory()` prefix, relying on CWD == exe
dir; a Project Assembly's own shader follows this exact same convention:
`"project_assemblies/shaders/ProbeCompute.comp.spv"`).

## Definition of Done

- [ ] `gte_add_project_shaders()` defined in `cmake/GteProject.cmake`,
      correctly re-bases globbed absolute paths to `CMAKE_SOURCE_DIR`-relative
      before calling `gte_add_shader()`.
- [ ] Any PHASE3 temporary stub is deleted.
- [ ] The test project's `ProbeCompute.comp` compiles to SPIR-V and stages
      correctly next to `ProjectAssemblyProbe_Game.dll`, in a `shaders/`
      sub-folder distinct from the engine's own internal one.
- [ ] Incremental rebuild behavior confirmed (recompiles only on real
      source change).
- [ ] The exact staged relative path string is recorded in this phase's
      completion report for PHASE8 to reuse verbatim.

## What this phase does NOT do

- Does NOT write any C++ code that actually LOADS/uses this compiled `.spv`
  (building a real `Pipeline`/`ComputePipeline` object from it) — entirely
  PHASE8's concern.
- Does NOT add any new shader language feature, `#include` resolution
  behavior, or compilation flag beyond what `gte_add_shader()` already
  provides.
