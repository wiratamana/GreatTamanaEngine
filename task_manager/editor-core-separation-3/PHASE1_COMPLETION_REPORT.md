# PHASE1 COMPLETION REPORT — `gte_plugin_abi` Foundation

**Phase file**: `PHASE1_GTE_PLUGIN_ABI_FOUNDATION.md`. **Parent**:
`PHASE0_MASTER_STRATEGY.md`. **Branch**: `feature/editor-core-separation`
(confirmed via `git_status` before starting and unchanged throughout — never
switched).

There was no prior `PHASEn_COMPLETION_REPORT.md` in this campaign folder to
read first — this is the campaign's own first implementation phase.

## Summary — what was actually built

1. **`plugins/gte_plugin_abi/`** (new folder), exactly per Step 3.1:
   - `GtePluginAbiFingerprint.h` — the fixed-size POD fingerprint struct +
     `operator==` (byte-for-byte comparison, `__builtin_memcmp` chosen over
     `<cstring>`'s `std::memcmp` — both equally correct on this GCC/MinGW-only
     repo; `__builtin_memcmp` kept as written in the strategy doc).
   - `GtePluginAbiFingerprintGenerated.h.in` — the `configure_file()` template.
   - `GtePluginModuleInfo.h`, `IPluginModule.h`, `PluginExports.h` — exactly
     as specified.
   - `CMakeLists.txt` — defines the `gte_plugin_abi` `INTERFACE` library,
     `configure_file()`s the generated fingerprint header. **Written with
     Step 3.4 point 5's correction already applied** (reads the real
     `GTE_ENABLE_PLUGINS` **and** the new `GTE_PLUGIN_SHARED_CRT_TOOLCHAIN_SUPPORTED`
     flag — see Deviation #2 below — instead of a hardcoded `1`), since there
     was no reason to write a deliberately-wrong intermediate version first.
   - `PublicSurface.md` — the explicit, reviewed boundary-type list (Step 3.3).
   - **Not yet wired into the real root `CMakeLists.txt` via `add_subdirectory()`**
     — confirmed via direct re-reading of `PHASE2_PLUGIN_HOST_AND_HELLO_WORLD_HANDSHAKE_PROBE.md`
     (Step 3.5/3.6) that PHASE2 is explicitly the phase that adds
     `add_subdirectory(plugins/gte_plugin_abi)` (alongside `demo_hello_world`)
     to the root file, gated by `GTE_ENABLE_PLUGINS`. PHASE1's own job is
     "the files exist and compile in isolation," not "the target exists in
     the main configure" — confirmed this reading is correct rather than
     guessing, per Universal Rule 9.
2. **`docs/conventions/plugin-architecture.md`** (new) — full convention
   write-up mirroring `docs/conventions/logging.md`'s shape/tone, covering
   the fingerprint gate, the curated-wrapper-interface rule, the CRT-linkage
   requirement (including this phase's own real, discovered limitations —
   see Deviations below), where things live physically, and what remains
   deliberately deferred.
3. **`AGENTS.md`** — new `## Plugin Architecture` section added between
   `## gte_core / gte_editor Library Separation` and
   `## Testability & Regression Safety`, matching the file's own
   chronological-by-introduction convention, linking to the new doc.
4. **`docs/README.md`** — added the matching conventions-index entry (not
   explicitly required by the phase file, but every other convention file has
   one — added for index consistency; the entry required one small follow-up
   `edit_line` correction after an initial `edit_line` call miscounted the
   replacement range and duplicated a paragraph — caught immediately by
   re-reading the file, fixed before moving on, verified clean by a final
   full `read_file`).
5. **`cmake/MingwRuntime.cmake`** (new) — `mingw_copy_runtime_dll()` and
   `gte_apply_plugin_shared_crt_linkage()`, **materially corrected from the
   phase file's own sketch** — see Deviation #1 below for the full reasoning;
   also adds a configure-time toolchain-capability probe
   (`GTE_PLUGIN_SHARED_CRT_TOOLCHAIN_SUPPORTED`) the strategy doc's own sketch
   did not anticipate needing (Deviation #2).
6. **Root `CMakeLists.txt`**:
   - New `option(GTE_ENABLE_PLUGINS ... ON)`, added immediately after the
     existing `option(GTE_CORE_STANDALONE_PROBE_ONLY ...)` block (confirmed
     real, current line via `search_in_dir` first — was line 102).
   - New `include(MingwRuntime)`, added immediately after the existing
     `include(CompileShaders)` line (confirmed real, current line 113 first).
   - New `gte_apply_plugin_shared_crt_linkage(GreatTamanaEditor)` call,
     added immediately before the existing `sdl3_copy_runtime_dll(GreatTamanaEditor)`
     line (confirmed real, current line 1203 first — matching the phase
     file's own documented ~1203 estimate exactly).

## Step 3.5 verification (mandated by the phase file)

1. **Re-confirmed today's real baseline** (`objdump -p build/GreatTamanaEditor.exe`)
   before touching anything: imports only `SDL3.dll`/`KERNEL32.dll`/
   `msvcrt.dll`/`SHELL32.dll`/`USER32.dll`/`WS2_32.dll` — zero
   `libstdc++-6.dll`/`libgcc_s_seh-1.dll`/`libwinpthread-1.dll`, exactly as
   the phase file's own Step 2 asserted.
2. **`plugins/gte_plugin_abi`'s own isolation compile check** (Step 3.3):
   built a small, temporary, NOT-committed CMake project
   (`build/_phase1_abi_check/`, deleted after use — outside the repo's own
   tracked tree, `build/` is already gitignored) that `add_subdirectory()`'d
   `plugins/gte_plugin_abi` and compiled a `main.cpp` including every one of
   its headers (including the real, CMake-`configure_file()`-generated
   `gte_plugin_abi/GtePluginAbiFingerprintGenerated.h`) with an include path
   containing **only** this folder + its generated-headers folder. **Result**:
   compiled and linked cleanly; running it printed
   `abiContractGeneration=1 compilerId=GNU compilerVersion=15.2.0
   buildConfig=Unspecified pointerSize=8 sharedRuntimeLinkage=1 selfEqual=1`
   (`sharedRuntimeLinkage=1` in THIS isolated probe only because its own
   scratch `CMakeLists.txt` hardcoded `GTE_ENABLE_PLUGINS=ON` with no
   toolchain-support gate — the REAL `plugins/gte_plugin_abi/CMakeLists.txt`
   correctly also requires `GTE_PLUGIN_SHARED_CRT_TOOLCHAIN_SUPPORTED`, which
   this standalone probe project never defines/includes). Confirms: zero real
   `gte_core`/`gte_editor` header dependency, the `configure_file()`
   mechanism genuinely works, `operator==` is correct, and `list(GET ...)`
   parses this real machine's `CMAKE_CXX_COMPILER_VERSION` (`15.2.0`) cleanly
   with no fewer-than-3-part edge case to worry about.
3. **Incremental targeted rebuild** (`cmake --build build --target GreatTamanaEditor`,
   also re-ran `gte_core`/`gte_editor` — both correctly reported "no work to
   do," confirming this phase touched neither) — after the CRT-linkage code
   landed, this failed once with a real, informative error (`unrecognized
   command-line option '-shared-libstdc++'`), which is Deviation #1 below;
   after the fix, it built/linked cleanly, and `objdump -p` on the resulting
   `.exe` is still **byte-for-byte identical** to the pre-phase baseline
   (same six DLL imports, nothing added) — exactly the "packaging-only,
   behavior-invisible when the toolchain can't do it yet" outcome Deviation #2
   makes an honest, correct result for this exact machine.
4. **Live runtime smoke test**: `run_app_background` the rebuilt
   `GreatTamanaEditor.exe` → `gte_send_request("/get_swapchain")` — screenshot
   confirmed the Editor renders identically to every prior campaign's own
   baseline (Scene/Game/Hierarchy/Inspector panels, sky gradient, dock
   layout) → `gte_send_request("/get_logs?limit=20&min_level=warning")`
   returned zero warnings/errors (`{"count":0,...}`) → `stop_app_background`.
   No `GTE_LOG_*`/console output of any kind was added by this phase (nothing
   in `gte_core`/`gte_editor` was touched), so this step is really confirming
   "nothing regressed," which it does.

## Deviations from the strategy doc (real, discovered, not silently smoothed over)

### Deviation #1 — `-shared-libstdc++` is not a real GCC/G++ command-line option

The phase file's own Step 3.4 code sketch calls
`target_link_options(${target_name} PRIVATE -shared-libgcc -shared-libstdc++)`.
Mechanically confirmed, on the real, installed toolchain: `g++
-shared-libstdc++ ...` is rejected outright —
`g++.exe: error: unrecognized command-line option '-shared-libstdc++'; did
you mean '-static-libstdc++'?` — reproduced identically on a SECOND,
independently-installed GCC 16.2.0 toolchain too (see Deviation #2), so this
is not specific to one broken install; it is a general fact about GCC/G++'s
own driver: `-shared-libgcc` is a real, valid, symmetric option, but there is
no `-shared-libstdc++` counterpart. Confirmed by direct experiment (a
trivial `iostream`/`vector`/exception-using program compiled with **zero**
special flags on a `--enable-shared`-built toolchain already imports
`libstdc++-6.dll`/`libgcc_s_seh-1.dll` — shared linkage is that toolchain's
own **default**; adding `-static-libgcc -static-libstdc++` is what flips it
to static; there is no flag that forces the opposite direction beyond simply
not passing those two). `cmake/MingwRuntime.cmake`'s
`gte_apply_plugin_shared_crt_linkage()` now passes only the real
`-shared-libgcc` flag and relies on shared libstdc++ being the toolchain's
own default when eligible (see `cmake/MingwRuntime.cmake`'s own extensive
top-of-file comment for the full evidence trail). This is a real, necessary
functional correction, not a stylistic one — the strategy doc's own literal
snippet would have failed to configure at all, for every future phase, on
any real GCC/MinGW toolchain.

### Deviation #2 — this repository's own default toolchain cannot produce a shared-CRT-linked binary at all, under any flags

Mechanically confirmed via `g++ -v`: the only MinGW toolchain installed on
this development machine at the start of this phase (scoop's `gcc` package,
`15.2.0`, `x86_64-w64-mingw32`) was itself configured
`--build=... --disable-shared ...` — it has **no** shared
libstdc++/libgcc/libwinpthread variant at all; `scoop search mingw` +
`scoop search gcc` confirmed no same-version (`15.2.0`) shared-runtime
alternative exists anywhere in scoop's own buckets. This is a genuine,
unanticipated environmental blocker the strategy doc's own Step 2 investigation
did not discover (it only checked the CURRENT build's own import table via
`objdump`, which correctly showed zero runtime-DLL dependency, but never
checked WHETHER the toolchain that produced it could even build the
alternative). Per the phase file's own explicitly-stated instruction ("If
`-shared-libgcc -shared-libstdc++` causes any REAL, existing test or the main
executable to fail to link/run for a reason unrelated to the three staged
DLLs ... stop and ask before working around it silently"), this was raised
via **two** separate `ask_questions` round-trips rather than guessed:

1. First round: confirmed the blocker's exact shape (no shared-runtime GCC of
   any version available at all) and asked how to proceed. **Answer**: "install
   necessary components with scoop (must use scoop), keep it minimalist, no
   Visual Studio, limited storage."
2. Second round (a deeper wrinkle discovered while acting on the first
   answer): the only shared-runtime-capable options in scoop (`mingw`,
   `mingw-winlibs`) are GCC `16.2.0` — a different major version from this
   repo's already-built `15.2.0` — meaning actually USING one for real means
   switching `CMAKE_CXX_COMPILER` for the WHOLE project, which forces a de
   facto full reconfigure+rebuild of literally everything (`gte_core`/
   `gte_editor`/every `third_party` library), directly conflicting with this
   phase's own "no full build except PHASE6" rule. **Answer**: install the
   toolchain now (so it's on disk and ready), but do **not** switch the
   active compiler in PHASE1 — make
   `gte_apply_plugin_shared_crt_linkage()` defensive (probe support at
   configure time, clean no-op + warn today) and leave the actual
   toolchain-swap+rebuild as an explicitly documented, deferred decision for
   a dedicated later step.

**What was actually done as a result**:

- `scoop install mingw` (mingw-builds-binaries,
  `x86_64-16.2.0-release-posix-seh-ucrt-rt_v14-rev1`, ~103 MB — the smallest
  of the two shared-capable candidates found, matching "keep it minimalist")
  — confirmed, by direct experiment, to genuinely default to shared
  libstdc++/libgcc linkage (`libstdc++-6.dll`/`libgcc_s_seh-1.dll` present
  next to its own `g++.exe`, and a trivial C++-feature-using test program
  compiled with it, with zero flags, imports both). Installed and left on
  disk, entirely unused by the actual repository configure — this repo's
  `CMakeLists.txt` never references it and `build/CMakeCache.txt`'s own
  `CMAKE_CXX_COMPILER` still points at the original scoop `gcc` (`15.2.0`)
  toolchain, unchanged, so this install has **zero** effect on the existing
  build tree today.
- `cmake/MingwRuntime.cmake` gained a real, configure-time probe: it checks
  whether `libstdc++-6.dll` exists next to the ACTIVE `CMAKE_CXX_COMPILER`
  (`GTE_PLUGIN_SHARED_CRT_TOOLCHAIN_SUPPORTED`, a `CACHE INTERNAL` bool) —
  when it does not (true for this repo's real, current configure),
  `gte_apply_plugin_shared_crt_linkage()` becomes a single, clear, per-target
  `message(WARNING ...)` no-op instead of emitting a flag that would either do
  nothing meaningful or (worse) silently claim a linkage mode that was never
  actually achieved. `plugins/gte_plugin_abi/CMakeLists.txt`'s own
  `GTE_PLUGIN_SHARED_RUNTIME_LINKAGE_INT` now requires **both**
  `GTE_ENABLE_PLUGINS` **and** `GTE_PLUGIN_SHARED_CRT_TOOLCHAIN_SUPPORTED`
  before claiming `1`, so the fingerprint's `sharedRuntimeLinkage` field is
  always honest for whatever toolchain a given machine/configure actually
  uses — directly satisfying Locked Design Decision #4's own "never claim
  shared linkage a given configuration didn't actually request" rule, just
  against a REAL discovered failure mode the strategy doc itself didn't
  anticipate, not only the `GTE_ENABLE_PLUGINS=OFF` case it explicitly named.
- Confirmed, live, that this makes the real build's own behavior a clean,
  single, well-explained warning (not three confusing repeated "DLL not
  found" warnings, which is what the phase file's own literal, unconditional
  `mingw_copy_runtime_dll()` call would have produced every configure from
  now on) and that `GreatTamanaEditor` still builds/links/runs exactly as
  before — see Step 3.5 evidence above.

**What remains genuinely open, for a future phase/dedicated task, stated
honestly**: the plugin ABI boundary's shared-CRT requirement is designed and
ready (the reusable CMake helper is real and correctly wired), but **not yet
proven to actually work end-to-end** on this machine — that requires a real,
deliberate decision to switch this repository's own `CMAKE_CXX_COMPILER` to
a shared-runtime-capable toolchain and accept the resulting full rebuild,
which this phase's own rules forbid it from doing. PHASE2 (which needs
`PluginHost` to actually `LoadLibraryW()` a real plugin `.dll`) will hit this
same fact again — the demo plugin `.dll` and the host `.exe` will both
compile and even load/handshake successfully today (the fingerprint
mismatch-detection path itself doesn't care whether `sharedRuntimeLinkage`
is 0 or 1, it just compares byte-for-byte), but they will BOTH have
`sharedRuntimeLinkage=0` baked in (since neither achieves real shared
linkage on this machine yet) — which is internally consistent and safe (a
false-negative-free comparison), just not yet the "real, working shared
heap" milestone the design ultimately wants. This is flagged here explicitly
so PHASE2 doesn't waste time re-diagnosing the same root cause.

### Minor note — `GtePluginAbiFingerprintGenerated.h`'s real include path

The phase file's own PHASE2 sketch writes
`#include "../../../plugins/gte_plugin_abi/GtePluginAbiFingerprintGenerated.h"`
from `src/Core/Plugins/PluginHost.cpp`. Given this phase's own `CMakeLists.txt`
(Step 3.1's own literal code block, implemented unmodified) configures the
generated header to
`${CMAKE_CURRENT_BINARY_DIR}/generated/gte_plugin_abi/GtePluginAbiFingerprintGenerated.h`
and adds `${CMAKE_CURRENT_BINARY_DIR}/generated` (not `.../generated/gte_plugin_abi`)
as the include directory, the correct consumption spelling is
`#include "gte_plugin_abi/GtePluginAbiFingerprintGenerated.h"` (relying on
the INTERFACE include path), not a `../../../plugins/gte_plugin_abi/...`
relative-file-path spelling, which resolves to a location in the SOURCE tree
where no such generated file actually exists. Verified directly during this
phase's own isolation compile check (Step 3.3) — the literal relative-path
spelling was tried first and failed with "No such file or directory";
switching to the `gte_plugin_abi/...` spelling (matching the real configured
include directories) fixed it immediately. Flagged here for PHASE2's own
benefit, since its own phase file's `PluginHost.cpp` sketch would hit this
exact same error if copied verbatim — not fixed in that phase file itself
(out of scope for PHASE1 to edit another phase's file), just recorded as a
discovered fact per this campaign's own Universal Rule 9.

## Files added/changed

- `plugins/gte_plugin_abi/GtePluginAbiFingerprint.h` (new)
- `plugins/gte_plugin_abi/GtePluginAbiFingerprintGenerated.h.in` (new)
- `plugins/gte_plugin_abi/GtePluginModuleInfo.h` (new)
- `plugins/gte_plugin_abi/IPluginModule.h` (new)
- `plugins/gte_plugin_abi/PluginExports.h` (new)
- `plugins/gte_plugin_abi/CMakeLists.txt` (new)
- `plugins/gte_plugin_abi/PublicSurface.md` (new)
- `docs/conventions/plugin-architecture.md` (new)
- `cmake/MingwRuntime.cmake` (new)
- `AGENTS.md` (modified — new `## Plugin Architecture` section)
- `docs/README.md` (modified — new conventions-index entry)
- `CMakeLists.txt` (modified — `GTE_ENABLE_PLUGINS` option, `include(MingwRuntime)`,
  `gte_apply_plugin_shared_crt_linkage(GreatTamanaEditor)` call)

## Non-Goals honored (per the phase file's own list)

No `PluginHost` class was created. No `LoadLibrary()` call exists anywhere.
No demo plugin project exists yet. All three are explicitly PHASE2's job.

## Compile-check summary (for the record)

- `gte_core`: "no work to do" (untouched by this phase).
- `gte_editor`: "no work to do" (untouched by this phase).
- `GreatTamanaEditor`: rebuilt successfully; `objdump -p` shows a byte-for-byte
  identical import table to the pre-phase baseline; live `run_app_background`
  + `GET /get_swapchain` + `GET /get_logs` smoke test passed with zero
  warnings/errors logged.
- `gte_plugin_abi` (isolated, temporary probe project, deleted after use):
  compiled, linked, and ran successfully with the expected fingerprint
  values.
