# PHASE0_MASTER_STRATEGY.md — STL Import Pipeline ("stl-parser-1")

**Branch:** `feature/stl-parser-impl`
**Orchestrator for:** `PHASE1_STL_MESH_LOADER_AND_PARSER.md`, `PHASE2_ASSET_IMPORTER_STL_GATING_AND_MESH_SOURCE_FORMAT.md`, `PHASE3_GENERIC_MESH_NORMAL_RECOMPUTE_AND_INSPECTOR_ACTION.md`

This document is the single entry point for this campaign. Every child phase
document below MUST be read together with this one — this file carries the
locked design decisions, the shared vocabulary, and the risk register that
apply across all three phases; the child documents carry the actual
step-by-step implementation detail for their own slice of the work.

---

## Step 1: The Goal (Where are we going?)

Make `*.stl` (STereoLithography — both the common **binary** variant and the
plain-text **ASCII** variant) an importable source format in this engine's
existing asset pipeline, exactly on par with `.pmx`/`.vmd`/image formats
today: dropping a `.stl` file onto the Editor's "Project" panel (or running
the headless `--reimport` CLI on one) must decode it into this engine's
native `MeshData` and wrap it as a real, on-disk `*.gta` (`AssetType::Mesh`)
asset — the exact same "another `*.gta` file" outcome a `.pmx` import already
produces — so it can immediately be dragged into the Scene/Hierarchy and
rendered through the engine's existing, unmodified mesh-instantiation and
rendering pipeline.

A concrete, real reference asset already sits in this repository (gitignored,
never committed — see `.gitignore`'s `/_reference/` entry) and is this
campaign's primary real-world test subject:

```
C:\Users\F5954\Documents\TAMANA\GreatTamanaEngine\_reference\pl-sky\assets\terrain.stl
```

Measured facts about this file (confirmed via a direct byte-level read of its
own header during strategy research — see `PHASE1`'s own doc for how a reader
re-derives these same numbers):

- **52,272,984 bytes** on disk.
- **Binary** STL (its own declared triangle count, read as a little-endian
  `uint32_t` at byte offset 80, exactly satisfies the binary-format size
  formula `84 + triangleCount * 50 == fileSize`).
- **1,045,458 triangles** (`3,136,374` non-shared vertices once imported —
  see Locked Design Decision 1 below).

This is a genuinely large, real-world binary STL — not a toy fixture — and is
what makes the "parse efficiently, and never blow up on a corrupt/huge
triangle count" requirements in `PHASE1` non-negotiable rather than
theoretical.

## Step 2: The Situation (Where are we now?)

This engine already has a complete, working, three-format import pipeline
that this campaign slots into **without changing its shape**:

- `src/Assets/AssetImporter.h`/`.cpp` — `ImportAssetFile()` is the one gating
  function the Editor's "Project" panel drag-and-drop (see
  `src/Editor/Panels/ProjectPanel.cpp`, `HandleExternalFileDrop()`) and the
  headless `--reimport` CLI (`src/main.cpp`) both call. It already dispatches
  by source extension into three independent branches (mesh/`.pmx`,
  motion/`.vmd`, texture/image), each of which parses the source file into a
  plain engine-native struct, serializes that struct to bytes, and wraps it
  as a `*.gta` via `AssetDatabase::ImportAsset()`. Every branch degrades
  gracefully to a plain, unmodified file copy if the source merely *looks*
  like its format by extension but fails to actually parse — never a hard
  failure.
- `src/Assets/MeshData.h` — the **format-neutral** in-memory mesh shape
  (`positions`/`normals`/`uvs`/`indices`/optional `skinWeights`) every mesh
  importer (today: `PmxLoader.h`) produces. `src/Assets/MeshFile.h` (de)
  serializes exactly this struct to/from the `*.gta`'s PAYLOAD bytes
  (`"GTEMESH1"` binary layout) — this is 100% source-format-agnostic already;
  **it requires zero changes for STL.**
- `src/Assets/RigFile.h` — the *optional*, PMX-specific sidecar
  (skeleton/morphs/physics/materials + per-vertex skin weights) stored in the
  `*.gta`'s METADATA section alongside the mesh payload. A boneless/riggless/
  materialless mesh already encodes this as a well-formed, all-empty blob
  today (see `RigFile.h`'s own doc comment) — **this is exactly what an STL
  import (which has none of skeleton/morphs/physics/materials) will produce,
  no new "no-rig" special case needed anywhere downstream.**
- `src/Game/Instantiation/MeshAssetGpuCatalog.cpp` — the runtime consumer that
  turns a decoded Mesh `*.gta` into GPU resources + a spawnable
  `EntityBlueprint`. Verified by inspection: when a mesh's `RigFileData`
  carries an empty `MaterialData` (as an STL import always will),
  `MeshMaterialPartitioner::PartitionMeshMaterials()` already returns exactly
  **one** slice covering the whole index range with `materialIndex == -1`,
  which `EnsureMeshAsset()` already routes through the plain, **untextured**
  `Mesh.vert`/`Mesh.frag` (position+normal) pipeline — the exact "flat grey
  clay" rendering path a boneless/materialless PMX model already uses today.
  **This confirms the entire runtime rendering/instantiation path needs NO
  code changes at all for STL** — this campaign is scoped purely to
  "parse the file, then reuse everything else."

What's genuinely missing, and what this campaign's three child phases add:

1. **No STL reader exists at all.** `PHASE1` adds it.
2. **`AssetImporter` doesn't know `.stl` is a mesh format.** `PHASE2` wires it
   in (and adds a small `MeshSourceFormat` field so the Editor/tests can tell
   a PMX-derived Mesh `*.gta` apart from an STL-derived one later).
3. **No way to fix bad normals after the fact.** Real-world STL files
   frequently carry sloppy/all-zero per-facet normals from the exporting CAD
   tool. `PHASE3` adds a generic (not STL-specific), Tier-1-testable
   "recompute every normal from the mesh's own vertex geometry" pure
   function, a thin persistence wrapper that rewrites just a Mesh `*.gta`'s
   payload in place, and an Inspector button that calls it on demand — this
   satisfies the explicit product decision below (Locked Design Decision 3).

## Step 3: The Plan (How do we get there?)

Three sequential, independently buildable/testable phases. Each one is a
self-contained unit of work: new/changed source files, new/changed test
files, the exact `CMakeLists.txt` registrations needed, and its own
completion-report expectation. Implementation happens in **Phase 2 of this
overall campaign's own delegation flow** (see "Delegation Flow" below) — this
document and its three children are **strategy only**, produced in Phase 1 of
that flow.

| # | File | One-line purpose |
|---|------|-------------------|
| 1 | `PHASE1_STL_MESH_LOADER_AND_PARSER.md` | New `src/Assets/StlLoader.h/.cpp`: detects binary vs. ASCII, parses both into a `MeshData`, hardened against corrupt/truncated/hostile input, with full Tier-1 test coverage plus an optional real-file smoke test against `terrain.stl`. |
| 2 | `PHASE2_ASSET_IMPORTER_STL_GATING_AND_MESH_SOURCE_FORMAT.md` | Extends `IsImportableAsMeshAsset()` to accept `.stl`, adds a shared internal helper so both the `.pmx` and `.stl` success paths share one "write the Mesh `*.gta`" implementation, adds `AssetImportResult::meshSourceFormat`, full test coverage. |
| 3 | `PHASE3_GENERIC_MESH_NORMAL_RECOMPUTE_AND_INSPECTOR_ACTION.md` | New pure, format-agnostic `RecomputeMeshNormalsFromGeometry()` (area-weighted per-vertex accumulation — correct for both STL's non-shared vertices and PMX's shared ones), a persistence wrapper that rewrites a Mesh `*.gta`'s payload in place, an Inspector "Recompute Normals from Geometry" button, and a cache-invalidation hook so newly-spawned instances pick it up this session. |

### Locked Design Decisions

These were raised as open questions during strategy research and have been
explicitly decided by the project owner. Every child phase MUST follow these
exactly — do not re-litigate them during implementation.

1. **Non-shared vertices, one flat normal per triangle, at import time.**
   Each STL triangle's 3 vertices are pushed as 3 brand-new, never-reused
   `MeshData` entries (never welded/deduplicated by position). This is what
   gives a freshly-imported STL correct, crisp per-face ("faceted"/"flat
   shaded") normals out of the box — the same visual convention every CAD/
   mechanical/terrain STL viewer uses, and it costs nothing extra to
   implement (it is in fact the *simpler* of the two options: no spatial
   hashing/vertex-welding pass is needed at all). `indices` for an imported
   STL is therefore always the trivial identity sequence `0, 1, 2, 3, 4, 5,
   ...` (3 unique indices per triangle, never reused) — see `PHASE1`.

2. **`.stl` is gated through the SAME `IsImportableAsMeshAsset()` predicate
   `.pmx` already uses**, not a new, parallel predicate/branch/flag. This
   matches `AssetImporter.h`'s own existing forward-looking doc comment
   ("A future OBJ/glTF importer would extend this same predicate... rather
   than inventing a separate gating function"). Concretely:
   `IsImportableAsMeshAsset(".stl")` must return `true`, and
   `AssetImporter.cpp`'s single mesh-import branch dispatches *internally* by
   extension to either `LoadPmxModel()` or the new `LoadStlModel()`. The
   direct, load-bearing consequence: `src/Editor/Panels/ProjectPanel.cpp`'s
   own drag-and-drop `.gta`-renaming check (`HandleExternalFileDrop()`,
   currently `if (IsImportableAsKtx2Texture(extension) ||
   IsImportableAsMeshAsset(extension))`) requires **zero changes** — a
   dropped `.stl` is automatically renamed to `.gta` and imported correctly
   the moment `PHASE2` lands, with no other file touched for that to work.

3. **Trust the file's stored per-facet normal at import time (matches
   Unity's own default mesh-import behavior); ALSO ship a generic, on-demand
   "Recompute Normals from Geometry" Inspector action.** `LoadStlModel()`
   itself does not discard a well-formed, non-degenerate stored normal — it
   only recomputes a normal *at parse time* when the file's own value is
   degenerate (near-zero length; see `PHASE1`, `Vec3::Normalize()`'s existing
   "returns `Zero()` instead of NaN" contract). Separately (`PHASE3`), a
   brand-new, fully generic (NOT STL-specific — it operates purely on
   whatever `MeshData` a Mesh `*.gta` already contains, so it works
   identically on a PMX-derived mesh too) recompute action is exposed as an
   Inspector button for any already-imported Mesh asset, satisfying "trust
   the file, but let the user recompute from actual vertex data if they
   choose to."

4. **`AssetImportResult` gains a small `MeshSourceFormat` enum field**
   (`Unknown` / `Pmx` / `Stl`), populated whenever `convertedToMeshAsset` is
   `true`. This is intentionally minimal scope for THIS campaign (a plain
   enum copy, no UI work required to land it) — a future phase is free to
   surface it in the Inspector's `BuildGtaMeshMetadata()` (`Editor/Panels/
   InspectorPanel.cpp`) as "Source Format: STL"/"Source Format: PMX", but
   wiring that particular UI string is NOT required by this campaign; only
   the field itself, tests for it, and `AssetImportResult::message` already
   mentioning the format in its human-readable text (see `PHASE2`).

5. **No axis/winding/scale remapping of any kind at STL import time** —
   `LoadStlModel()` copies every position/normal straight out of the file, in
   the exact order/handedness it finds them, mirroring `PmxLoader.h`'s own
   explicit precedent and its own documented "a future step wiring this into
   the render pipeline should account for that if a loaded model's
   orientation/winding looks wrong" disclaimer. If `terrain.stl`'s visual
   orientation looks wrong once actually spawned in a Scene, that is an
   explicitly out-of-scope, documented follow-up item for a future campaign,
   not something to silently "fix" inside `LoadStlModel()` itself.

### Shared Vocabulary (used identically across all three child phases)

- **"Binary STL"**: 80-byte free-form header, then a little-endian `uint32_t`
  triangle count, then that many fixed 50-byte triangle records (12 bytes
  normal + 12 bytes vertex-1 + 12 bytes vertex-2 + 12 bytes vertex-3, each as
  3 IEEE-754 `float`s, + a trailing 2-byte "attribute byte count" this engine
  always ignores).
- **"ASCII STL"**: a plain-text format, `solid <name>` ... repeated
  `facet normal nx ny nz` / `outer loop` / `vertex x y z` (×3) / `endloop` /
  `endfacet` blocks ... `endsolid`.
- **"Degenerate normal"**: a stored normal vector whose length is at or below
  `kEpsilon` (see `src/Math/MathTypes.h`) — i.e. `Normalize()` would otherwise
  return `Vec3::Zero()`.
- **A Mesh `*.gta`**: the on-disk artifact this whole campaign produces —
  unchanged shape (`GtaHeader` + `RigFileData` metadata + `MeshData` payload)
  regardless of whether it came from a `.pmx` or a `.stl` source.

### Risk Register

| Risk | Mitigation | Owning phase |
|------|------------|--------------|
| A corrupt/hostile binary STL declares a triangle count far larger than the file actually contains (e.g. a truncated download, or a maliciously crafted 4-byte count field claiming billions of triangles) — naively trusting it and pre-allocating `3 * count` vertices could exhaust memory or crash before any bounds check ever runs. | `LoadStlModel()` MUST validate `84 + triangleCount * 50 == actualFileSize` (using the file's own real, already-known size) BEFORE reserving/allocating anything triangle-count-sized. A mismatch is treated exactly like any other corrupt file: `success = false`, descriptive `message`, empty `mesh` — never a crash, never a partial/garbage result. | PHASE1 |
| `terrain.stl` (1,045,458 triangles, ~52MB) is genuinely large — a naive per-vertex `std::vector::push_back()` with no `reserve()` could cause O(log n) reallocation churn during import (still correct, but needlessly slow on the one real asset this campaign is measured against). | Binary path: `reserve()` every output vector to the exact known final size (`triangleCount * 3`) up front, using the header's own declared count (already validated per the row above). ASCII path: a cheap up-front `std::count()` of `"facet"` occurrences in the raw text gives a good-enough reserve estimate before the real parse pass. | PHASE1 |
| Extending `IsImportableAsMeshAsset()` to accept `.stl` could silently regress `.pmx` import if the new internal per-extension dispatch inside `AssetImporter.cpp` is wired incorrectly (e.g. STL bytes accidentally routed through `LoadPmxModel()`). | PHASE2's own test suite explicitly re-runs every EXISTING `AssetImporterTests.cpp` PMX-import assertion unchanged (regression safety), plus adds the new STL-specific ones — a shared "write the Mesh `*.gta`" helper is used by both paths so there is exactly one place that logic can go wrong, not two independently-diverging copies. | PHASE2 |
| A "Recompute Normals" action that's actually implemented as "flat per-face only" would silently DESTROY a PMX model's correct smooth shading the moment a user ever clicks it on one (since PMX meshes share vertices across triangles). | `RecomputeMeshNormalsFromGeometry()` (PHASE3) is implemented as a proper **area-weighted per-vertex accumulation** (accumulate each triangle's raw, non-normalized face-normal cross product into all 3 of ITS OWN vertex-index slots, then normalize once at the end) — this is mathematically correct and safe for BOTH a non-shared mesh (STL: reduces to one flat normal per triangle, since each vertex is touched by exactly one triangle) AND a shared-vertex mesh (PMX: naturally produces smooth per-vertex normals, since a shared vertex accumulates every triangle that touches it) — the exact same algorithm real DCC tools (Blender's "Recalculate Normals", Unity's mesh-import "Normals: Calculate") use. | PHASE3 |
| Recomputing normals in place changes actual GPU vertex data (position+normal), not just optional sidecar metadata (unlike the existing `jointPhysicsOverrides` precedent) — an already-spawned Scene entity's GPU mesh will NOT retroactively update. | Explicitly documented (both in code comments and in the Inspector button's own tooltip text) as "only affects instances spawned AFTER this; existing Scene entities must be deleted and re-spawned to pick up the new normals" — the exact same one-cache-entry invalidation shape `MeshAssetGpuCatalog`/`MeshInstantiationSystem` already use for `RefreshCachedJointPhysicsOverridesFromDisk()`, just erasing the cache entry instead of patching it in place (`InvalidateCachedMeshAsset()`). This is a deliberate, honest scope boundary, not a bug. | PHASE3 |

### Delegation Flow For This Campaign (informational — not part of the code itself)

1. **Iteration 1 (this pass):** produce this master strategy doc + the three
   child phase docs only. No source code is written in this iteration.
2. **Iteration 1.5 (a focused pre-check):** `PHASE1` is this campaign's
   heaviest, most detail-sensitive document (a brand-new binary-format parser
   with hardening requirements against a real 52MB/1M-triangle file) — it
   gets its own, narrowly-scoped double-check pass before the general
   double-check below.
3. **Iteration 2 (full double-check):** re-reads all four documents in this
   folder end-to-end looking for gaps/incorrectness/insufficiency/missing
   content/worth-improving items, overwrites any file that needs it in
   place (never creates new files), and — once satisfied — delegates one
   implementation task PER child phase (three total), each pointed at this
   same folder and told to read this master doc first.

### Definition Of Done (for the whole campaign)

- `src/Assets/StlLoader.h/.cpp` exists, is registered in the root
  `CMakeLists.txt`, and `LoadStlModel()` correctly parses both a binary and
  an ASCII fixture, degrades gracefully on corrupt/truncated/oversized-claim
  input, and (when present on the machine) successfully parses the real
  `_reference/pl-sky/assets/terrain.stl` end-to-end.
- Dropping (or `--reimport`-ing) a `.stl` file produces a Mesh `*.gta`
  indistinguishable in shape from a `.pmx`-derived one (same header layout,
  same empty-but-well-formed `RigFileData`), immediately tracked by
  `AssetDatabase`, immediately spawnable/renderable with zero changes to
  `MeshAssetGpuCatalog`/`RenderSystem`/any shader.
- `AssetImportResult::meshSourceFormat` correctly reports `Stl` vs. `Pmx`.
  Every existing `.pmx`/`.vmd`/image `AssetImporterTests.cpp` assertion still
  passes unchanged.
  `IsImportableAsMeshAsset(".stl")` returns `true`.
- A user can select ANY already-imported Mesh `*.gta` in the Inspector and
  click "Recompute Normals from Geometry" to overwrite its stored normals
  with a fresh, geometry-derived, area-weighted computation — verified both
  by a Tier-1 unit test of the pure math function and a real-temp-file
  round-trip test of the persistence wrapper.
- `cmake --build build` succeeds; `ctest -C Debug --output-on-failure` (from
  the `build` directory) passes with zero regressions and the new test files
  included.
