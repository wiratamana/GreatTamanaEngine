# Asset pipeline
> How a source file (PMX, STL, glTF, VMD, image) becomes a `.gta` asset and then a runtime object. For anyone adding a format or debugging an import.
> Verified against code: 2026-10-30

## TL;DR
- Import turns every supported source file into one `.gta` file. Everything else is a plain copy.
- A `.gta` file has a 64-byte header, a metadata block and a payload. The Guid lives in the header.
- Textures of a model become their own `.gta` files in `<mesh>_Textures/`. The mesh finds them by Guid.
- Import never throws. Bad optional data (clusters, textures) never fails the import.
- Scenes are not `.gta`. They are `.gtscene` JSON (see [Scene serialization](scene-serialization.md)).

## Diagram
```text
 source file (Project panel drop, POST /import_asset)
        |  ImportAssetFile()                  src/Assets/AssetImporter.cpp
        v
 by extension:
   .pmx .stl .gltf  -> loader -> MeshData (+ rig, materials)
   .vmd             -> VmdLoader -> MotionData
   png jpg ... pgm  -> stb_image -> RGBA8 -> KTX2
   anything else    -> plain file copy
        v
 mesh only: cluster step -> import material textures
        v
 AssetDatabase::ImportAsset()  -> atomic write of <name>.gta
        v
 runtime: AssetDatabase.RefreshFromDirectory()   (header scan only)
          MeshAssetGpuCatalog -> decode, upload -> entities
```

## How it works
1. `ImportAssetFile()` lowercases the extension and picks the branch.
2. A mesh loader fills `MeshData`. PMX also fills rig data. glTF also fills materials.
3. The cluster step runs (see [Mesh clusters](mesh-clusters.md)). It never fails the import.
4. The importer keeps the saved joint physics overrides of an existing `.gta` at the same path.
5. The mesh is encoded first. If the arrays are inconsistent, nothing is written, not even textures.
6. Each model texture is written as a Texture `.gta`. The rig metadata then stores Guids, not paths.
7. `AssetDatabase::ImportAsset()` writes the file atomically. It reuses the Guid if the path already holds an asset.
8. At runtime the database scans headers only. `MeshAssetGpuCatalog` decodes the payload when a mesh is spawned.

`AssetImportResult` reports the outcome. The three `convertedTo*` flags are exclusive. None set means plain copy.

## Supported formats
| Source | Output `.gta` type | Loader | Notes |
|---|---|---|---|
| `.pmx` | Mesh + rig metadata | `PmxLoader` | Bones, IK, append, morphs, rigid bodies, joints, materials. Parse failure = plain copy. |
| `.stl` | Mesh | `StlLoader` | Binary or ASCII. Every triangle has 3 own vertices. UVs are zero. Parse failure = plain copy. |
| `.gltf` | Mesh + materials | `GltfLoader` | External `.bin` and images only. No `.glb`. Parse failure = import fails. |
| `.vmd` | Animation | `VmdLoader` | Keeps the MMD 30 fps frame grid. Parse failure = plain copy. |
| `.png .jpg .jpeg .bmp .tga .gif .psd .hdr .pic .pnm .ppm .pgm` | Texture | `Ktx2Encoder` | One mip. Uncompressed `RGBA8_UNORM` KTX2. Decode failure = plain copy. |
| other | none | none | Byte copy. |

- glTF is right-handed. The loader mirrors Z into the engine's left-handed, Y-up space.
- glTF node transforms are baked. Triangles are grouped per material. Tangents come from the file or MikkTSpace.
- Only `TEXCOORD_0` is read. See [PBR materials](../conventions/pbr-materials.md) for the rest.
- A plain-copy fallback hides parse errors. Read the result `message`: it says "imported as-is".

## The `.gta` container
Little-endian. The header is exactly 64 bytes (`src/Assets/GtaFile.h`).

| Offset | Size | Field |
|---|---|---|
| 0 | 16 | Magic `GREATTAMANAASSET` |
| 16 | 8 | `assetType` |
| 24 | 8 | `version` (currently 1) |
| 32 | 16 | Guid (low, high) |
| 48 | 8 | `flags` (bit 0 Compressed, bit 1 Encrypted; both unused) |
| 56 | 8 | `payloadOffset`. Bytes 64 to `payloadOffset` are metadata, the rest is payload. |

- `AssetType` values are fixed and append-only: Unknown 0, Texture 1, Mesh 2, Material 3, Shader 4, Audio 5, Scene 6, Text 7, Font 8, Animation 9, Prefab 10, Other 1000.
- Only Texture, Mesh and Animation are produced today. The rest are reserved.
- The Guid is in the header. There is no `.meta` sidecar. A Guid is 32 lowercase hex digits. All zero means "no asset".
- `ReadGtaHeader` reads 64 bytes. `ReadGtaMetadataOnly` skips the payload. `ReadGtaFile` reads all. All return `nullopt` on error.

## Payload and metadata formats
| Part | Magic | Content |
|---|---|---|
| Mesh payload | `GTEMESH1` | Counts, positions, normals, uvs, `uint32` indices. Optional tangent tail. Optional cluster tail `GCL1`. Nothing may follow. |
| Mesh rig (metadata) | `GTERIG03` | Skin weights, bones with IK, morphs, rigid bodies, joints, materials with texture Guids. Optional tails: joint physics overrides, PBR factors. |
| Motion payload | `GTEMOTN1` | Bone, morph, camera, light, mode and IK keyframes. |
| Texture payload | KTX2 | Single-mip `RGBA8_UNORM`. |

- Decoders return `nullopt` on bad data. They check counts against the remaining bytes before they allocate.
- A damaged cluster tail keeps the mesh and drops the clusters. A damaged vertex, index or tangent block rejects the file.
- An older rig blob that ends early decodes with defaults.
- A mesh without optional tails has the same bytes as the base format. An old build rejects a clustered file.
- Encoding returns an empty vector when counts exceed 32 bits. The importer then fails with "arrays are inconsistent".

## Model textures
1. Loaders store the source image path in `MaterialTextureRef::sourcePath`. It is only used during import.
2. The importer writes `<meshStem>_Textures/<i>_<name>.gta` next to the mesh `.gta` and stores the Guid.
3. At runtime `MeshAssetGpuCatalog` scans the mesh's own folder (recursively) and resolves textures by Guid only.
4. A texture that fails to import keeps `Guid::Invalid()`. The part draws with a default image. `missingTextureCount` counts them.

Keep the `_Textures` folder next to the mesh `.gta`. If you move one, move both.

## AssetDatabase
- It is an in-memory index of Guid and path. `RefreshFromDirectory` scans recursively and reads headers only.
- A corrupt file is skipped. If two files share a Guid, the first one wins.
- `FindByGuid` and `FindByPath` return pointers. They die on the next `Clear`, `Refresh` or `ImportAsset`. Copy the `AssetRecord`.
- Several callers build their own database: scene IO, hot reload, and `MeshAssetGpuCatalog` (per mesh folder). The Project panel keeps none; an import registers into a call-local database.

## Atomic writes
`src/Assets/AtomicFile.h` is the only way a `.gta` is written.

| Rule | Detail |
|---|---|
| Temp file first | Writes `<name>.gta.<n>.<n>.tmp`, then renames. A failure keeps the old file. |
| One writer | `WriterClaim` allows one writer per target in the process. The second one fails with "busy". |
| Identity check | Size and write time are checked before the rename. A change gives "changed on disk". |
| Free space | The payload plus a 1 MiB margin must fit. |
| Startup sweep | `SweepStaleTempFiles` removes orphan temp files older than 1 hour under the project root. |
| Create-only | Clone writes use it. An existing file is never replaced. |

The rename fails if another program holds the target open. It is safe against a crash, not against power loss.

## Other asset tools
| Tool | What it does |
|---|---|
| `MeshNormalRecompute` | Area-weighted vertex normals. The Inspector action rewrites only the payload. Call `InvalidateCachedMeshAsset` after. |
| `MeshTangents` | Converts authored tangents or generates MikkTSpace tangents. |
| `MeshMaterialPartitioner` | Splits the index buffer into one slice per material. |
| `ImportStamp` | `<name>.import.stamp`. Only the Sponza opener uses it to decide on a re-import (`kGltfImporterVersion`, now 4). |

The stamp is not part of the general importer.

## Key files
| File | Role |
|---|---|
| `src/Assets/AssetImporter.*` | `ImportAssetFile`, `MeshImportOptions`, `AssetImportResult` |
| `src/Assets/GtaFile.*`, `AssetTypes.*` | Container format, `AssetType`, `Guid` |
| `src/Assets/AssetDatabase.*` | Guid and path index |
| `src/Assets/MeshFile.*`, `RigFile.*`, `MotionFile.*` | Payload and metadata encode and decode |
| `src/Assets/PmxLoader.*`, `StlLoader.*`, `GltfLoader.*`, `VmdLoader.*` | Source parsers |
| `src/Assets/Ktx2Encoder.*`, `Ktx2Decoder.*`, `ImageFileDecoder.*` | Image to KTX2 |
| `src/Assets/AtomicFile.*` | Atomic writes |
| `src/Game/Instantiation/MeshAssetGpuCatalog.*` | Runtime decode, upload, parts |

## Rules
1. Decoders and importers never throw. Return `nullopt` or a result with `success = false`.
2. Never renumber `AssetType` or `MeshSourceFormat`. Only append.
3. Add new optional mesh data as a tail after the existing tails. Old files must still decode.
4. Bump `kGltfImporterVersion` when the glTF output changes.
5. No third-party type crosses a public header. `saba` and `glm` stay inside `PmxLoader.cpp` and `VmdLoader.cpp`. `stb_image` lives in one file, `StbImageImpl.cpp`.
6. `src/Assets` must not depend on `src/Editor`. The image extension list exists twice on purpose (`AssetImporter.h` and `Editor/AssetInspectorData.h`).

## How to add an asset type
1. Append a value to `AssetType` in `src/Assets/AssetTypes.h`. Update `AssetTypeFromExtension`.
2. Add `*Data.h` and `*File.h/.cpp` with a magic, `Encode` and `Decode`.
3. Add a loader.
4. Add an `IsImportableAs*` check and a branch in `ImportAssetFile`.
5. Add tests in `tests/Assets/`.

For a new mesh format: return `MeshData` (and `MaterialData`), call `FinalizeMeshAssetImport`, and append a `MeshSourceFormat` value.

## Gotchas
- PMX toon, sphere and edge data are imported but not drawn. The PMX `diffuse` color is not applied.
- The glTF emissive texture is imported but not sampled.
- Textures carry no color-space tag. The GPU picks sRGB for base color and linear for data images.
- Not supported: `.glb`, `KHR_*` extensions, glTF sampler modes, a second UV set.
- `.stl` meshes are poorly clustered because no vertex is shared. See [Mesh clusters](mesh-clusters.md).
- Morph weights are imported but never blended. See [Animation and physics](animation-physics.md).

## Related
- [Mesh clusters](mesh-clusters.md) · [Animation and physics](animation-physics.md) · [Scene serialization](scene-serialization.md)
- [PBR materials](../conventions/pbr-materials.md) · [HTTP routes](../debugging/http-routes.md) · [Overview](overview.md)
