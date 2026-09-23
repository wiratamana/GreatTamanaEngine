# `gte_core` Standalone-Build Probe

## What this is

A tiny, **manually-invocable** local CMake project. It is NOT a real CI
pipeline (this repository has none of its own - only vendored
`third_party/**/.github` workflows exist, which belong to third-party
dependencies, not this project - see `PHASE0_MASTER_STRATEGY.md`'s Locked
Design Decision #5). It configures and builds **only** the `gte_core` static
library target from the real repository root `CMakeLists.txt` - no
`gte_editor`, no main executable, no test suite, no ImGui/ImGuizmo/SDL3
fetching at all.

This is the ONLY place in the whole project where `gte_editor` is
intentionally not configured. Every other build of this repository always
links `gte_editor` alongside `gte_core` into one executable/test binary, which
means an accidental `gte_core -> gte_editor` dependency would otherwise never
be caught by a normal build (see the design doc's Section 7.3: *"a plain local
`cmake --build .` never even attempts to link `gte_core` standalone"*). This
probe exists purely to catch that class of regression mechanically, on
demand.

## How it works

It does not hand-duplicate `gte_core`'s own ~230-file `target_sources()` list
(which would silently drift out of sync with the real `CMakeLists.txt` the
moment a future phase adds/removes a file). Instead, this folder's own
`CMakeLists.txt` `add_subdirectory()`s the real repository root, forcing two
cache variables before doing so:

- `GTE_CORE_STANDALONE_PROBE_ONLY=ON` - the root `CMakeLists.txt`'s own
  `if(NOT GTE_CORE_STANDALONE_PROBE_ONLY)` guards (added by this same phase)
  skip `gte_editor`'s `add_library()`, the `GreatTamanaEditor` executable and
  every one of its `gte_add_shader()`/`sdl3_copy_runtime_dll()` calls, and the
  `fetch_sdl3()`/`fetch_imgui()`/`fetch_imguizmo()` calls entirely.
- `GTE_BUILD_TESTS=OFF` - skips `GreatTamanaEngineTests`/GoogleTest entirely.

What remains is exactly `gte_core`'s own `add_library(gte_core STATIC ...)`
target, its `target_link_libraries()` (`volk`/`vma`/`stb_image`/
`stb_image_write`/`KTX::ktx`/`httplib`/`nlohmann_json`/`Threads::Threads`/
`saba_pmx` - never `SDL3::SDL3`, `gte_editor`, `imgui`, or `imguizmo`), and its
handful of always-on `target_compile_definitions()`.

## Exact command to run this by hand

From the repository root:

```
cmake -S tools/ci/gte_core_standalone_probe -B build-core-probe
cmake --build build-core-probe --target gte_core
```

(`--target gte_core` is not strictly required - `gte_core` is the only real
target this configuration defines besides third-party dependency targets
`volk`/`vma`/`KTX::ktx`/etc, so a plain `cmake --build build-core-probe` builds
it anyway. Passing it explicitly is just extra clarity about intent.)

## What a successful run proves

- `gte_core.a` (`libgte_core.a` on this toolchain) compiles and links as a
  genuinely standalone static library - zero `gte_editor` involvement, zero
  ImGui, zero SDL3 (neither fetched nor linked), zero debug-only Editor
  feature code (design doc Section 1.3, Rule 1).
- Every one of `gte_core`'s own `.cpp`/`.h` files still compiles with
  **literally no `gte_editor` symbols resolvable at all** in this
  configuration - a genuine `gte_core -> gte_editor`-only-symbol dependency
  (the exact hazard Section 7.3 of the design doc describes) would fail this
  probe's link step immediately, on the very next run, rather than silently
  continuing to "work" forever inside the main build (which always links both
  archives together).

## What this probe deliberately does NOT do

- It does not build any part of the actual Player Build Pipeline (design doc
  Section 8) - that is a separate, later initiative, out of scope for this
  entire campaign.
- It is not wired into any GitHub Actions workflow or other real CI system -
  none exists in this repository (Locked Design Decision #5). Run it by hand,
  as documented above, whenever you want to re-confirm the one-way dependency
  boundary still holds.
- It never appears as a user-facing option inside the main build - do not set
  `GTE_CORE_STANDALONE_PROBE_ONLY` manually in a normal
  `cmake -S . -B build` configure.
