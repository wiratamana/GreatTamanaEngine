# PHASE0_MASTER_STRATEGY.md — Network Import + Instantiate Pipeline ("stl-parser-2")

**Branch:** `feature/stl-parser-impl`
**Orchestrator for:** `PHASE1_ASSET_IMPORT_COMMAND_BRIDGE_AND_EDITOR_WIRING.md`,
`PHASE2_IMPORT_ASSET_NETWORK_ENDPOINT.md`,
`PHASE3_INSTANTIATE_MESH_ASSET_ENGINE_COMMAND.md`,
`PHASE4_INSTANTIATE_ASSET_NETWORK_ENDPOINT.md`,
`PHASE5_END_TO_END_SMOKE_TEST_AND_CAMPAIGN_CLOSEOUT.md`

This document is the single entry point for this campaign. Every child phase
document below MUST be read together with this one — this file carries the
locked design decisions (all of them explicitly confirmed by the project
owner — see "Locked Design Decisions" below), the shared vocabulary, and the
risk register that apply across all five phases; the child documents carry
the actual step-by-step implementation detail for their own slice of the
work.

This campaign is the direct sequel to `task_manager/stl-parser-1/` (which
made `.stl` an importable mesh source format, on par with `.pmx`). That
campaign is DONE and merged on this same branch — read
`task_manager/stl-parser-1/PHASE0_MASTER_STRATEGY.md` first if you need
background on `AssetImporter`/`StlLoader`/`MeshData`, but do not re-open or
re-implement any of it. This campaign builds two brand-new HTTP endpoints on
top of that already-finished pipeline.

---

## Step 1: The Goal (Where are we going?)

Today, importing a file into this engine's asset pipeline and instantiating
an asset into the live Scene are both **mouse-and-keyboard-only** operations
(drag a file onto the Editor's "Project" panel; drag a `*.gta` onto
"Hierarchy"/"Scene"). This campaign adds two new HTTP endpoints so an
external caller (an AI/LLM-driven debug loop, a CI script, or any other
loopback HTTP client) can do both **without a human touching the UI at
all**:

1. **`POST /import_asset`** — imports a single file that lives ANYWHERE on
   this machine's filesystem (explicitly OUTSIDE the engine's own "Project"
   folder is the primary use case this campaign is built for) through the
   exact same `AssetImporter::ImportAssetFile()` pipeline the Editor's
   "Project" panel drag-and-drop already uses — the same STL/PMX/VMD/image
   parsing, the same `*.gta` wrapping, the same `AssetDatabase` registration.
2. **`POST /instantiate_asset`** — spawns an already-imported `*.gta`
   `AssetType::Mesh` asset into the live ECS Scene, so it appears in both the
   "Scene" and "Game" Editor panels — the network-triggerable equivalent of
   dragging a Mesh asset from "Project" onto "Hierarchy".

The concrete, real proof this campaign is measured against — and the subject
of `PHASE5`'s own final smoke test — is the exact same reference asset
`stl-parser-1` already used:

```
C:\Users\F5954\Documents\TAMANA\GreatTamanaEngine\_reference\pl-sky\assets\terrain.stl
```

(52,272,984 bytes on disk; binary STL; 1,045,458 triangles; 3,136,374
non-shared vertices once imported — see `stl-parser-1`'s own
`PHASE0_MASTER_STRATEGY.md` for how these numbers were derived). This file
lives OUTSIDE "Project" (under the gitignored `_reference/` folder) —
exactly the "import a file from outside Project" scenario `/import_asset`
exists for. By the end of `PHASE5`, a single short sequence of HTTP calls
must: import `terrain.stl` into "Project", instantiate the resulting
`*.gta`, and visually confirm (via the already-existing `GET
/get_swapchain`/`GET /get_game_view` capture endpoints) that a real,
lit/rendered terrain mesh is now sitting in the Scene — a complete,
human-free "debug this STL end to end" loop.

## Step 2: The Situation (Where are we now?)

### What already exists and needs NO changes

- **`src/Assets/AssetImporter.h`/`.cpp`** — `ImportAssetFile(AssetDatabase&,
  sourcePath, preferredDestinationPath)` already does everything
  `/import_asset` needs at the parsing/encoding level: STL/PMX → `MeshData` →
  `*.gta` (`AssetType::Mesh`), VMD → `MotionData` → `*.gta`
  (`AssetType::Animation`), a supported image → KTX2 → `*.gta`
  (`AssetType::Texture`), anything else → a plain file copy. This campaign
  never touches this file.
- **`src/Game/Game.h`/`.cpp`** — `Entity CreateMeshEntityFromGtaFile(Renderer&
  renderer, const std::string& absoluteGtaPath)` already does everything
  `/instantiate_asset` needs at the spawning level: decodes the `*.gta`,
  uploads GPU mesh(es) (cached per distinct path, via
  `MeshInstantiationSystem`/`MeshAssetGpuCatalog`), builds a real root+children
  ECS entity hierarchy, wires up skinning/physics hand-off if the model turns
  out to be skinned. Returns `kInvalidEntity` (never throws) for anything
  that doesn't resolve to a valid, non-empty Mesh `*.gta`. This campaign
  never touches this method's own internals.
- **Four existing cross-thread bridges** already establish the exact
  pattern this campaign's two new endpoints must follow (see `AGENTS.md`,
  "Networking", and each bridge's own header comment):
  - `FrameCaptureBridge` — read-only pixel capture.
  - `EngineCommandBridge` — ECS-MUTATING commands
    (`InstantiatePrimitive`/`DeleteEntity`/`SetEntityTrs`/`InstantiateLight`),
    single global slot, `SubmitAndWait()`/`TryPeekPendingCommandRequest()`/
    `FulfillCommand()`. `Application::Run()` drains at most one pending
    command per frame, EARLY (right after SDL input polling, before
    `Game::Update()`).
  - `EditorUiCommandBridge` — EDITOR-UI-mutating commands (`ActivateTab`),
    drained right after `m_editorLayer->NewFrame()`, before `BuildUI()`. This
    is the bridge whose exact shape `PHASE1` copies for the new
    `AssetImportCommandBridge`.
  - `FrameDebuggerCommandBridge` — Frame Debugger commands, drained at the
    same point as `EditorUiCommandBridge`.
  Every one of these bridges is "one NEW kind of cross-thread request gets
  its OWN bridge type" — `AGENTS.md` explicitly documents this as a rule, not
  a coincidence (see the `FrameCaptureBridge` bullet: "A future endpoint
  needing DIFFERENT engine data must NOT extend this class's ... enum for an
  unrelated purpose — build its own small, similarly-reviewed, similarly-
  narrow bridge instead").
- **`src/Editor/EditorLayer.h`'s `IEditorLayer` interface** (real impl:
  `ImGuiEditorLayer.cpp`; inert impl: `NullEditorLayer.cpp`, compiled instead
  when `GTE_ENABLE_EDITOR=OFF`) is the ONLY abstraction boundary between
  `Application`/`Network` and anything Editor/ImGui-owned — see
  `IEditorLayer::ActivateTab()` for the exact, already-shipped precedent
  `PHASE1` mirrors: a plain-data-in, plain-data-out virtual method,
  implemented for real in `ImGuiEditorLayer` and as a harmless no-op in
  `NullEditorLayer`.
- **`src/Editor/Panels/ProjectPanel.h`/`.cpp`** owns the ENGINE'S ONLY
  `AssetDatabase` instance (`m_assetDatabase`) and the "Project" root
  resolution (`ResolveProjectRootDirectory()`,
  `src/Editor/ProjectRootPath.h`/`.cpp` — compiled whenever
  `GTE_ENABLE_EDITOR` is ON, independent of the separate
  `GTE_ENABLE_PROJECT_PANEL` switch). `HandleExternalFileDrop()` is the
  existing, hand-driven precedent for exactly what `/import_asset` needs to
  do programmatically: resolve a destination directory inside "Project",
  compute a collision-safe destination filename
  (`ProjectPanelData.h`'s `MakeUniqueDestinationPath()`), call
  `ImportAssetFile(m_assetDatabase, source, destination)`, and mark the
  panel's own tree as needing a rescan (`m_needsRescan = true`).

### What's genuinely missing, and what this campaign's five child phases add

1. **No cross-thread path exists at all from the Network layer into
   `ProjectPanel`/`AssetDatabase`.** `PHASE1` adds a brand-new,
   `EditorUiCommandBridge`-shaped bridge (`AssetImportCommandBridge`) plus a
   new `IEditorLayer::ImportExternalAssetIntoProject()` virtual method (real
   implementation forwards to a new `ProjectPanel::ImportExternalFile()`
   method; `NullEditorLayer`'s implementation reports "project not
   available"), plus `Application`'s own wiring (ownership, constructor
   injection into `NetworkServer`, the per-frame drain call) — with **no
   network ROUTE registered yet** (that is `PHASE2`'s job). This is the
   heaviest, most cross-cutting phase in this campaign (see "Delegation
   Flow" below for why it gets its own pre-check pass).
2. **No `POST /import_asset` HTTP route exists.** `PHASE2` adds the actual
   `NetworkRoutes.h`/`.cpp` request-parsing/response-building functions and
   registers the route in `NetworkServer.cpp`, built entirely on top of
   `PHASE1`'s bridge.
3. **`CreateMeshEntityFromGtaFile()` has no network-reachable, EngineCommandBridge-
   compatible wrapper, and reports no rich success/failure OUTCOME at all
   (just an `Entity`, no error message).** `PHASE3` adds a new, thin
   `Game::InstantiateMeshAssetFromGtaFile()` method (an `Outcome`-returning
   wrapper around the EXISTING, unchanged `CreateMeshEntityFromGtaFile()` —
   see Locked Design Decision 4 for why this stays deliberately bare-bones)
   plus a fifth `EngineCommandKind` value on the ALREADY-EXISTING
   `EngineCommandBridge` (this is an ECS/Renderer-mutating spawn, exactly
   like `InstantiatePrimitive` — it reuses that bridge, it does NOT get a new
   one — see Locked Design Decision 9).
4. **No `POST /instantiate_asset` HTTP route exists.** `PHASE4` adds the
   `NetworkRoutes.h`/`.cpp` functions and registers the route, built entirely
   on top of `PHASE3`'s new engine command.
5. **Nothing has ever proven the two new endpoints work together, end to
   end, against the real reference asset.** `PHASE5` runs the real
   `import_asset` → `instantiate_asset` → `GET /get_swapchain`/`GET
   /get_game_view` chain against `terrain.stl`, confirms zero regressions via
   the full test suite, and writes the campaign's closing report.

## Step 3: The Plan (How do we get there?)

Five sequential, independently buildable/testable phases. Each one is a
self-contained unit of work: new/changed source files, new/changed test
files, the exact `CMakeLists.txt`/`tests/CMakeLists.txt` registrations
needed, and its own completion-report expectation.

| # | File | One-line purpose |
|---|------|-------------------|
| 1 | `PHASE1_ASSET_IMPORT_COMMAND_BRIDGE_AND_EDITOR_WIRING.md` | New `src/Application/AssetImportCommandBridge.h/.cpp`, a new `IEditorLayer::ImportExternalAssetIntoProject()` virtual (+ `NullEditorLayer` stub), a new `ProjectPanel::ImportExternalFile()` method, `Application`'s wiring/per-frame drain, AND a new fifth (still-unused) `AssetImportCommandBridge*` constructor parameter/member on `NetworkServer` itself — no HTTP route yet. |
| 2 | `PHASE2_IMPORT_ASSET_NETWORK_ENDPOINT.md` | `NetworkRoutes.h`/`.cpp` parsing/response-building for `POST /import_asset`, `NetworkServer.cpp` route registration (using the `AssetImportCommandBridge` pointer/constructor parameter `PHASE1` already added — no further `Application.h`/`.cpp` or `NetworkServer` constructor-signature changes needed), full test coverage. |
| 3 | `PHASE3_INSTANTIATE_MESH_ASSET_ENGINE_COMMAND.md` | New `Game::InstantiateMeshAssetFromGtaFile()` + `InstantiateMeshAssetOutcome` (`src/Game/EngineCommandResults.h`), a fifth `EngineCommandKind` value + dispatch branch, full test coverage. |
| 4 | `PHASE4_INSTANTIATE_ASSET_NETWORK_ENDPOINT.md` | `NetworkRoutes.h`/`.cpp` parsing/response-building for `POST /instantiate_asset`, `NetworkServer.cpp` route registration, full test coverage. |
| 5 | `PHASE5_END_TO_END_SMOKE_TEST_AND_CAMPAIGN_CLOSEOUT.md` | Full regression suite, then the real, live `terrain.stl` import→instantiate→screenshot smoke test over HTTP, campaign completion report. |

### Locked Design Decisions

These were raised as open questions during strategy research and have been
**explicitly decided by the project owner** (via direct Q&A during this
strategy session). Every child phase MUST follow these exactly — do not
re-litigate them during implementation.

1. **`/import_asset`'s heavy work (parsing the external file, writing the
   `*.gta`) runs on the MAIN THREAD, via a brand-new bridge that mirrors
   `EditorUiCommandBridge`/`GET /activate_tab` exactly** — NOT a
   network-thread-local/throwaway `AssetDatabase` shortcut. This is a
   deliberate, informed trade-off: importing a large file (the real
   `terrain.stl` included) will briefly stall the engine's whole frame loop
   while the main thread parses it, in exchange for strict, unambiguous
   adherence to this codebase's existing rule that a route handler must
   never touch `AssetDatabase` (or any other engine-owned state) directly,
   even indirectly, full stop (see `AGENTS.md`, "Networking"). `PHASE1`
   implements this.
2. **`/import_asset` accepts an OPTIONAL `destination_folder` field** — a
   path relative to the "Project" root (e.g. `"Meshes/Terrain"`), created
   automatically if missing. Omitted (or empty-string) means "import
   directly into the Project root", matching `ImportAssetFile()`'s own
   existing collision-safe-rename behavior. `PHASE2` implements the parsing;
   `PHASE1`'s `ProjectPanel::ImportExternalFile()` implements the actual
   directory resolution + creation, WITH path-traversal hardening (see Risk
   Register below).
3. **`/instantiate_asset` identifies the asset to spawn by a `gta_path`
   STRING — never a Guid.** This mirrors `Game::CreateMeshEntityFromGtaFile
   (Renderer&, const std::string& absoluteGtaPath)`'s own existing, unchanged
   contract exactly, and deliberately keeps `Game`/`EngineCommandDispatch`
   (core, always-compiled layer) free of any new dependency on
   `AssetDatabase`/Guid-resolution machinery, which today lives ONLY in
   Editor-only code (`ProjectPanel`). **Clarifying implementation detail
   (not a re-litigation of the locked decision above, just how it's
   realized):** `gta_path` is used EXACTLY as given, matching
   `CreateMeshEntityFromGtaFile`'s own parameter name
   (`absoluteGtaPath`) literally — callers are expected to pass an ABSOLUTE
   path (e.g. exactly the `final_absolute_path` `/import_asset`'s own
   response already hands back — see `PHASE2`). If a relative path is given
   instead, it resolves against the engine PROCESS's current working
   directory (`std::filesystem::absolute()`), NOT specially against the
   "Project" root — `PHASE4`'s route handler does no Editor-aware path
   resolution at all, keeping `NetworkServer.cpp` free of any new
   `GTE_ENABLE_EDITOR`-conditional code. This is intentionally a narrower
   scope than "auto-resolve any relative path against Project" would be —
   documented here so it is a deliberate, visible choice, not a silently
   discovered gap later.
4. **`/instantiate_asset` is a BARE-BONES wrapper.** It spawns the mesh
   EXACTLY the way `CreateMeshEntityFromGtaFile()` already behaves today: at
   the world origin (identity transform on the root), completely unparented,
   named after the source file's own stem (no de-duplication, no custom
   name, no position, no parent — none of `InstantiatePrimitive()`'s richer
   naming/positioning/parenting contract). `PHASE3`'s new
   `Game::InstantiateMeshAssetFromGtaFile()` method adds ONLY a rich
   success/failure OUTCOME (so the HTTP response is actually useful) on top
   of the existing, completely unchanged spawn behavior — it must never add
   new parameters that change WHERE or how the entity is spawned.
5. **`PHASE5`'s final end-to-end smoke test uses the REAL, full
   `terrain.stl`** (52MB / 1,045,458 triangles) — not a small synthetic
   fixture — because this is the closest possible proxy for a genuine,
   real-world debugging session, which is this whole campaign's stated
   purpose. Every EARLIER phase's own AUTOMATED unit/integration tests
   (`PHASE1`–`PHASE4`) still use small, fast, synthetic fixtures (tiny
   hand-built `*.gta` files, a handful of triangles, or none at all where a
   test doesn't need real mesh bytes) — `terrain.stl` is reserved
   specifically for `PHASE5`'s own manual/live verification step, exactly
   mirroring `stl-parser-1`'s own precedent (`PmxLoaderRealModelSmokeTest`/
   `StlLoaderRealFileSmokeTest`-style tests: present-on-this-machine-only,
   never a hard CI dependency).
6. **`AssetImportCommandBridge::SubmitAndWait()`'s default wait timeout is a
   fixed 120,000ms (120 seconds)** — large enough that even a genuinely slow
   main-thread parse of the real 52MB/1,045,458-triangle `terrain.stl` (plus
   whatever else the main thread's own frame is doing that particular
   millisecond) should never spuriously time out. There is NO per-request
   `timeout_ms` override field in `/import_asset`'s own JSON body — this was
   an explicit, deliberate simplification the project owner chose over a
   more flexible (but more code) override mechanism.
7. **`/import_asset` returns HTTP 503 (Service Unavailable) whenever the
   Editor's "Project" panel isn't available in this build** — i.e.
   whenever `GTE_ENABLE_EDITOR` is OFF, OR it's ON but
   `GTE_ENABLE_PROJECT_PANEL` is OFF. This matches this codebase's
   existing "bridge/feature not available → 503" convention used by every
   other bridge-backed route (`captureBridge == nullptr` → 503,
   `commandBridge == nullptr` → 503, `uiCommandBridge == nullptr` → 503,
   `frameDebuggerCommandBridge == nullptr` → 503) — this campaign does not
   invent a new status code for this case.
8. **`/import_asset`'s `destination_folder` is hardened against path
   traversal.** An absolute path, ANY Windows drive-relative or
   root-relative path (e.g. `"C:Temp"` or `"\Escape"` — `is_absolute()`
   alone does not catch either of these, since it requires both a root name
   AND a root directory together; a real check must also reject a path with
   EITHER one present on its own), or any path whose lexically-normalized
   form starts with a `..` segment (i.e. would resolve to somewhere OUTSIDE
   the "Project" root), is REJECTED as a validation failure (HTTP 400) —
   never silently clamped, never allowed to write outside "Project".
   `PHASE1`'s `ProjectPanel::ImportExternalFile()` is where this check
   actually lives (see that phase's own Step 3 for the exact algorithm).
9. **`InstantiateMeshAsset` is a FIFTH value on the ALREADY-EXISTING
   `EngineCommandBridge`/`EngineCommandKind` enum — it does NOT get a new,
   dedicated bridge.** Unlike `/import_asset` (a genuinely new KIND of
   cross-thread request — Editor/Project/AssetDatabase-shaped — which
   correctly earns its own new bridge per Locked Design Decision 1),
   spawning a Mesh asset into the ECS/Renderer is EXACTLY the same shape of
   request `InstantiatePrimitive`/`InstantiateLight` already are (ECS +
   Renderer mutation, resolved by `Application::Run()`'s existing
   `EngineCommandBridge` pump). This also means `/instantiate_asset` works
   in ANY build configuration (`GTE_ENABLE_EDITOR` on OR off), exactly like
   `/instantiate_primitive` already does today — it has NO dependency on the
   Editor/"Project" panel at all, unlike `/import_asset`.

### Shared Vocabulary (used identically across all five child phases)

- **"The Project root"**: `gte::ResolveProjectRootDirectory()`
  (`src/Editor/ProjectRootPath.h`) — the "Project" folder living next to the
  built executable, created on first use. Only ever resolved from
  Editor-compiled code (`ProjectPanel`) in this campaign — never from
  `Game`/`Network`/`Application` core code (see Locked Design Decision 3's
  clarifying note).
- **"A bridge"**: one of this engine's small, single-global-slot,
  mutex+condition-variable cross-thread request/result classes
  (`FrameCaptureBridge`/`EngineCommandBridge`/`EditorUiCommandBridge`/
  `FrameDebuggerCommandBridge`, and — after `PHASE1` —
  `AssetImportCommandBridge`), each with the exact same
  `SubmitAndWait()`/`IsCommandPending()`/`TryPeekPendingCommandRequest()`/
  `FulfillCommand()` shape (see `EditorUiCommandBridge.h`/`.cpp` for the
  canonical, smallest example to copy from).
- **"Bare-bones wrapper"**: a new method whose only job is to (a) call an
  EXISTING, unchanged engine method and (b) turn its return value into a
  richer, HTTP-friendly success/failure `Outcome` struct — it must never
  change what the underlying method actually DOES.
- **A Mesh `*.gta`**: the on-disk artifact `stl-parser-1` already produces
  from a `.pmx` or `.stl` source — unchanged by this campaign.

### Risk Register

| Risk | Mitigation | Owning phase |
|------|------------|--------------|
| A network caller passes a `destination_folder` containing `".."` (or an absolute path), intending to write a `*.gta` OUTSIDE the "Project" folder entirely (e.g. overwriting an arbitrary file elsewhere on disk). | `ProjectPanel::ImportExternalFile()` rejects (HTTP 400, via the bridge's outcome) any `destination_folder` that is absolute, OR whose `std::filesystem::path::lexically_normal()` form has `".."` as its first component — computed and checked BEFORE any directory is created or any file is written. | PHASE1 |
| Importing the real 52MB/1,045,458-triangle `terrain.stl` on the MAIN THREAD (Locked Design Decision 1) visibly freezes the Editor/Game window for the duration of the parse — a real, accepted, DOCUMENTED trade-off, not a bug to "fix" by moving the work off-thread. | `PHASE1`'s own doc explicitly calls this out with a large (120s) bridge timeout (Locked Design Decision 6); `PHASE5`'s smoke test explicitly expects and tolerates a multi-second stall, and must not be written to assume a fast/instant response. | PHASE1, PHASE5 |
| A future contributor "helpfully" collapses the new `AssetImportCommandBridge` into the existing `EngineCommandBridge` or `EditorUiCommandBridge` to "save a file", regressing the "one new kind of request = one new bridge" rule `AGENTS.md` already documents. | `PHASE1`'s own doc quotes the exact `AGENTS.md`/`FrameCaptureBridge.h` rule this would violate, and explains why `/import_asset` (Editor/Project-shaped) and `/instantiate_asset` (ECS/Renderer-shaped, reusing `EngineCommandBridge`) are correctly DIFFERENT cases, not an inconsistency. | PHASE1, PHASE3 |
| `GTE_ENABLE_EDITOR=OFF`/`GTE_ENABLE_PROJECT_PANEL=OFF` builds must still compile/link `NetworkServer.cpp` (which is ALWAYS compiled) without a hard dependency on Editor-only code — a naive implementation might `#include` an Editor-only header unconditionally from `Network`, breaking the non-Editor build's link step. | `PHASE1` keeps every new Editor-facing symbol behind `IEditorLayer`'s existing "real vs. Null impl, chosen entirely by which `.cpp` is compiled" seam — `Application`/`Network` only ever see the plain-data `IEditorLayer` interface and the new `AssetImportCommandBridge` (itself Editor-independent, living under `src/Application/`), never `ProjectPanel`/`AssetDatabase`/`ResolveProjectRootDirectory()` directly. | PHASE1 |
| `Game::InstantiateMeshAssetFromGtaFile()`'s outcome reporting silently regresses `CreateMeshEntityFromGtaFile()`'s own existing degrade-gracefully contract (e.g. by throwing, or by changing what gets spawned). | `PHASE3` requires this new method to be a PURE forwarding wrapper — one call to the existing method, one `if (entity == kInvalidEntity)` branch — with its own test suite asserting the exact same entities get created as a direct `CreateMeshEntityFromGtaFile()` call would. | PHASE3 |

### Delegation Flow For This Campaign (informational — not part of the code itself)

1. **Iteration 1 (this pass):** produce this master strategy doc + the five
   child phase docs only. No source code is written in this iteration.
2. **Iteration 1.5 (a focused pre-check):** `PHASE1` is this campaign's
   heaviest, most cross-cutting document (a brand-new bridge type, a new
   `IEditorLayer` virtual method touching BOTH concrete implementations, a
   new `ProjectPanel` method with security-sensitive path handling, and
   `Application`'s own composition-root wiring) — it gets its own, narrowly
   scoped double-check pass before the general double-check below.
3. **Iteration 2 (full double-check):** re-reads all six documents in this
   folder end-to-end looking for gaps/incorrectness/insufficiency/missing
   content/worth-improving items, overwrites any file that needs it in
   place (never creates new files), and — once satisfied — delegates one
   implementation task PER child phase (five total, `PHASE1`→`PHASE5` IN
   ORDER, since each later phase depends on the one(s) before it), each
   pointed at this same folder and told to read this master doc first.

### Definition Of Done (for the whole campaign)

- `src/Application/AssetImportCommandBridge.h`/`.cpp` exists, registered in
  the root `CMakeLists.txt`, mirroring `EditorUiCommandBridge`'s exact shape.
- `IEditorLayer::ImportExternalAssetIntoProject()` exists, implemented for
  real in `ImGuiEditorLayer` (forwarding to a new
  `ProjectPanel::ImportExternalFile()`) and as an inert "project not
  available" stub in `NullEditorLayer`.
- `POST /import_asset` exists, documented in
  `docs/conventions/networking.md`, returns 200/400/503/504 exactly per
  `PHASE2`'s own locked contract, and correctly imports a real external
  file into "Project" (creating `destination_folder` if given, hardened
  against path traversal).
- `Game::InstantiateMeshAssetFromGtaFile()` + `InstantiateMeshAssetOutcome`
  exist; `EngineCommandKind::InstantiateMeshAsset` is a real, dispatched
  fifth value on the existing bridge.
- `POST /instantiate_asset` exists, documented in
  `docs/conventions/networking.md`, returns 200/400/503/504 exactly per
  `PHASE4`'s own locked contract.
- A live, running instance of the engine, driven PURELY over HTTP, can:
  import the real `terrain.stl` (from `_reference/pl-sky/assets/`, outside
  "Project") via `POST /import_asset`, instantiate the resulting `*.gta` via
  `POST /instantiate_asset`, and a subsequent `GET /get_swapchain`/`GET
  /get_game_view` capture visibly shows a real, rendered terrain mesh in the
  Scene — see `PHASE5`.
- `cmake --build build` succeeds; `ctest -C Debug --output-on-failure` (from
  the `build` directory) passes with zero regressions and every new test
  file included.


---

## Double-Check Pass (stl-parser-2 campaign, Iteration 2 — whole-folder)

Per this document's own "Delegation Flow", all six files in this folder were
re-read end to end and independently re-verified against the REAL, current
`src/`/`tests/`/`CMakeLists.txt`/`tests/CMakeLists.txt` state (not merely
trusted from `PHASE1`'s own earlier Iteration 1.5 pre-check) before this
campaign's five implementation tasks were delegated. `PHASE0` and `PHASE1`
were found accurate as-is (every concrete claim in both — file paths,
signatures, `#if` gating, bridge shapes, the path-traversal algorithm, the
CMakeLists.txt registration points — checked out against the real source
tree with no corrections needed this pass) and were not modified further.
Corrections made to the other three phase documents during this pass:

1. **`PHASE2`, Section 3.2**: the original draft's inline route-handler
   pseudocode called `BuildImportAssetResponseJson(outcome)` with the wrong
   type (`ImportExternalFileOutcome`, the `Application`-layer type) and then
   relied on a SEPARATE, later "Important, do not skip" callout to silently
   correct it — a real, if flagged, foot-gun for an implementer skimming only
   the first code block. The pseudocode itself now shows the correct,
   final field-by-field-copy-into-`ImportedAssetResponseView` shape directly,
   and the redundant later callout was trimmed to a short cross-reference.
2. **`PHASE3`, Section 3.6**: confirmed by direct inspection that
   `Renderer`'s constructor is `explicit Renderer(Window&)`
   (`src/Renderer/Renderer.h`) — genuinely impossible to construct without a
   live SDL window + Vulkan device. The original draft's hedge that the
   `kInvalidEntity`/missing-file branch of `InstantiateMeshAssetFromGtaFile()`
   "may be exercisable without a real `Renderer&`" was therefore corrected:
   the blocker is obtaining a `Renderer&` reference AT ALL at the call site,
   not merely how early `MeshAssetGpuCatalog::Resolve()` itself bails out
   internally — this is confirmed to be a hard, permanent Tier-2 wall, and no
   new `tests/Game/InstantiateMeshAssetFromGtaFileTests.cpp` file should be
   created at all (mirroring `PHASE1`'s own established "accepted gap,
   verified by inspection, no invented test file" precedent for
   `NullEditorLayer`). Also confirmed, by browsing `tests/Application/` and
   grepping all of `tests/` for `ExecuteEngineCommand`, that NO dispatch-level
   test file (`EngineCommandDispatchTests.cpp` or similarly named) exists
   anywhere today — the original draft's "or whatever the existing dispatch
   test file is named" phrasing incorrectly implied one might already exist.
3. **`PHASE4`, Section 3.4**: the original draft guessed its new end-to-end
   test file should mirror `tests/Network/ActivateTabEndpointEndToEndTests.cpp`
   "likely" — confirmed WRONG by inspection: that file exercises the
   structurally different `EditorUiCommandBridge` (its own 404/409
   status-code semantics, which do not apply to `/instantiate_asset` at all).
   The correct template, confirmed by reading it in full, is
   `tests/Network/EngineCommandEndpointsEndToEndTests.cpp` — the SAME
   `EngineCommandBridge` `/instantiate_asset` itself reuses, already covering
   the exact 200/400/503/504 status-code shape this new route needs, via its
   own `FakeEngineCommandStandIn` (a `switch` over every `EngineCommandKind`)
   that this phase's new `InstantiateMeshAsset` case slots directly into.
4. **`PHASE5`, Section 3.5**: `PHASE4`'s own Section 3.3 explicitly flagged
   that `/instantiate_asset` keeps `EngineCommandBridge`'s unchanged 3000ms
   default timeout, and that uploading the real `terrain.stl`'s 3,136,374
   vertices to the GPU for the first time is genuine, non-trivial work that
   COULD exceed it — explicitly making `PHASE5` "responsible for noticing and
   reporting this." The original `PHASE5` draft's own Section 3.5 never
   actually mentioned this possibility at all (unlike Section 3.4's parallel
   note for the `/import_asset` call) — added an explicit note covering the
   same "retry with a larger `timeout_seconds`, and if the ENGINE's own
   bridge genuinely timed out, treat it as a real finding to fix and
   document as a deviation" guidance, so this phase doesn't skip over the
   exact scenario `PHASE4` asked it to watch for.
