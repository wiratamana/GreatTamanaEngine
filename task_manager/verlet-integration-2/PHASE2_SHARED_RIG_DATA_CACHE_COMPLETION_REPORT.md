# PHASE2 — Shared Rig Data Cache (`src/Editor/ModelRigCache.h/.cpp`) — Completion Report

Parent: `PHASE0_MASTER_STRATEGY.md` (Culprit A/E). Implements
`PHASE2_SHARED_RIG_DATA_CACHE.md` in full.

## What was done

Extracted `BoneViewerWindow::EnsureDataLoaded()`'s ad hoc, private, window-
scoped "read a Mesh `*.gta`'s METADATA section as `RigFileData`, mtime-gated
reload" logic into a new, small, reusable, Tier-1-tested class,
`ModelRigCache` — exactly per the phase document's Step 3, with zero
deviation from the specified design:

- **`src/Editor/ModelRigCache.h`** — new file. Declares `ModelRigCache` with
  one public method, `const RigFileData* GetOrLoad(const std::string&
  absoluteGtaPath)`, plus the single-slot cache's three private members
  (`m_cachedPath`/`m_cachedWriteTime`/`m_cachedRig`). Doc comments copied
  verbatim from the phase document (Culprit A/E cross-references, the
  "returned pointer only valid until the next call" contract, and the
  deliberate choice to stay single-slot rather than a path-keyed map).
- **`src/Editor/ModelRigCache.cpp`** — new file. `GetOrLoad()` implements the
  exact same short-circuit (`absoluteGtaPath == m_cachedPath && mtime
  unchanged` → return the already-cached pointer), `ReadGtaFile()` +
  `AssetType::Mesh` header check (→ `nullptr` if not a valid Mesh asset), and
  the one subtle behavior explicitly called out in the phase document: an
  empty `gta->metadata` blob (a Mesh `*.gta` imported before rig extraction
  existed) makes `DecodeRigDataFromBytes()` fail outright on its own magic
  check, so this is explicitly mapped to a valid, empty `RigFileData{}`
  rather than surfacing as `nullptr` — preserving
  `BoneViewerWindow::EnsureDataLoaded()`'s existing "boneless mesh → valid,
  empty skeleton, not a failure" behavior for this cache's own callers too.
- **`CMakeLists.txt`** — added `src/Editor/ModelRigCache.h`/`.cpp` to the
  `GTE_ENABLE_PROJECT_PANEL` `target_sources()` block, right alongside the
  existing `AssetPreviewMesh.h/.cpp`/`BoneViewerWindow.h/.cpp` entries (same
  block, since this class is only ever used by that window and by Phase 4's
  planned Inspector section, both gated the same way).
- **`tests/Editor/ModelRigCacheTests.cpp`** — new file, 7 test cases per the
  phase document's Step 3.4 (all seven named cases it lists, no fewer):
  `ReturnsNullptrForANonexistentPath`, `ReturnsNullptrForAFileThatIsNotAMeshGta`,
  `LoadsBonesRigidBodiesAndJointsFromARealMeshGta` (one hand-built `Bone`/
  `RigidBody`/`Joint` each, with distinct field values, asserted field-by-
  field), `ReturnsAValidEmptyRigFileDataForABonelessMeshGta`,
  `ReloadsWhenTheFileIsRewrittenWithANewerMtime` (explicitly advances the
  second write's mtime by 5 seconds via `std::filesystem::last_write_time()`
  to avoid a same-tick false negative, exactly as the phase document
  anticipated), `DoesNotReloadWhenNeitherPathNorMtimeChanged`, and
  `SwitchingBetweenTwoDifferentPathsReloadsCorrectlyEachTime`. Uses a real
  temp directory created/torn down per test (mirrors
  `tests/Editor/ProjectPanelDataTests.cpp`'s own fixture exactly) and real
  on-disk `*.gta` files built via `GtaFile.h`'s `WriteGtaFile()` +
  `RigFile.h`'s `EncodeRigDataToBytes()` (fixture-construction style mirrors
  `tests/Assets/RigFileTests.cpp`'s own `BuildSampleRigData()`).
- **`tests/CMakeLists.txt`** — registered `Editor/ModelRigCacheTests.cpp` in
  the nested `GTE_ENABLE_EDITOR` → `GTE_ENABLE_PROJECT_PANEL` block (same
  nesting as `ProjectPanelDataTests.cpp`/`AssetInspectorDataTests.cpp`), plus
  added a matching descriptive "test taxonomy" paragraph in that file's own
  header comment block, in the same style/detail as the existing
  `Editor/ProjectPanelDataTests.cpp` entry.

Nothing under `src/Editor/BoneViewerWindow.*` or
`src/Editor/Panels/InspectorPanel.cpp` was touched — `ModelRigCache` exists
standalone, unused by any production call site yet, exactly as this
phase's own scope requires (Phase 3 rewires `BoneViewerWindow` onto it;
Phase 4 wires `InspectorPanel` onto it). `src/Assets/PhysicsData.h`,
`SkeletonData.h`, `RigFile.h/.cpp` were not touched either, per the phase
document's "What We Will NOT Do".

## Verification

- **Fast compile check** (per this campaign's workflow rules — no full
  build): `cmake --build build --target gte_core` — succeeded, only
  `ModelRigCache.cpp` newly compiled (plus the existing library re-linked),
  zero warnings/errors.
- `cmake --build build --target GreatTamanaEngineTests` — succeeded
  (compiled the new `Editor/ModelRigCacheTests.cpp.obj` + relinked the test
  binary).
- Targeted test run (not the full suite, per the "no full regression yet"
  workflow rule): `GreatTamanaEngineTests.exe --gtest_filter=ModelRigCacheTest.*`
  — **7/7 passed**, 0 failures, covering every case the phase document's
  Step 3.4 calls for.

## Notes for the next phase (Phase 3 — Bone Viewer view-mode dropdown/gizmo)

- `ModelRigCache`'s API surface is exactly what the phase document
  specified — no deviations (`GetOrLoad(const std::string&)` returning
  `const RigFileData*`, `nullptr` for "not a Mesh asset", non-null-but-empty
  for "boneless Mesh asset").
- Phase 3 is expected to change `BoneViewerWindow::Build()`'s own signature
  to `Build(Registry&, Renderer&, EditorContext&, ModelRigCache&)` (per
  `PHASE0_MASTER_STRATEGY.md`'s Culprit F) and delete
  `BoneViewerWindow`'s own private `EnsureDataLoaded()` bone-decoding half in
  favor of calling `ModelRigCache::GetOrLoad()` directly — this phase left
  `BoneViewerWindow.cpp` completely untouched, so that rewiring is still
  fully pending. `ImGuiEditorLayer` will need to own one shared
  `ModelRigCache` instance (per the master strategy's Step 3) and pass it
  into both `BoneViewerWindow::Build()` (Phase 3) and the Inspector's future
  "Model Part" section (Phase 4) — neither of those call sites exist yet
  after this phase.
- `git status` at the start of this phase showed the same pre-existing,
  unrelated `task_manager/separate-anim-physics-system-9/` →
  `task_manager/verlet-integration-1/` rename noted by Phase 1's own
  completion report, still sitting in the working tree, untouched by this
  phase's own commit for the same reason Phase 1 gave (not this campaign's
  history to conflate).
