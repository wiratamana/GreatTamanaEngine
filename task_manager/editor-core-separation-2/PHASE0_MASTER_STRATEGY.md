# PHASE0 — MASTER STRATEGY: Actually Making `gte_core.a` Link Standalone

Campaign folder: `task_manager/editor-core-separation-2/`
Predecessor campaign (READ THIS FIRST, in full, before touching any phase):
`task_manager/editor-core-separation-1/PHASE0_MASTER_STRATEGY.md` and, most
importantly, `task_manager/editor-core-separation-1/CAMPAIGN_COMPLETION_REPORT.md`
— that report is the honest, evidence-based reason this second campaign
exists at all. Read its "Honest summary" and "What remains genuinely open"
sections before doing anything else.

This file is the ORCHESTRATOR. Every child phase file (`PHASE1_*.md` ..
`PHASE5_*.md`) refers back to this file as its Parent. Read this file in full
before starting any child phase.

Branch: stay on `feature/editor-core-separation`. Never switch branches.

---

## Step 1: The Goal (Where are we going?)

`editor-core-separation-1` split the engine into two CMake static-library
targets, `gte_core.a` (engine only) and `gte_editor.a` (ImGui/gizmos/Frame
Debugger/panels, depends on `gte_core.a`, one-way). That campaign's own
Phase 19 closeout report is brutally honest that **3 of its own 4 "Hard
Rules" are still false today**, and traced all three failures back to
exactly the same root cause, restated below.

**The goal of THIS campaign is narrow and concrete**: make all four of the
original Hard Rules mechanically, provably true, by fixing the three real,
named code defects that make them false, and by replacing the one
verification tool (`tools/ci/gte_core_standalone_probe`) that was proven
structurally incapable of ever catching this class of bug with one that
actually can.

### The Four Hard Rules (verbatim from `editor-core-separation-1`, Section 1.3) — this campaign's Definition of Done

1. `gte_core.a` compiles and links standalone with literally zero editor
   code, zero ImGui, zero debug-only feature code, zero SDL headers in its
   own translation units — checked mechanically.
2. `gte_editor.a` may depend on `gte_core.a`. Never the reverse — true in
   `#include` terms, `target_link_libraries()` terms, AND in the "no
   unresolved external symbol only `gte_editor.a` defines" sense.
3. A thin Player-build host (never having seen `gte_editor.a`'s source) can
   link `gte_core.a` alone and get a running, renderable engine.
4. `gte_editor.a` is unconditionally configured every time this repo is
   configured — no CMake option/macro anywhere skips building it (except the
   already-sanctioned, narrow `GTE_CORE_STANDALONE_PROBE_ONLY` CI escape
   hatch — unchanged, out of scope, see Non-Goals below).

**This campaign's Definition of Done**: run a real `cmake --build`
that links a real, standalone executable against `gte_core.a` alone (no
`gte_editor.a` in the link line at all) that calls into `RenderSystem::Draw()`
and constructs a `Network::NetworkServer`, and it **succeeds**, with a real,
mechanically-produced, checked-into-this-repo probe project proving it —
forever, not just "as of this one manual phase-19 experiment".

---

## Step 2: The Situation / The Problem (Where are we now?)

Re-confirmed by fresh, direct reading of the real, current source during this
campaign's own investigation (not merely inherited from the prior campaign's
claims) — every one of the following is still true today, on this exact
branch:

### Defect A — `RenderSystem::Draw()` calls a free function only `gte_editor.a` defines

`src/Game/RenderSystem.cpp` (compiled into `gte_core`) calls
`RecordFrameDebuggerDraws(*capture, ...)` — a free function DECLARED in
`src/Game/RenderSystem.h` (core) but DEFINED only in
`src/Editor/FrameDebuggerDrawRecording.cpp` (editor). `capture`'s static
type, `FrameDebuggerCaptureContext*`, is a bare forward declaration in
core-tier code — legal to pass around and null-check, but the moment
`RenderSystem.cpp` calls the free function BY NAME, the linker needs that
symbol resolved. A `gte_core`-alone link fails with `undefined reference to
gte::RecordFrameDebuggerDraws(...)`. **This exact failure was mechanically
reproduced** by `editor-core-separation-1`'s own Phase 19 throwaway link
probe.

### Defect B — `Core::BuildFrame()` calls a SECOND free function only `gte_editor.a` defines (a genuinely NEW finding this campaign's own investigation surfaced — `editor-core-separation-1`'s Phase 19 never caught it)

`src/Core/Core.cpp` (compiled into `gte_core`) calls
`AddFrameDebuggerReplayPasses(b, m_game, m_renderer, ..., *frameDebuggerCapture)`
— ANOTHER free function, declared in `src/Application/RenderPasses.h` (core)
but defined only in `src/Editor/FrameDebuggerReplayPasses.cpp` (editor). This
is the exact same class of bug as Defect A, on the exact same
`frameDebuggerCapture` pointer, just one call-site deeper in the same
feature. `editor-core-separation-1`'s Phase 19 throwaway link probe only
forced `RenderSystem.cpp.obj` into its test link (it called
`RenderSystem::Draw()` directly) — it never pulled in `Core.cpp.obj`, so it
never actually exercised this second, equally-real undefined reference. Any
executable that actually calls `Core::BuildFrame()` (i.e. any real running
engine, Player host included) hits this the moment
`m_editorLayer != nullptr` is ever true and the Frame Debugger replay path
is armed — but even for a Player host where `m_editorLayer` is always
`nullptr` (so the branch never executes), the SYMBOL must still exist in the
final link, or the link fails outright, regardless of whether the branch
ever runs at runtime. **Confirmed by direct reading of `Core.cpp` lines
830-846 during this campaign's own investigation.**

### Defect C — three real `#include` violations of "`gte_core` never includes anything under `src/Editor/` except `EditorLayer.h`"

Re-confirmed today, exactly as `editor-core-separation-1` Phase 19 already
found and left open:
- `src/Network/NetworkRoutes.h` → `#include "../Editor/EditorPanelCatalog.h"`
- `src/Network/NetworkRoutes.cpp` → `#include "../Editor/Logger.h"` (used
  only for the compile-time constant `Logger::kCapacity`, to clamp
  `GET /get_logs`'s `limit` query parameter — no link-time hazard from this
  one specific use, since it's a `static constexpr`, but it is still a real,
  live violation of the "never `#include` anything under `src/Editor/`
  except `EditorLayer.h`" rule, and it drags in `Logger`'s FULL class
  declaration, including its runtime methods, for zero reason).
- `src/Network/NetworkServer.cpp` → `#include "../Editor/Logger.h"`, this one
  WITH a genuine link-time hazard: it calls `Logger::Query()`, `Logger::Clear()`,
  `Logger::EntryCount()`, `Logger::IsEnabled()`, `Logger::LatestEntryId()` —
  five real, `gte_editor`-only static methods, directly, from
  `GET /get_logs`/`POST /clear_logs` route handlers compiled into
  `gte_core`.

`EditorPanelCatalog.h` itself is header-only (no `.cpp`, so it causes no
*link*-time hazard on its own — a header with no matching translation unit
simply compiles wherever it's included) but its physical location under
`src/Editor/` is itself the architectural violation the design explicitly
disallows (only `EditorLayer.h` is the one documented exception) — and today
it is compiled directly into `gte_core`'s own `target_sources()` list
already (see root `CMakeLists.txt` line ~431), which only makes its
`src/Editor/` address more clearly wrong, not less.

### The build system's own tell: `$<LINK_GROUP:RESCAN,gte_editor,gte_core>`

Both `CMakeLists.txt` (the `GreatTamanaEditor` executable target) and
`tests/CMakeLists.txt` (the `GreatTamanaEngineTests` target) link
`gte_editor`/`gte_core` through a `$<LINK_GROUP:RESCAN,...>` generator
expression instead of a plain `target_link_libraries(... gte_editor)` —
because a plain, single-pass link genuinely fails today: GNU `ld` only scans
each static archive once by default, and without the rescan, the two real
undefined references (Defects A and B) never get a second chance to resolve
against `gte_editor.a`'s own archive. **This is a live, self-documenting
symptom of Defects A and B still being real** — and is this campaign's own
best, free, mechanical regression check: once Defects A and B are actually
fixed, a PLAIN single-pass link must start succeeding, with the `RESCAN`
generator expression removed outright (Phase 4 does this, deliberately, as a
correctness proof, not an optimization).

### Why the existing standalone-core probe (`tools/ci/gte_core_standalone_probe`) can never catch any of this

`editor-core-separation-1`'s own Phase 19 already discovered and documented
this precisely: that probe configures and BUILDS `gte_core` as a static
archive ONLY — it never links an executable. Archiving object files
(`ar`/CMake's static-library step) never resolves symbols across translation
units; an archive can contain any number of internally-unresolved symbol
references and still "build" successfully. The probe is therefore
structurally blind to exactly the class of bug this campaign fixes. It
remains useful for what it WAS built for (catching a missing-`#include`
type violation, e.g. an accidental `#include <imgui.h>` inside `gte_core`)
— it is not replaced, only supplemented (Phase 4 adds a second, new probe
project specifically for the link-level check; see that phase for why a
second project, not a rewrite of the first, is correct).

---

## Step 3: The Plan — Locked Design Decisions (do not re-litigate these)

1. **The Frame Debugger capture pointer becomes a real, gte_core-owned,
   abstract interface — not a bigger free-function inventory.** A new pure
   interface, `gte::IFrameDebuggerCaptureRecorder`
   (`src/Core/FrameDebuggerCaptureRecorder.h`), declares exactly the two
   operations `gte_core`-tier code needs to perform against an armed capture
   context: `RecordFrameDebuggerDraw(...)` (replaces the free function
   `RecordFrameDebuggerDraws()`) and `AddReplayPasses(...)` (replaces the
   free function `AddFrameDebuggerReplayPasses()`). `FrameDebuggerCaptureContext`
   (`src/Editor/FrameDebuggerCapture.h`, `gte_editor`-only) implements this
   interface — it keeps every one of its own existing public methods
   (`RecordDraw()`, `RecordEntityDraw()`, `Reset()`, `DrawRecords()`,
   `SetReplayStepPreviews()`, ...) completely unchanged; it merely gains two
   new `override` methods whose bodies are the exact, unmodified logic the
   two free functions used to contain. Every `gte_core`-tier file that
   currently threads a bare `FrameDebuggerCaptureContext*` pointer through
   (`Core.h/.cpp`, `RenderPasses.h/.cpp`, `Game.h/.cpp`, `RenderSystem.h/.cpp`,
   and `EditorLayer.h`'s own `PrepareFrameDebuggerCaptureContext()` return
   type — the one documented `src/Editor/` header `gte_core` may include)
   changes that pointer's STATIC TYPE to `IFrameDebuggerCaptureRecorder*`
   instead. This is a straight, mechanical, behavior-preserving rename at
   every pass-through site, plus two real call-site conversions (a direct
   free-function call becomes a virtual-dispatch method call through the
   pointer) — see PHASE2 for the exhaustive file-by-file list.
   **Why this, and not "add another IEditorLayer method"**: the
   `FrameDebuggerCaptureContext*` pointer is threaded FOUR call-levels deep
   (`Core.cpp` → `RenderPasses.cpp` → `Game.cpp` → `RenderSystem.cpp`) before
   `RecordFrameDebuggerDraws()` is ever called — `RenderSystem.cpp` has no
   access to `IEditorLayer*` at all, so a new `IEditorLayer` method could
   never reach that deep call site. The interface-pointer approach fixes
   both Defect A (four levels deep) and Defect B (one level deep, directly
   in `Core.cpp`) with exactly the same mechanism, since both defects are
   really the same root cause: a concrete, `gte_editor`-only type's free
   function being called by name from `gte_core`-tier code.
2. **The direct `Logger::*` dependency becomes a Bucket-B-style capability
   interface, mirroring `ISceneIOCapability` exactly.** A new
   `ILogQueryCapability` interface, declared next to `ISceneIOCapability` in
   `src/Core/EditorCapabilities.h`, with methods mirroring `Logger::Query()`/
   `Clear()`/`EntryCount()`/`IsEnabled()`/`LatestEntryId()` one-for-one. A new
   `EditorLogQueryCapability` (`src/Editor/EditorLogQueryCapability.h/.cpp`,
   `gte_editor`) implements it by delegating to the real `Logger` class,
   mirroring `EditorSceneIOCapability`'s own shape exactly. `NetworkServer`
   gains a SIXTH defaulted, nullable, non-owning constructor pointer
   parameter (`ILogQueryCapability* logQueryCapability = nullptr`), following
   the exact same pattern its existing five bridge pointers already
   establish. `EditorHost` wires the one real instance in, the same way it
   already wires `EditorSceneIOCapability` (a function-local `static`,
   Meyers-singleton-style, since this class is pure delegation with no state
   of its own). `NetworkServer.cpp`'s `GET /get_logs`/`POST /clear_logs`
   route handlers call through the capability pointer instead of `Logger::`
   directly, with a null-check degrading to a safe, documented fallback
   (mirrors every other bridge's existing "nullptr → 503" convention) instead
   of ever crashing.
3. **`NetworkRoutes.cpp`'s compile-time-only `Logger::kCapacity` use is
   fixed by relocating the constant, not by adding a capability method for
   it.** `Logger::kCapacity`'s literal value (`2000`) moves into
   `src/Core/Logging.h` as a new `inline constexpr std::size_t
   kLogCapacity = 2000;`; `Logger::kCapacity` becomes
   `static constexpr std::size_t kCapacity = kLogCapacity;` (unchanged value,
   unchanged call sites everywhere else that already say `Logger::kCapacity`).
   `NetworkRoutes.cpp` drops its `#include "../Editor/Logger.h"` entirely and
   uses `kLogCapacity` directly from `Core/Logging.h` (already included by
   every file in this codebase that needs `GTE_LOG_*`).
4. **`EditorPanelCatalog.h` physically relocates from `src/Editor/` to
   `src/Core/`** — a pure `git mv` plus a mechanical `#include` path fix at
   every call site (`NetworkRoutes.h`, `DockLayout.cpp`) and a mechanical
   doc-comment path fix everywhere else that merely MENTIONS its path in a
   comment (`DockLayout.h`, `EditorLayer.h`, `Panels/FrameDebuggerPanel.h`,
   `NetworkServer.cpp`, `Application/EditorUiCommandBridge.h`,
   `tests/Editor/EditorPanelCatalogTests.cpp`,
   `tests/Network/ActivateTabEndpointEndToEndTests.cpp`). Zero behavior
   change — same namespace, same symbol names, same content, byte-for-byte,
   only its own file path (and every `#include`/comment referencing that
   path) changes. This is genuinely the lowest-risk phase in this whole
   campaign — see PHASE1.
5. **The two free functions this campaign removes
   (`RecordFrameDebuggerDraws()`, `AddFrameDebuggerReplayPasses()`) are
   DELETED outright, not left behind as dead code.** Their bodies do not
   disappear — they become `FrameDebuggerCaptureContext`'s own two new
   `override` method bodies, physically still defined in the same two
   `.cpp` files they already live in today
   (`src/Editor/FrameDebuggerDrawRecording.cpp`,
   `src/Editor/FrameDebuggerReplayPasses.cpp` — kept, renamed in content
   only, not renamed as files, to minimize `CMakeLists.txt` churn), just
   spelled as `void FrameDebuggerCaptureContext::RecordFrameDebuggerDraw(...)`
   instead of a free function.
6. **The `$<LINK_GROUP:RESCAN,gte_editor,gte_core>` generator expression is
   REMOVED outright, in both `CMakeLists.txt` and `tests/CMakeLists.txt`,
   replaced with a plain `target_link_libraries(... gte_editor)`, as the
   very last code change of this campaign (Phase 4), specifically BECAUSE it
   is this campaign's own best, free, mechanical proof that Defects A and B
   are genuinely gone** — if any gap were somehow missed, this one-line
   change would immediately fail to link with the exact same
   `undefined reference` error the prior campaign's Phase 19 already
   reproduced once, catching a regression immediately rather than shipping
   it silently.
7. **A second, NEW, permanent CI-style probe project,
   `tools/ci/gte_core_player_link_probe/`, is added — it does NOT replace
   `tools/ci/gte_core_standalone_probe/`, it supplements it.** The existing
   probe stays exactly as-is (it still correctly catches a missing-`#include`-
   style violation). The new probe is a tiny, throwaway-style, but
   PERMANENTLY CHECKED-IN `main.cpp` that `#include`s a small number of real
   `gte_core` headers (enough to force `RenderSystem.cpp.obj`, `Core.cpp.obj`,
   and `Network/NetworkServer.cpp.obj` to be pulled from `libgte_core.a`),
   constructs a `gte::Network::NetworkServer` and calls
   `gte::RenderSystem::CollectRenderables()`/`Draw()`-reachable code, and is
   linked against `gte_core.a` **alone** — no `gte_editor`, no ImGui, no SDL,
   no `$<LINK_GROUP:RESCAN,...>` — exactly the shape
   `editor-core-separation-1`'s own Phase 19 already proved (via a throwaway,
   never-committed probe) is the ONLY mechanism that can actually catch this
   class of bug. This campaign commits that probe permanently, so it can be
   re-run by any future phase/campaign, forever, instead of being
   reinvented by hand each time someone worries about this again. See
   PHASE4 for its exact shape.
8. **`GTE_ENABLE_PROJECT_PANEL`/`GTE_ENABLE_PROFILER`/`GTE_ENABLE_JOB_SYSTEM`/
   `GTE_ENABLE_NETWORK`/`GTE_CORE_STANDALONE_PROBE_ONLY` are all completely
   untouched, orthogonal switches** — exactly as `editor-core-separation-1`
   already locked. Nothing in this campaign changes any of them.
9. **Every new interface/class follows the exact same conventions the prior
   campaign already established** (see `AGENTS.md`): `namespace gte { ... }`,
   RAII, a pure-virtual interface has NO data members and a
   `virtual ~Interface() = default;`, a concrete adapter class mirrors
   `EditorSceneIOCapability`'s own header/`.cpp` split (declaration-only
   header, zero dependency on the thing it wraps; the real dependency lives
   only in the `.cpp`).

---

## Step 3 (continued) — Phase Sequence and Dependency Order

Execute STRICTLY in this order — each phase assumes every earlier phase is
already done.

| # | File | One-line summary | Risk |
|---|------|-------------------|------|
| 1 | `PHASE1_EDITORPANELCATALOG_RELOCATION.md` | Move `EditorPanelCatalog.h` from `src/Editor/` to `src/Core/`; fix every `#include`/comment reference | Low |
| 2 | `PHASE2_FRAME_DEBUGGER_CAPTURE_RECORDER_INTERFACE.md` | New `IFrameDebuggerCaptureRecorder` interface; convert every pass-through call site; delete both free functions; fixes Defects A and B | **High — the core of this campaign** |
| 3 | `PHASE3_LOG_QUERY_CAPABILITY_AND_NETWORKROUTES_CLEANUP.md` | New `ILogQueryCapability`/`EditorLogQueryCapability`; wire into `NetworkServer`/`EditorHost`; relocate `Logger::kCapacity`'s value into `Core/Logging.h`; fixes Defect C | Medium |
| 4 | `PHASE4_LINK_GROUP_REMOVAL_AND_PLAYER_LINK_PROBE.md` | Remove `$<LINK_GROUP:RESCAN,...>` from both CMake targets; add the new permanent `gte_core_player_link_probe` executable-link probe | Medium-High (this is the phase that PROVES the fix) |
| 5 | `PHASE5_FINAL_REGRESSION_VERIFICATION_AND_CAMPAIGN_CLOSEOUT.md` | Full clean build + full `ctest`; re-run both probes; live HTTP smoke test via `gte_send_request`; re-check the design doc's Four Hard Rules for real; `CAMPAIGN_COMPLETION_REPORT.md` | Low (verification only, but the campaign's own mandatory final checkpoint) |

---

## Universal Rules Every Phase Must Follow

1. **Read this file (Parent) in full before starting.** Read every
   `PHASEn_COMPLETION_REPORT.md` left by a prior phase in this same folder —
   it may contain a clue, a deviation, or a discovered fact the next phase
   needs.
2. **Stay on branch `feature/editor-core-separation`.** Never switch
   branches.
3. **Namespace/RAII/Clean-Architecture conventions from `AGENTS.md` apply to
   every new file.**
4. **No full build/full `ctest` except PHASE5's own final checkpoint.** Every
   other phase does an incremental compile check only (e.g.
   `cmake --build build --target gte_core`, then
   `cmake --build build --target gte_editor`, then, only once both compile
   cleanly, `cmake --build build --target GreatTamanaEditor` — do NOT attempt
   the full `GreatTamanaEditor`/`GreatTamanaEngineTests` link until the
   `gte_core`/`gte_editor` targets themselves compile, since the
   `$<LINK_GROUP:RESCAN,...>` workaround is still in place through PHASE3 and
   will keep masking/unmasking link errors until PHASE4 removes it on
   purpose) plus a quick `run_app_background` + `gte_send_request`
   visual/log smoke check where relevant.
5. **Use the engine's own logging + `gte_send_request` for debugging.** Never
   add `std::cout`/`printf`/`OutputDebugString` debug prints — use
   `GTE_LOG_*` and pull logs back via `GET /get_logs` (once PHASE3 lands,
   this route itself is one of the very things this campaign fixes — so
   PHASE1/PHASE2 must debug via the OLD, still-working direct-`Logger::`
   code path; only PHASE3 onward can assume the new capability path is live).
6. **Every phase must end with**: a compile-check result, a
   `PHASEn_COMPLETION_REPORT.md` written into this same folder, and a git
   commit (`git_add` + `git_commit`) of both the code changes and that
   report.
7. **Every phase-implementation task must call `ask_questions`** if it hits
   a genuine ambiguity this strategy doc does not resolve — do not guess
   silently on anything architecturally significant. Every task delegated
   from this campaign (including any task THAT task itself further
   delegates, if ever allowed to) must also be instructed to use
   `ask_questions` under the same rule.
8. **Implementation phases (PHASE1-PHASE5) must NOT call `delegate_task`.**
   Only the orchestrating session (the one that created this campaign's own
   strategy files) is allowed to delegate. A phase that discovers it is too
   large mid-flight should say so honestly in its completion report rather
   than spawning sub-tasks itself.
9. **If a phase's own exact file paths/line numbers differ from what this
   strategy assumed** (real source drifts between when this strategy was
   written and when a phase actually runs), use `search_in_dir` to
   re-confirm the real, current location before editing — never edit blind
   against a guessed path/line number.
10. **Use the engine's network debugging features proactively.** This
    engine exposes `GET /get_swapchain`, `GET /get_game_view`,
    `GET /get_logs`, `GET /list_tabs`, `GET /frame_debugger/*`, etc. (see
    `AGENTS.md`, "Networking") — after any phase that touches a runtime
    behavior (PHASE2, PHASE3), launch the built executable via
    `run_app_background` and drive it via `gte_send_request` to visually/
    programmatically confirm nothing regressed, then `stop_app_background`
    it. Do not skip this in favor of "it compiled, so it must be fine."

---

## Non-Goals (repeat, do not implement these under this campaign)

- The Player Build Pipeline (a real, shippable `<ProjectName>.exe`) is still
  explicitly out of scope — this campaign proves `gte_core.a` COULD support
  one (via the new player-link probe), it does not build the actual Player
  tooling.
- No change to `GTE_ENABLE_PROJECT_PANEL`/`GTE_ENABLE_PROFILER`/
  `GTE_ENABLE_JOB_SYSTEM`/`GTE_ENABLE_NETWORK`/`GTE_CORE_STANDALONE_PROBE_ONLY`.
- No redesign of `IEditorLayer` itself, and no change to which of its
  existing ~30 methods are "Core-tier" versus "host-tier" per
  `editor-core-separation-1`'s own Locked Design Decision #8 — that
  boundary is correct and untouched; this campaign only fixes the ONE
  documented, genuinely-separate gap (the Frame Debugger capture pointer's
  free-function calls) that decision's own text already flagged as the
  correct future fix (see `CAMPAIGN_COMPLETION_REPORT.md`'s "What remains
  genuinely open", option (a)).
- No real CI pipeline for this repository — the new player-link probe is a
  manually-invocable local CMake project, exactly like the existing
  standalone-core probe, not a GitHub Actions workflow.
- No renaming of `gte_core`/`gte_editor`/`Core`/`EditorHost`/`GreatTamanaEditor`.
