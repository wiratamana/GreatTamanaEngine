# Scene Serialization

_Part of [GreatTamanaEngine](../../AGENTS.md)'s contributor conventions. See
[docs/README.md](../README.md) for the full documentation index._

The `scene-serialization-1` campaign (`task_manager/scene-serialization-1/`,
`PHASE0_MASTER_STRATEGY.md`) gave the engine its first real Save/Load loop -
a new, always-compiled `src/Scene/` module (`SceneDocument.h` - plain data,
`SceneTextFormat.h/.cpp` - a hand-rolled, Tier-1-tested text format, zero
ECS/Renderer/filesystem dependency, `SceneBuilder.h/.cpp` - the ECS-facing
bridge) plus `src/Editor/SceneIO.h/.cpp` (`SaveScene()`/`LoadScene()`, the
Game/Renderer/filesystem-touching glue) wired into `File > Save Scene`
(Ctrl+S) / `File > Open Scene` (Ctrl+O) (`DockLayout.cpp`). See `README.md`,
"Status", for the full user-facing rundown. Follow these rules whenever
touching this feature:

- **`Scene/SceneBuilder.h` only ever walks ROOT entities, and only ever
  recognizes `PrimitiveSource`/`MeshAssetSource`.** A future third
  serializable "kind" needs a matching new `SceneObjectKind` enumerator, a
  new `SceneTextFormat.cpp` key, and a new branch in both
  `BuildSceneDocumentFromRegistry()`/`LoadScene()`'s spawn dispatch - never a
  silent special case bolted on elsewhere.
- **`Editor/SceneIO.cpp`'s `AssetDatabase` is always a fresh, throwaway scan,
  never cached across calls.** A future change must not start persisting it
  without re-deriving whether that's still safe given `AssetDatabase`'s own
  `FindByGuid()`/`FindByPath()` pointer-lifetime caveats (see
  `AssetDatabase.h`'s own doc comments) - see PHASE0's own "Culprit E"
  writeup for why this lookup deliberately lives in Editor-side glue,
  never inside `Game` itself.
- **`Scene/SceneTextFormat.cpp`'s `kind == Asset` requires a genuinely valid
  `assetGuid`, checked exactly once, at each object's `END` line.** A future
  new `SceneObjectKind` that also carries a `Guid` reference should apply
  this exact same "validate at END, scoped to that one kind" pattern, rather
  than reintroducing an earlier, unscoped, per-line check this campaign's
  own PHASE3 revision deliberately removed (see PHASE0's "Second-Iteration
  Audit").
- **Only a ROOT entity's Transform/Name round-trips - never a multi-part
  asset's own child "submesh part" entities.** A Load always re-derives a
  spawned asset's children fresh from the asset itself
  (`Game::CreateMeshEntityFromGtaFile()`), so a manual per-child Transform
  tweak does not survive a save/load round-trip - an explicitly accepted
  simplification (see PHASE0's Design Decision #3), not a bug to "fix" by
  serializing children too without a fresh design discussion first.
