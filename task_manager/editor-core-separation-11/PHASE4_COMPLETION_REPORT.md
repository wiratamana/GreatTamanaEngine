# PHASE4 — Shader Compilation Wiring — COMPLETION REPORT

**Campaign:** `editor-core-separation-11` — "Project Assembly" System
**Phase:** PHASE4 of 8
**Status:** DONE
**Date:** 2026-09-28 (independently re-verified in a dedicated PHASE4 session,
correcting one inaccurate claim left by the PHASE3 session's own write-up —
see "Correction versus the previous report" below)

---

## Why this report supersedes the previous one

`gte_add_project_shaders()` was actually implemented during the PHASE3
session (per that phase file's own explicit "do not leave a permanent stub"
instruction), and a first version of this report was written at that time.
This PHASE4 session re-read `PHASE0_MASTER_STRATEGY.md` and
`PHASE4_SHADER_COMPILATION_WIRING.md` in full (per this campaign's own
"never trust a prior report blindly" rule, LDD6), re-verified every anchor
via `search_in_dir`/`read_file`, and then independently REPRODUCED every
item on this phase's own Definition of Done from scratch, rather than
accepting the prior report's claims at face value. **One of the prior
report's own claims turned out to be wrong** — a genuine, mechanically
re-confirmed discrepancy — documented in full below. This is the corrected,
authoritative version of this report.

## What was built (unchanged from PHASE3 session — re-confirmed, not re-done)

`gte_add_project_shaders(TARGET ASSETS_DIR)`, defined in
`cmake/GteProject.cmake` (confirmed present, lines 121-153, exactly matching
this phase file's own Step 1 snippet verbatim):

- Globs `${ASSETS_DIR}/*.vert`, `*.frag`, `*.comp` (`CONFIGURE_DEPENDS`).
- Re-bases each absolute globbed path to be relative to `CMAKE_SOURCE_DIR`
  via `file(RELATIVE_PATH ...)`, since `gte_add_shader()`'s own `SOURCE`
  parameter is documented/used as repo-root-relative, not
  calling-`CMakeLists.txt`-relative.
- Calls the engine's own, completely unmodified `gte_add_shader(TARGET
  SOURCE)` (`cmake/CompileShaders.cmake`) for each one.

Zero changes were made to `cmake/CompileShaders.cmake` itself — re-confirmed
again this session, by directly reading its current, real body: its
existing OUTPUT-based/POST_BUILD two-step shape already parameterizes purely
by `<target>`/`<source>`, needing no modification to serve a Project
Assembly target instead of `GreatTamanaEditor`.

No PHASE3 temporary stub existed to remove — PHASE3 was implemented with
this phase's real definition already in place from the start.

### Test shader (unchanged)

`Projects/ProjectAssemblyProbe/Assets/ProbeCompute.comp` (confirmed present,
57 bytes, byte-identical to this phase file's own Step 3 snippet):

```glsl
#version 450
layout(local_size_x = 1) in;
void main() {}
```

## Independent re-verification performed this session (own commands, own
output — not copied from the prior report)

1. **Anchor re-check**: `search_in_dir` for `gte_add_project_shaders` inside
   `cmake/` confirmed 4 hits in `GteProject.cmake` (the call sites at lines
   59/73 and the definition at line 133), matching the prior report exactly.
2. **File-state re-check**: `browse_dir` on
   `Projects/ProjectAssemblyProbe/Assets/` confirmed `ProbeCompute.comp`
   exists (57 bytes) alongside `HelloGame.cpp` and `Editor/`.
3. **No-change incremental rebuild**: `cmake --build build --target
   ProjectAssemblyProbe_Game --target ProjectAssemblyProbe_Editor` →
   `ninja: no work to do` — confirms nothing spuriously re-invokes `glslc`
   when nothing changed.
4. **Real-change incremental rebuild (comment-only edit)**: appended a
   comment line to `ProbeCompute.comp`, rebuilt — `glslc` re-ran exactly
   once, producing `build/shaders/ProbeCompute.comp.spv` (368 bytes... —
   actually landed byte-identical, see below). Confirms the OUTPUT-based
   `add_custom_command`'s own dependency tracking (keyed on the real GLSL
   source file's mtime) works correctly and re-invokes `glslc` on any
   source-file save, not merely on semantic changes.
5. **Real-change incremental rebuild (semantic edit)**: changed
   `local_size_x = 1` to `local_size_x = 2` (a genuine compiled-bytecode
   difference, not just a comment) — `glslc` re-ran, `build/shaders/ProbeCompute.comp.spv`
   grew from 352 to 368 bytes with a fresh timestamp, confirmed via
   `browse_dir(details:true)`.

## Correction versus the previous report — a genuine, newly-confirmed gap
(this is the important finding of this session)

The PHASE3-session report claimed: *"a no-change rebuild produced `ninja: no
work to do`; touching `ProbeCompute.comp` ... triggered exactly one `glslc`
recompile and re-staged `build/project_assemblies/shaders/ProbeCompute.comp.spv`
(confirmed via file timestamps)."* **Re-tested independently this session,
step by step, and the "re-staged" half of that claim is FALSE as a general
rule** — mechanically reproduced twice:

- After the comment-only edit (Step 4 above): the intermediate
  `build/shaders/ProbeCompute.comp.spv` was re-written by `glslc` (a fresh,
  real custom-command run — confirmed in the build log), but the STAGED copy
  at `build/project_assemblies/shaders/ProbeCompute.comp.spv` kept its OLD
  timestamp and byte count. A second, immediate rebuild attempt reported
  `ninja: no work to do` — i.e. the build system considered
  `ProjectAssemblyProbe_Game`/`_Editor` **fully up to date** despite the
  staged shader now being stale relative to the just-recompiled intermediate
  one. (In this specific case the compiled bytes happened to be identical
  anyway, since GLSL comments compile away — but the STAGING STEP itself
  provably did not even attempt to run, confirmed by inspecting `build.ninja`
  directly, not merely by the coincidental byte-identical outcome.)
- After the semantic edit (Step 5 above, genuinely different compiled
  bytes): same result — the intermediate `.spv` updated to 368 bytes, but
  `build/project_assemblies/shaders/ProbeCompute.comp.spv` remained the OLD
  352-byte file, confirmed via `browse_dir`. A follow-up `ninja -v` run
  reported `no work to do` — the staging step genuinely never re-ran.

**Root cause, confirmed by reading the real, generated `build/build.ninja`
directly** (not guessed): `cmake/CompileShaders.cmake`'s `gte_add_shader()`
adds the compiled `${COMPILED}` path to the consuming target via
`target_sources(${TARGET} PRIVATE ...)`. For a SHARED library target, this
only wires `${COMPILED}` into ninja as an **order-only** prerequisite of
`cmake_object_order_depends_target_<NAME>` (confirmed at
`build.ninja` — `build cmake_object_order_depends_target_ProjectAssemblyProbe_Game:
phony || GreatTamanaEditor.exe shaders/ProbeCompute.comp.spv`, note the `||`
= order-only). The actual LINK edge for
`project_assemblies/ProjectAssemblyProbe_Game.dll` — the edge the
`POST_BUILD` staging-copy command is physically attached to — lists only
`HelloGame.cpp.obj` and `libGreatTamanaEditor.dll.a` as its real,
staleness-tracked inputs; the compiled `.spv` is **not** one of them at all,
not even order-only. Ninja therefore has no reason to consider the link
edge "dirty" purely because the shader recompiled, so the `POST_BUILD`
copy command living on that same edge never re-executes.

**Confirmed this is a real dependency-wiring gap, not merely a theoretical
one, by forcing the opposite case**: added then reverted a harmless comment
line to `HelloGame.cpp` (an actual link-input change) while the shader was
still in its "changed but not yet staged" state — rebuilding then DID
relink `ProjectAssemblyProbe_Game.dll` and, in the same step, correctly ran
`Staging ProbeCompute.comp.spv next to ProjectAssemblyProbe_Game`, and the
staged file's byte count/timestamp finally caught up. This proves the
staging command itself is correctly written (`copy_if_different`, correct
paths) — it is purely a **missing dependency edge**, not a broken command.

**Why this was invisible for the engine's own internal shaders** (and why
neither this campaign's PHASE0 nor the PHASE3 session caught it): for
`GreatTamanaEditor` itself, `${CMAKE_BINARY_DIR}/shaders/<name>.spv` (the
intermediate/OUTPUT path) and `$<TARGET_FILE_DIR:GreatTamanaEditor>/shaders/<name>.spv`
(the staged/POST_BUILD path) are the exact SAME file (`GreatTamanaEditor`'s
own runtime output directory IS `CMAKE_BINARY_DIR`) — so the "staging copy"
for the engine's own shaders is a silent no-op self-copy, and the missing
link-edge dependency never matters: the moment `glslc` runs, the file
already sits at its final, real, used location. **A Project Assembly target
is the first real consumer of this shared mechanism where the intermediate
and staged paths genuinely differ** (`build/shaders/` vs.
`build/project_assemblies/shaders/`), which is exactly what exposes the gap.

### Practical consequence / risk this creates for later phases

A developer (or an automated agent in a later phase, e.g. PHASE8 iterating
on the real compute shader) who edits ONLY a Project Assembly `.comp`/
`.vert`/`.frag` file and rebuilds with a target-scoped `cmake --build build
--target <Name>_Game` will see `glslc` genuinely re-run (so it LOOKS like
the change took effect), but the actually-loaded, staged copy under
`project_assemblies/shaders/` can silently stay stale unless something else
in that same build also forces the target to relink (e.g. a `.cpp` change,
a full clean, or `ninja -t clean <target>` first). This is a genuine,
confirmed, silent-staleness risk — not a build failure, which makes it more
dangerous (no error, just wrong/old shader bytes staged).

### Disposition — documented, not fixed, in this phase

Per this phase file's own explicit scope ("This phase reuses it completely
unmodified... Zero changes needed to it" / "Does NOT add any new shader
language feature... beyond what `gte_add_shader()` already provides") and
per `PHASE0_MASTER_STRATEGY.md`'s LDD1 (additive/parallel changes only,
never casually touching shared existing mechanisms), **this report
deliberately does NOT patch `cmake/CompileShaders.cmake`** to add a proper
dependency edge (e.g. via `OBJECT_DEPENDS`/`add_dependencies()` from the
target's link step onto `${COMPILED}`) — that is shared, cross-cutting
infrastructure whose only other consumer is every one of the engine's own
existing internal shaders, and changing it is a bigger-than-PHASE4
decision. Flagging it here, concretely, mechanically confirmed, is this
phase's own job; fixing it is left for a future maintenance pass or for
PHASE8 to explicitly decide whether it needs to touch this (PHASE8 should,
at minimum, do a full/clean rebuild of its own Project Assembly targets
after any shader edit, rather than relying on a target-scoped incremental
build alone, until/unless this gap is closed).

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
`<exe dir>/project_assemblies/shaders/ProbeCompute.comp.spv`, re-confirmed
this session via `browse_dir` after restoring the test files to their
canonical, committed state and doing one final clean rebuild — see
"Final state" below.)

## Final state — test files restored, build clean, live smoke-test passed

All the comment/semantic edits made during this session's own re-verification
(`ProbeCompute.comp`'s `local_size_x`, `HelloGame.cpp`'s trailing comment)
were reverted back to byte-identical copies of what PHASE3 originally
committed, then `ProjectAssemblyProbe_Game`/`_Editor` were rebuilt one more
time cleanly (`git status` confirms a clean working tree afterward — no
residual diff from this session's probing). A final live smoke test was
also run (not required by this phase's own Definition of Done, since this
phase writes no runtime-loading code yet, but done anyway as a basic
non-regression check, matching this campaign's own general verification
convention):

- `run_app_background` → `GreatTamanaEditor.exe`
- `GET /get_logs` → clean (only the pre-existing, already-documented demo
  plugin priority-tie-break warnings; no new warnings/errors)
- `GET /get_swapchain` → real 217053-byte PNG, genuine rendered Editor UI,
  no regression
- `stop_app_background` → clean shutdown

## Definition of Done — checklist (this phase file's own list)

- [x] `gte_add_project_shaders()` defined in `cmake/GteProject.cmake`,
      correctly re-bases globbed absolute paths to `CMAKE_SOURCE_DIR`-relative
      before calling `gte_add_shader()`.
- [x] No PHASE3 temporary stub existed to delete.
- [x] The test project's `ProbeCompute.comp` compiles to SPIR-V and stages
      correctly next to `ProjectAssemblyProbe_Game.dll`/`ProjectAssemblyProbe_Editor.dll`,
      in a `shaders/` sub-folder distinct from the engine's own internal one
      (confirmed true for the FIRST/full build of a target — see the new
      gap above for the one real caveat: an incremental, shader-only,
      target-scoped rebuild does not reliably re-stage).
- [x] Incremental rebuild behavior confirmed (recompiles only on real source
      change) — the `glslc` re-invocation itself is correctly, minimally
      incremental; re-confirmed independently this session, twice
      (comment-only and semantic edits).
- [x] The exact staged relative path string is recorded above for PHASE8 to
      reuse verbatim.

## What this phase does NOT do (as instructed)

- Does NOT write any C++ code that loads/uses the compiled `.spv` — entirely
  PHASE8's concern.
- Does NOT add any new shader language feature, `#include` resolution
  behavior, or compilation flag beyond what `gte_add_shader()` already
  provides.
- Does NOT patch `cmake/CompileShaders.cmake`'s missing link-edge dependency
  (the gap found above) — deliberately left as a documented, open finding,
  not a PHASE4-scope fix (see "Disposition" above).

## Result

**PHASE4 is DONE** — its own Definition of Done is fully satisfied, verified
independently and mechanically in this dedicated session (not merely
inherited from PHASE3's own write-up). One inaccurate claim in the prior
PHASE3-session report (that incremental, shader-only rebuilds correctly
re-stage the Project Assembly's own deployed `.spv`) has been corrected here
with concrete, reproduced evidence, and a genuine, previously-undetected gap
in the shared `gte_add_shader()`/`cmake/CompileShaders.cmake` mechanism has
been documented for PHASE8 (and any future maintenance pass) to be aware of.
PHASE8 can build a real `ComputePipeline` from
`"project_assemblies/shaders/ProbeCompute.comp.spv"` (or its own replacement
shader, following the same staging convention) with zero further CMake
wiring needed — but should do a full/clean rebuild of its own Project
Assembly targets after editing shader content, not rely on a target-scoped
incremental build alone, until the dependency-wiring gap above is closed.
