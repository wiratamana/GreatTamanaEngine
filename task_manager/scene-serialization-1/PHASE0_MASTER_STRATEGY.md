# PHASE0 — MASTER STRATEGY: Remove Hardcoded Demo Triangles + Text-Based Scene Serialization (v2)

Orchestrator document for the `scene-serialization-1` campaign. Every child
phase document in this folder implements one slice of this plan. Read this
file first, then execute `PHASE1_...md` → `PHASE6_...md` **strictly in
order** — each phase's code depends on the previous phase's deliverables
already compiling. Every phase produces real, compilable, testable
`.h`/`.cpp`/`CMakeLists.txt`/test changes — none of them are "just planning."

## Second-Iteration Audit (v2) — what changed since v1, and why

This campaign's documents were re-read end to end against the ACTUAL current
source tree (every referenced file, function signature, line number, CMake
list, and test-registration entry named below was independently re-verified,
not just re-read) before writing this v2. The result: v1 held up extremely
well — every file path, function signature, `CMakeLists.txt`/
`tests/CMakeLists.txt` line number, and API assumption (`Guid`/`Vec3`/`Quat`
factory methods, `Registry`/`ComponentStorage<T>` semantics,
`AssetDatabase`/`TransformHierarchy` behavior) named across PHASE1–PHASE6
still matches the live code exactly. Three concrete, worth-fixing gaps were
found and are now fixed in this v2 (PHASE1, PHASE2, and PHASE5 needed no
changes at all — they are re-verified accurate and left exactly as v1 wrote
them):

1. **PHASE3's `assetGuid=` validation was over-strict and mis-scoped.** v1's
   spec validated "a parsed `assetGuid=` value must not be `Guid::Invalid()`"
   as a per-line, kind-independent rule. Two real problems with that: (a) it
   would reject an otherwise well-formed `Primitive` record that happened to
   also carry a stray/leftover `assetGuid=` line (harmless for a Primitive —
   that field is only ever meaningful for `Asset` — but v1's parser would
   still fail the WHOLE file over it), and (b) it did nothing to catch an
   `Asset` record that simply omits `assetGuid=` entirely (an absent key
   legitimately keeps `SceneObjectRecord`'s own default per v1's own "absent
   isn't malformed" rule — which for `assetGuid` IS `Guid::Invalid()` — so a
   hand-edited or truncated `Asset` block with no `assetGuid=` line at all
   would silently parse "successfully" into an unresolvable record, only
   failing much later and silently at Load time). **Fixed in PHASE3 v2**:
   the "must be a real Guid" invariant is now checked exactly once, at the
   natural point — when an object's `END` line closes it — and is scoped to
   `kind == Asset` only. See PHASE3's revised format spec/implementation
   notes/test list.
2. **PHASE4 never called out the path-normalization invariant its own
   `FindByPath()` call quietly depends on.** `AssetDatabase::FindByPath()`
   (`src/Assets/AssetDatabase.cpp`) re-normalizes whatever path it's given
   through `std::filesystem::absolute()` before comparing, and
   `RefreshFromDirectory()` populates its index the exact same way — so a
   `MeshAssetSource::gtaPath` that was captured from a DIFFERENT absolute-path
   construction (e.g. a relative segment, a different slash style, or a
   different `Guid` if the path is actually two different files after all)
   would silently fail to resolve. This already holds true in practice today
   (`Panels/ProjectPanel.cpp`'s drag payload is built from
   `SDL_GetBasePath()`-derived, already-absolute segments — the exact same
   family of path `AssetDatabase` itself normalizes to), but v1 never wrote
   this down as a load-bearing assumption future code must not break.
   **Fixed in PHASE4 v2**: an explicit "Correctness invariant" callout plus
   one added test case.
3. **No phase ever touched `README.md`.** `README.md`'s own "Status" section
   (and a couple of earlier prose paragraphs) describes the 3 hardcoded demo
   triangles as CURRENT, present-tense engine behavior ("`Game` builds a
   small demo scene (three entities sharing one mesh/pipeline...)", "exactly
   like the existing demo triangles share theirs") — both of which become
   stale the moment PHASE1 lands, and neither v1's PHASE1 nor PHASE6 said
   anything about it. `TODO.md`'s own opening paragraph states the house
   rule this violates directly: `README.md` is kept "focused on describing
   the architecture/status *as it exists today*". Every prior campaign in
   this repository (Render Graph, GPU Vertex Skinning, Job System, ...)
   closed itself out with a fresh, dated `README.md` "Status" bullet
   summarizing what shipped, rather than leaving the file silently stale —
   this campaign was about to be the first one that didn't. **Fixed in
   PHASE6 v2**: a new step 3.8 adds exactly that bullet, in the same
   bold-lead-sentence house style every other "Status" entry already uses.

Nothing above changes this campaign's scope, ordering, or any of the four
binding design decisions below — they are unchanged from v1.

## Campaign file map

| File | Delivers |
|---|---|
| `PHASE0_MASTER_STRATEGY.md` | This document — root-cause analysis, design-decision log, ordering, and (as of v2) the second-iteration audit above. |
| `PHASE1_REMOVE_HARDCODED_DEMO_TRIANGLES.md` | Deletes the 3 hardcoded triangle entities + their private demo Pipeline/Mesh from `Game::EnsureDemoSceneBuilt()`, while preserving the default Camera entity it also creates (renamed to reflect its narrowed purpose). Confirms `Shaders/Triangle.vert/.frag` must NOT be removed (still used by `PrimitiveGpuCatalog`'s own default pipeline). Re-verified accurate in the v2 audit; unchanged. |
| `PHASE2_PRIMITIVE_SOURCE_TAG_AND_HIERARCHY_DESTROY_HELPER.md` | New `PrimitiveSource` ECS component (so a primitive-spawned entity remembers which `PrimitiveType` it is, the way `MeshAssetSource` already remembers an asset path) wired through `EntityBlueprint`/`PrimitiveGpuCatalog`/`EntityInstantiator`. New `TransformHierarchy::DestroyEntityAndDescendants()` free function (needed by Phase 4's "clear before load" step, and reusable later for the TODO.md "per-entity Delete" item). Re-verified accurate in the v2 audit; unchanged. |
| `PHASE3_SCENE_DOCUMENT_AND_TEXT_FORMAT.md` | New, always-compiled `src/Scene/` module: `SceneDocument.h` (plain data) + `SceneTextFormat.h/.cpp` (pure, Tier-1-testable text ⇄ `SceneDocument` read/write — the actual ".gtscene" file format, hand-rolled, zero ECS/Renderer/filesystem dependency). **v2: tightened `assetGuid=` validation to be kind-aware and checked at object-close time** (see the audit above). |
| `PHASE4_SCENE_BUILDER_REGISTRY_ASSETDATABASE_BRIDGE.md` | `src/Scene/SceneBuilder.h/.cpp` — the ECS-facing bridge: `BuildSceneDocumentFromRegistry()` (Registry + AssetDatabase → SceneDocument) and `ClearSerializableSceneObjects()` (wipes only what this feature owns before a Load). Still zero Renderer dependency, still Tier-1-testable. **v2: documents the `FindByPath()` path-normalization invariant explicitly** (see the audit above). |
| `PHASE5_EDITOR_SCENE_IO_AND_PROJECT_ROOT_HELPER.md` | New `src/Editor/ProjectRootPath.h/.cpp` (shared "/Project next to the .exe" resolver, extracted so it works even when `GTE_ENABLE_PROJECT_PANEL` is OFF) and `src/Editor/SceneIO.h/.cpp` (`SaveScene()`/`LoadScene()` — the thin, Game/Renderer/filesystem-touching glue that ties Phases 2–4 together against the hardcoded `TestScene.gtscene` path). Re-verified accurate in the v2 audit; unchanged. |
| `PHASE6_FILE_MENU_SAVE_OPEN_CTRL_SHORTCUTS.md` | Wires `File > Save Scene` (Ctrl+S) and `File > Open Scene` (Ctrl+O) into `DockLayout.cpp`'s menu bar, updates `ImGuiEditorLayer.cpp`'s call site, adds lightweight status feedback, updates `CMakeLists.txt`/`tests/CMakeLists.txt`, and closes out stale `TODO.md` prose. **v2: adds a new step updating `README.md`'s "Status" section** (see the audit above). |

## Design Decisions Already Resolved (do not re-litigate these)

Four genuine ambiguities were identified while investigating the current
source tree and put to the user directly before this plan was written. Their
answers are binding for every phase below:

1. **Camera on startup.** The hardcoded demo Camera entity `Game::EnsureDemoSceneBuilt()`
   currently also creates is **kept** — only the 3 triangle entities + their
   private Pipeline/Mesh are removed. A brand-new/loaded scene must never be
   left with nothing to look through. See PHASE1.
2. **Load scope.** This campaign implements a real, user-facing
   **`File > Open` (Load Scene)** menu item, not just an internal, untested
   `Deserialize`-only function. See PHASE6.
3. **Asset-instantiated entities.** Only the **ROOT** entity's Guid +
   Transform is captured for an asset-spawned hierarchy; every child "part"
   entity is discarded on save and freshly re-derived by re-instantiating the
   asset on load (`Game::CreateMeshEntityFromGtaFile()` already rebuilds the
   exact same child shape from the asset itself). Manual per-child Transform
   tweaks do **not** survive a save/load round-trip — an explicitly accepted
   simplification. See PHASE4/PHASE5.
4. **Module placement.** The (de)serialization logic lives in a brand-new,
   always-compiled top-level module, **`src/Scene/`** — mirroring how
   `src/Physics/` and `src/Jobs/` were bootstrapped as fresh modules — not
   folded into `src/Assets/`. See PHASE3/PHASE4.

## Step 1: The Goal (Where are we going?)

Two concrete, user-requested outcomes:

1. **Remove the 3 hardcoded triangles.** `Game::EnsureDemoSceneBuilt()`
   (`src/Game/Game.cpp`) currently builds, on the very first `Render()` call,
   a private demo `Pipeline` + a 3-vertex `Mesh` + three `Transform`+
   `MeshRenderer` entities positioned left/center/right. This code must go —
   it was always documented as a temporary proof-of-pipeline placeholder
   (`Game.h`'s own doc comment: *"Will be replaced by a real scene/
   asset-loading system once there's more than a hardcoded demo scene"*).
2. **Real, if deliberately simple, scene serialization.** Give the engine its
   first "Save"/"Open" for the two kinds of scene object it can currently
   create — a primitive (`Game::CreatePrimitiveEntity()`, "Hierarchy" →
   right-click → "Create 3D Object") and an asset instance
   (`Game::CreateMeshEntityFromGtaFile()`, dragging a `*.gta` out of
   "Project") — serialized as **plain text**, to a new, hand-rolled
   `*.gtscene` format, saved/loaded from one **hardcoded** path:
   `<Project folder>/TestScene.gtscene`, triggered by a new
   **`File > Save Scene` (Ctrl+S)** / **`File > Open Scene` (Ctrl+O)** menu
   pair. An asset-instantiated object is referenced by its stable
   `AssetDatabase` **Guid**, never a raw machine-local path — the same
   principle `MaterialTextureRef::guid` already established for a PMX
   material's texture (see `TODO.md`).

This is explicitly the **first**, intentionally minimal slice of
`TODO.md`'s long-standing "Scene serialization (save/load a scene to/from a
file)" roadmap item — not a general, arbitrary-component, arbitrary-path,
undo-able scene editor. See each phase's own "What We Will NOT Do" section.

## Step 2: The Situation / The Problem (Where are we now?)

A full read of the current source tree turned up the exact facts every
phase below is built against:

1. **Culprit A — the 3 triangles and the engine's only default Camera are
   welded together in one function.** `Game::EnsureDemoSceneBuilt()`
   (`src/Game/Game.cpp`, called from `Game::Render()`) creates the 3
   triangles **and** the one Camera entity the engine has ever spawned
   automatically, guarded by a single `bool m_demoSceneBuilt`. Naively
   deleting the whole function would also delete the only Camera the engine
   ever creates on its own, leaving "Scene"/"Game" looking at nothing
   (`RenderSystem::ResolveActiveCameraViewProjection()`'s fallback is
   `Mat4::Identity()` — survivable, but a strictly worse first-run
   experience than today, and NOT what the user asked to remove). **Fixed by
   PHASE1**, which surgically removes only the Pipeline/Mesh/3-entity block
   and keeps (renaming) the Camera-only bootstrap.
2. **Culprit B — `PrimitiveGpuCatalog` independently depends on the exact
   same shader pair the demo scene uses, and must not be touched.**
   `PrimitiveGpuCatalog::EnsureDefaultPipeline()`
   (`src/Game/Instantiation/PrimitiveGpuCatalog.cpp`) loads
   `shaders/Triangle.vert.spv`/`Triangle.frag.spv` for its own, completely
   separate default pipeline used by every "Create 3D Object" primitive —
   confirmed by direct code inspection, and already called out by
   `Game.cpp`'s own comment (*"intentionally separate from
   PrimitiveGpuCatalog's own default pipeline (even though they happen to use
   the same shader pair today)"*). **This means `Shaders/Triangle.vert/.frag`
   and their `gte_add_shader()` CMake calls must NOT be removed** — only
   `Game.cpp`'s own private `m_demoPipeline` + its `CreateMesh()` call go.
   PHASE1 states this as an explicit guardrail.
3. **Culprit C — a primitive-spawned entity has no on-disk-serializable
   memory of which shape it is.** `EntityBlueprintNode`
   (`src/Game/Instantiation/EntityBlueprint.h`) and
   `EntityInstantiator::Instantiate()`
   (`src/Game/Instantiation/EntityInstantiator.cpp`) already thread a
   `meshAssetSourcePath` through to a new `MeshAssetSource` component for an
   asset-spawned root — but nothing analogous exists for a primitive; a
   `MeshRenderer`'s `MeshHandle`/`PipelineHandle` alone cannot be reversed
   back into a `PrimitiveType` (they're just resource-pool indices, not
   semantic data). **Fixed by PHASE2**, which adds a `PrimitiveSource`
   component mirroring `MeshAssetSource`'s own pattern exactly.
4. **Culprit D — nothing in the engine can destroy an entity's whole
   subtree today.** `Registry::DestroyEntity()` destroys exactly one entity
   (removing it from every component pool) — grep of
   `ECS/TransformHierarchy.h` confirms no recursive
   "destroy this entity and everything parented under it" helper exists yet.
   Loading a scene must replace (not merge into) whatever is currently in
   the Registry, which needs exactly this primitive. **Fixed by PHASE2**,
   which adds `DestroyEntityAndDescendants()` right alongside the other
   `TransformHierarchy.h` free functions (`GetChildren()`, `SetParent()`,
   ...) — this is also independently useful for `TODO.md`'s own deferred
   "Per-entity Hierarchy context menu (Delete/Rename/Duplicate)" item.
5. **Culprit E — resolving "asset Guid ⇄ absolute path" requires an
   `AssetDatabase`, which is (correctly) NOT part of `Game`'s own public
   API.** `Game::CreateMeshEntityFromGtaFile()` takes an absolute path
   directly; it has never needed to know a Guid exists. `AssetDatabase`
   itself (`src/Assets/AssetDatabase.h/.cpp`) is confirmed, by direct
   `CMakeLists.txt` inspection, to be **unconditionally compiled** into
   `gte_core` (NOT gated behind `GTE_ENABLE_PROJECT_PANEL`) — so a Guid
   lookup is always available to Editor code regardless of whether the
   "Project" panel itself is built. **Fixed by PHASE4/PHASE5**: the Guid
   resolution step (a fresh, throwaway `AssetDatabase::RefreshFromDirectory()`
   scan of the Project folder) lives entirely in the Editor-side glue
   (`SceneIO.cpp`), never inside `Game` itself — `Game`'s existing public API
   (`CreatePrimitiveEntity()`/`CreateMeshEntityFromGtaFile()`) is untouched.
   As of the v2 audit, note the load-bearing assumption this rests on:
   `AssetDatabase::FindByPath()` re-normalizes its input through
   `std::filesystem::absolute()` before comparing, and
   `RefreshFromDirectory()` populates its index the same way — see PHASE4's
   "Correctness invariant" callout.
6. **Culprit F — the "/Project next to the .exe" path is currently a
   PRIVATE, `GTE_ENABLE_PROJECT_PANEL`-gated implementation detail.**
   `ProjectPanel::ProjectPanel()`'s constructor (`src/Editor/Panels/ProjectPanel.cpp`)
   is the only place today that resolves `SDL_GetBasePath() + "Project"`, and
   `ProjectPanel.*`/`ProjectPanelData.*` are only compiled when
   `GTE_ENABLE_PROJECT_PANEL` is ON — confirmed directly in the root
   `CMakeLists.txt`'s `target_sources()` block. `File > Save`/`File > Open`
   must work whenever `GTE_ENABLE_EDITOR` is ON, **regardless** of that
   separate, independently-toggleable switch (they are core Editor menu
   items, not "Project panel" features) — so this path resolution cannot be
   reused from `ProjectPanelData.h` as-is. **Fixed by PHASE5**, which
   extracts a tiny, unconditionally-`GTE_ENABLE_EDITOR`-compiled
   `ResolveProjectRootDirectory()` and refactors `ProjectPanel` to call it
   too (removing the duplication rather than leaving two independent
   copies).
7. **Culprit G — no third-party JSON (or any structured text format)
   library is vendored.** Directory listing of `third_party/` confirms only
   SDL3/Vulkan/VMA/stb/KTX/saba/glm/imgui/imguizmo/googletest exist — no
   JSON library. `TODO.md`'s own "e.g. JSON" phrasing was only ever an
   illustrative suggestion, not a requirement, and the user explicitly asked
   to "keep it simple." **Fixed by PHASE3**: a small, hand-rolled,
   line-oriented text format (`GTSCENE <version>` header +
   `OBJECT`/`key=value`/`END` blocks) — simple enough to hand-write a
   forgiving parser for, with zero new dependencies.
8. **Culprit H (found in the v2 audit) — `README.md` still describes the
   hardcoded demo triangles as current behavior, and no phase updates it.**
   `README.md`'s own "Status" section (and a couple of earlier "Rendering"/
   "Entity-Component-System" paragraphs) describe `Game`'s demo scene in the
   present tense. `TODO.md`'s own opening paragraph states the house rule
   this violates: `README.md` stays focused on describing the architecture/
   status *as it exists today*, and every prior campaign closed out with a
   fresh, dated "Status" bullet rather than leaving stale prose behind.
   **Fixed by PHASE6**, which adds a step for exactly that.

## Step 3: The Plan (How will we get there?)

Execute phases 1 → 6, strictly in order — later phases' code depends on
earlier phases' new types/functions already existing and compiling:

1. **PHASE1** deletes the 3 hardcoded triangles + their private
   Pipeline/Mesh, keeps the default Camera bootstrap (renamed), and confirms
   `Shaders/Triangle.vert/.frag` stay in the build.
2. **PHASE2** adds the `PrimitiveSource` component (so a primitive
   "remembers" its shape) and `TransformHierarchy::DestroyEntityAndDescendants()`
   (so a Load can wipe exactly what it owns).
3. **PHASE3** builds the pure text format (`SceneDocument` plain data +
   `SerializeSceneDocument()`/`DeserializeSceneDocument()`), fully
   Tier-1-tested, no ECS/Renderer/filesystem I/O at all yet. As of v2, its
   `assetGuid=` validation is kind-aware (see PHASE3).
4. **PHASE4** bridges a live `Registry` + `AssetDatabase` to/from a
   `SceneDocument` (`BuildSceneDocumentFromRegistry()`/
   `ClearSerializableSceneObjects()`), still Tier-1-testable (a hand-built
   Registry + a real temp-directory `AssetDatabase`).
5. **PHASE5** adds the shared Project-root path resolver and the
   Game/Renderer-touching `SaveScene()`/`LoadScene()` glue against the
   hardcoded `TestScene.gtscene` path — the first piece of this campaign
   that is genuinely Tier 2 (needs a live `Renderer` to spawn primitives/
   mesh assets on Load), consistent with this codebase's existing
   "GPU-touching glue stays thin, its pure pieces are what get tested"
   convention.
6. **PHASE6** wires `File > Save Scene`/`File > Open Scene` (+ Ctrl+S/Ctrl+O)
   into the Editor's menu bar, updates the one call site that needs a wider
   signature, closes out stale `TODO.md` prose, and (as of v2) adds a fresh
   `README.md` "Status" bullet.

## Step 4: What We Will NOT Do (Focus)

- We will **not** add a file-open/save dialog, "Save As", or any
  user-chosen path — the path is deliberately hardcoded
  (`<Project>/TestScene.gtscene`) per the user's own explicit spec.
- We will **not** serialize arbitrary ECS components/an arbitrary entity
  graph — only the two object kinds the user named (a primitive, an asset
  instance), and only their ROOT entity's Transform/Name — never Camera,
  never physics/animation runtime state, never per-child Transform overrides
  of an asset's own sub-parts (see Design Decision #3 above).
- We will **not** vendor a JSON (or any other third-party structured text)
  library — a small hand-rolled format is sufficient and keeps this
  dependency-free (see Culprit G above).
- We will **not** build undo/redo, multi-scene, or scene-diffing — a Load
  always fully replaces whatever this feature owns; a Save always fully
  overwrites the one hardcoded file, with no confirmation prompt.
- We will **not** touch `AssetType::Scene`'s existing `*.gta`-binary-format
  intent (`AssetTypes.h`'s own comment, `AssetType::Scene = 6`) — this
  campaign's `*.gtscene` text format is a deliberately separate,
  simpler mechanism for right now; reconciling the two (e.g. wrapping a
  scene as a real tracked `*.gta` asset with its own Guid) is an explicitly
  deferred follow-up, not in scope here.

## Step 5: Their Role (What does this mean for you, the implementer?)

Treat each child phase document as a standalone work order: it names the
exact files to add/modify, the exact structs/functions/signatures to write,
the exact existing call sites to touch (with line-level context from the
current source tree), and the exact test files to add or extend. Do not
skip a phase's own test file where one is called for — every new
Tier-1-testable module in this plan must land with its `tests/` counterpart
in the same change, per `AGENTS.md`'s own "Testability & Regression Safety"
rule. Do not reorder the phases — PHASE4 cannot compile without PHASE2's
`PrimitiveSource`/`DestroyEntityAndDescendants()` and PHASE3's
`SceneDocument`; PHASE5 cannot compile without PHASE4's
`BuildSceneDocumentFromRegistry()`/`ClearSerializableSceneObjects()`; PHASE6
cannot compile without PHASE5's `SaveScene()`/`LoadScene()`.
