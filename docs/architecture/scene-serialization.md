# Scene serialization
> How a scene is saved to and loaded from a `.gtscene` JSON file, and how to make a component saveable. For anyone adding components or debugging save and load.
> Verified against code: 2026-10-30

## TL;DR
- A scene is a plain JSON file, `*.gtscene`, format version 2.
- Save walks every entity and every registered component. No per-component code.
- A component is saved only if its fields are registered once in `BuiltinComponentReflection.cpp`.
- Mesh and primitive entities are rebuilt from their source on load. GPU handles are never saved.
- A bad file loads nothing. There is no partial load.

## Diagram
```text
 SAVE   Registry --BuildSceneDocumentFromRegistry--> SceneDocument --SerializeSceneDocument--> .gtscene
 LOAD   .gtscene --DeserializeSceneDocument--> SceneDocument
        ClearEntireScene() -> ReconstructSceneFromDocument()   (caller scans AssetDatabase first)
```

## File format
```json
{
  "gtscene_version": 2,
  "entities": [
    { "parent": null, "sibling_index": 0,
      "components": { "Name": { "value": "Camera" },
                      "Transform": { "position": [0, 0, -5], "rotation": [0, 0, 0, 1], "scale": [1, 1, 1] },
                      "Camera": { "fovYDegrees": 60, "nearZ": 0.1, "farZ": 1000, "active": true } } },
    { "parent": null, "sibling_index": 1, "asset_guid": "0123456789abcdef0123456789abcdef",
      "components": { "Name": { "value": "Model" } } }
  ]
}
```

| Key | Meaning |
|---|---|
| `gtscene_version` | Must be `2`. Any other value or a missing key makes the load fail. There is no migration. |
| `parent` | `null`, or the array index of the parent entity. Forward references are fine. An index out of range fails the whole file. |
| `sibling_index` | Order among siblings. Default 0. |
| `asset_guid` | Optional. Only on a root spawned from a mesh asset. Stored as 32 hex digits. |
| `components` | One key per registered component type. Unknown keys and components are ignored. |

The file is pretty-printed with 2 spaces. `Vec3` is a 3-element array. `Quat` is a 4-element array.

## How save works
1. `BuildSceneDocumentFromRegistry(registry, assetDatabase)` visits every root entity and all descendants.
2. For each entity it asks every type in `ComponentTypeRegistry` whether the entity has it. If yes, it writes the fields.
3. For an entity with `MeshAssetSource`, it resolves the `.gta` path to a Guid through the database.

## How load works
1. The caller reads and parses the file. It scans a fresh `AssetDatabase` and calls `ClearEntireScene`.
2. **Pass A, create.** Each record is spawned by kind:
   - `PrimitiveSource`: `CreatePrimitiveEntity`.
   - Known `asset_guid`: `CreateMeshEntityFromGtaFile`. Saved children are then matched by name to the new part children (first unused match).
   - Unknown Guid: the whole saved subtree is skipped.
   - Anything else: a bare entity with a default `Transform`.
3. **Pass B1.** Parents and sibling order are set in one `AssignHierarchy` call.
4. **Pass B2.** Saved component fields are applied generically. A malformed field keeps its current value.
5. `EnsureDefaultCameraExists` runs, so a scene without a camera still has one.

`ReconstructSceneFromDocument` does not clear the scene and does not scan assets. Both belong to the caller.

## Reflected components
| Component | Saved fields |
|---|---|
| `Transform` | `position`, `rotation`, `scale` |
| `Name` | `value` |
| `Camera` | `fovYDegrees`, `nearZ`, `farZ`, `active` |
| `DirectionalLight` | `color`, `illuminanceLux`, `active` |
| `PrimitiveSource` | `type` |

These are never reflected, on purpose:

| Component | Why not |
|---|---|
| `MeshRenderer` | Holds session-local GPU handles. A saved handle would point at a random resource. It is rebuilt on spawn. |
| `MeshAssetSource` | A path is not stable. It is saved as `asset_guid` at record level. |
| `SkeletalAnimator` | Restoring it needs `PlayAnimationOnEntity` side effects. Not done yet. |
| `DynamicChainRig` | Live simulation state. A fresh load starts from the rest pose. |
| `ResolvedAnimationPose` | Derived data. Recomputed every frame. |
| `Transform::parent`, `siblingIndex` | Saved at record level as `parent` and `sibling_index`. |

## How to make a component saveable
1. Add the component header in `src/ECS/Components/`. Keep it plain data: values, handles, path keys. No pointers.
2. Register it once in `src/ECS/Reflection/BuiltinComponentReflection.cpp`.
3. Add a test in `tests/Scene/` or `tests/ECS/`.

```cpp
RegisterComponentType<MyComponent>("MyComponent", {
    GTE_REFLECT_FIELD(MyComponent, someFloat),
    GTE_REFLECT_ENUM_FIELD(MyComponent, someEnum, ToString, TryParseMyEnum),
});
```

Nothing in `src/Scene/` or `src/Network/` changes. Save, load and hot reload pick the type up.

## Rules
1. A missing JSON key keeps the current value. A JSON error becomes a clean `false` with a message. Nothing throws out.
2. `ComponentTypeRegistry::Instance()` bootstraps itself on the first call. Set the "bootstrapped" flag before registering. Registration calls `Instance()` again.
3. Include `MathJsonAdapters.h` before the reflect macros when a field is a `Vec3` or `Quat`.
4. Change a parent only with `SetParent` or the hierarchy assign call. Never write `Transform::parent` by hand.
5. `SceneBuilder` is in `gte_core`. It needs a `Renderer` for load. The editor and project hot reload both reuse it.

## Where it is used
| Caller | Entry point |
|---|---|
| Editor | `Editor/SceneIO.*`. Ctrl+S saves and Ctrl+O loads `<exe dir>/Project/TestScene.gtscene`. |
| HTTP | `POST /save_scene` and `POST /load_scene` with an optional `{"path": "..."}`. Empty means the default path. |
| Hot reload | Capture and restore of the live scene, including project-defined components. See [Project assembly](../guides/project-assembly.md). |

## Key files
| File | Role |
|---|---|
| `src/Scene/SceneDocument.h` | Plain records, no ECS types |
| `src/Scene/SceneJsonFormat.*` | `kSceneJsonFormatVersion`, serialize, deserialize |
| `src/Scene/SceneBuilder.*` | Registry to document, and back |
| `src/ECS/Reflection/` | Descriptors, registry, field macros, math adapters |
| `src/Editor/SceneIO.*` | File and path glue |

## Gotchas
- A hand-edited child part keeps its transform only if the child names still match. Unnamed parts match in order.
- A moved or deleted asset drops its whole saved subtree. The load still succeeds.
- Animation state is lost on load. Play the clip again.
- A save failure over HTTP gives `500`. A load failure gives `400`.

## Related
- [ECS](ecs.md) · [Asset pipeline](asset-pipeline.md) · [Animation and physics](animation-physics.md)
- [Project assembly](../guides/project-assembly.md) · [HTTP routes](../debugging/http-routes.md)
