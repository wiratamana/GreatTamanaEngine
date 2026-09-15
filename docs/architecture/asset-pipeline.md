# Asset Pipeline

_Part of [GreatTamanaEngine](../../README.md)'s architecture docs. See
[docs/README.md](../README.md) for the full documentation index._

`src/Assets/` is a small, always-compiled engine-level module (no
`GTE_ENABLE_EDITOR` dependency at all — it's used regardless of whether the
Editor is built in) implementing this engine's unified binary asset
container format, `*.gta` ("Great Tamana Asset"), and the in-memory
registry that tracks every one of them:

- **`*.gta` file format** (`Assets/GtaFile.h/.cpp`) — every asset, of every
  kind (image, 3D model, and whatever else follows), is wrapped in the same
  64-byte common header: a 16-byte `"GREATTAMANAASSET"` magic, an
  `AssetType` (`Assets/AssetTypes.h` — `Texture`/`Mesh`/`Material`/`Shader`/
  `Audio`/`Scene`/`Text`/`Font`/`Animation`/`Prefab`/`Other`; `Text` is
  deliberately unimplemented for now — plain text files are untouched by
  this pipeline), a format version, a 128-bit `Guid`, an `AssetFlags`
  bitmask (`Compressed`/`Encrypted`), and a payload offset separating an
  opaque metadata byte range from the asset's actual binary payload.
  `ReadGtaHeader()` reads only those 64 bytes, so indexing a whole directory
  of `*.gta` files — even ones with huge texture/mesh payloads — never
  touches their bulk data at all. Unlike Unity's `AssetDatabase` (a `*.meta`
  sidecar file per asset), each asset's `Guid` lives **inside its own
  file's header** — there is no separate file that can ever drift out of
  sync with the asset it identifies.
- **`AssetDatabase`** (`Assets/AssetDatabase.h/.cpp`) — the Unity-
  `AssetDatabase`-style in-memory registry of every tracked `*.gta` asset.
  `RefreshFromDirectory()` recursively scans a directory tree and rebuilds
  the whole `Guid`↔path index from what it finds (tolerating corrupt files
  and `Guid` collisions gracefully — the same "rebuilt from disk each time"
  philosophy already used by `ProjectPanelData::ScanProjectDirectory()`);
  `ImportAsset()`/`ImportRawFile()` write a new `*.gta` and register it
  immediately (reusing an existing asset's `Guid` when overwriting it in
  place, so a scene's cross-reference to it survives a re-import); and
  `FindByGuid()`/`FindByPath()`/`GetAssetsOfType()` are the lookup surface a
  future scene-serialization system will resolve asset references through
  (see `TODO.md`).
- **PNG/JPG → KTX2 import gating** (`Assets/Ktx2Encoder.h/.cpp`,
  `Assets/AssetImporter.h/.cpp`) — the Editor's "Project" panel drag-and-drop
  import (see [Editor / Debug UI](editor-debug-ui.md)) now GATES every dropped file
  through `AssetImporter::ImportAssetFile()`: a source image extension
  `IsImportableAsKtx2Texture()` recognizes (PNG/JPEG/BMP/TGA/GIF/PSD/HDR/
  PIC/PNM — stb_image's own supported formats) is decoded via stb_image and
  re-encoded as a single-mip, uncompressed KTX2 container
  (`VK_FORMAT_R8G8B8A8_UNORM`) via the statically-linked KTX-Software
  library (see `BUILDING.md`), then wrapped as a `*.gta`
  (`AssetType::Texture`) and registered with a `ProjectPanel`-owned
  `AssetDatabase` immediately — this is what makes the engine actually
  "know about" the resulting texture asset the instant it's imported, fully
  queryable by `Guid`/path with no separate rescan needed. Every other
  extension still lands as a plain, byte-for-byte, unmodified file copy —
  and if a file merely *looks* like a supported image by extension but
  fails to actually decode (corrupt/truncated), the import degrades
  gracefully to a plain copy too, rather than failing outright. No Basis
  Universal supercompression yet (the immediate goal was format
  *unification*, not compression ratio — see `TODO.md`), and there is still
  no GAMEPLAY consumption path (nothing yet lets a `MeshRenderer`/material
  reference a `*.gta` texture by `Guid` and have it bound to an actual
  shader descriptor — see `TODO.md`). `Assets/StbImageImpl.cpp` is now the
  ONE translation unit in the entire engine that compiles stb_image's
  implementation (moved out of the Editor-only `AssetPreviewTexture.cpp`,
  which still uses stb_image's declarations for its own live Inspector
  preview of a plain, not-yet-imported image file) — this had to live in an
  always-compiled module since the import pipeline needs real image
  decoding in every build configuration, not just when the Editor is
  enabled.
- **`*.gta` → pixels, the other direction** (`Assets/Ktx2Decoder.h/.cpp`) —
  `DecodeKtx2ToRgba8()` decodes a KTX2 container's bytes (e.g. straight out
  of a `*.gta` `AssetType::Texture` asset's own payload) back into plain
  RGBA8 pixels, the exact inverse of `EncodeImageBytesToKtx2()` above, and
  pixel-exact round-trip-tested against it (see
  `tests/Assets/Ktx2DecoderTests.cpp`). This is what lets the Editor's
  "Inspector" panel show a live texture preview for a `*.gta`-wrapped
  texture the same way it already does for a plain, not-yet-imported
  PNG/JPEG — selecting a `*.gta` asset whose own header confirms
  `AssetType::Texture` (`AssetPreviewTexture::Resolve()`, see
  [Editor / Debug UI](editor-debug-ui.md)) decodes its KTX2 payload instead of calling stb_image,
  then lands in the exact same `Renderer::CreateTexture2D()` RGBA8-upload
  path either way. Only understands the single, uncompressed
  `VK_FORMAT_R8G8B8A8_UNORM` container this engine's own encoder actually
  produces today, by design — a future Basis-Universal-supercompressed
  `*.gta` would need a matching transcode path added here (see `TODO.md`).
- **MikuMikuDance (`.pmx`) model import → Mesh `*.gta`** (`Assets/PmxLoader.h/.cpp`,
  `Assets/MeshData.h`, `Assets/MeshFile.h/.cpp`, `Assets/AssetImporter.h/.cpp`) —
  the mesh equivalent of the PNG/JPG → KTX2 pipeline above. A dropped `.pmx`
  file (`IsImportableAsMeshAsset()`) is parsed via `PmxLoader::LoadPmxModel()`,
  which wraps `saba::ReadPMXFile()` — a **curated subset** of
  [benikabocha/saba](https://github.com/benikabocha/saba) (its raw
  `Base/File`/`Base/UnicodeUtil`/`Model/MMD/{PMXFile,MMDFileString,
  SjisToUnicode}` file-reading layer only — deliberately NOT saba's
  Bullet-dependent skinning/physics runtime or its GLFW/ImGui viewer, and NOT
  its own spdlog dependency, patched out post-fetch — see
  `cmake/FetchSaba.cmake`'s header comment for the full reasoning), fetched
  the same "no submodule, download+stage on first configure" way as SDL3/
  Vulkan/VMA/ImGui, alongside its one real dependency, **glm** (header-only,
  used only inside `PmxLoader.cpp` — no `saba::`/`glm::` type ever crosses
  `PmxLoader.h`'s own public API, which only ever exposes this engine's plain
  `Vec3`/`Vec2`/`MeshData`). `LoadPmxModel()` extracts per-vertex
  positions/normals/UVs plus triangle indices into a plain `MeshData`
  (`src/Assets/MeshData.h` — the shared shape any future mesh importer,
  e.g. OBJ/glTF, would also produce), which `MeshFile.h`'s
  `EncodeMeshDataToBytes()` serializes into a simple, engine-private flat
  binary layout (magic + counts + tightly-packed position/normal/uv/index
  arrays — the mesh equivalent of `Ktx2Encoder`'s KTX2 container) and wraps
  as a `*.gta` (`AssetType::Mesh`) via `AssetDatabase::ImportAsset()`, same as
  the texture pipeline. A file that merely *looks* like a `.pmx` by extension
  but fails to actually parse degrades gracefully to a plain copy, same
  convention as a corrupt image.
  **Bone weights/skinning, bones, morphs, and rigid-body/joint physics** are
  now extracted too (this was previously an explicit gap — see `TODO.md`'s
  history): `LoadPmxModel()` additionally returns a `SkeletonData`
  (`Assets/SkeletonData.h` — the bone hierarchy, including IK chains/limits
  and append/fixed-axis/local-axis bones), a `MorphData`
  (`Assets/MorphData.h` — all seven PMX morph kinds: Position/UV/Bone/
  Material/Group/Flip/Impulse), and a `PhysicsData` (`Assets/PhysicsData.h`
  — rigid bodies and joints, DATA only, no simulation backend), plus
  per-vertex `VertexSkinWeights` bundled straight into `MeshData` itself
  (covering all of BDEF1/BDEF2/BDEF4/SDEF/QDEF). `MeshFile.h`'s own
  position/normal/UV/index binary layout is unchanged; the new skin
  weights/bones/morphs/physics instead round-trip through a sibling format,
  `Assets/RigFile.h`'s `EncodeRigDataToBytes()`/`DecodeRigDataFromBytes()`,
  which `AssetImporter.cpp` stores in the same `*.gta`'s METADATA section
  (see `GtaFile.h`'s `GtaFileData::metadata` — previously always empty for a
  Mesh asset) alongside the unchanged mesh payload. A boneless/riggless
  `.pmx` still imports successfully with an all-empty rig section. This is
  still import/data-extraction only — no GPU skinning, IK solving, morph
  blending, or physics simulation happens anywhere in this engine yet (no
  Bullet or equivalent backend is vendored); see `TODO.md` for that
  follow-up. **UPDATE: CPU skinning and IK solving are no longer a gap —
  see [the Changelog](../CHANGELOG.md)'s "A spawned MMD model can now actually be ANIMATED"
  entry and its own "IK solving AND PMX append/grant bone inheritance"
  update for `Animation/IkSolver.h`/`Animation/AppendBoneSolver.h` — morph
  blending and physics simulation are still the only remaining gaps this
  sentence originally called out.** A Mesh `*.gta` CAN now be spawned as a
  real, rendered
  `Transform`+`MeshRenderer` entity via `Game::CreateMeshEntityFromGtaFile()`
  (see [Rendering](rendering.md) and [Editor / Debug UI](editor-debug-ui.md) for the Editor's own
  drag-and-drop trigger for it) — but always in its ORIGINAL BIND POSE
  until `Game::PlayAnimationOnEntity()` is also called on it (see
  [the Changelog](../CHANGELOG.md)) — morph/physics evaluation still never runs. A multi-part (multi-material) model spawns as
  a real parent/child hierarchy, not a flat list of independent siblings:
  one plain, empty ROOT entity (`Transform` only) named after the `*.gta`
  file itself, with every submesh "part" entity attached under it — see
  [Entity-Component-System](ecs.md) for `Transform`'s own parent/child
  support, and `Game::CreateMeshEntityFromGtaFile()`'s own doc comment
  (`src/Game/Game.h`) for the exact naming rule (a part is named after its
  originating PMX material when that material itself has a name — see the
  new `Name` component, `src/ECS/Components/Name.h`).
  **Materials/textures are imported as real, Guid-referenced `*.gta` assets,
  never a raw filesystem path baked into the model.** `PmxLoader.h` extracts
  a `.pmx`'s material list + texture references into a `MaterialData`
  (`src/Assets/MaterialData.h`) whose `MaterialTextureRef` entries carry
  BOTH a `sourcePath` (the texture's resolved-at-import-time absolute path,
  kept purely as import-time diagnostic information — never read again
  afterwards) and a `Guid`. `AssetImporter.cpp`'s
  `ImportPmxMaterialTextures()` decodes/re-encodes every one of those
  source paths as its own standalone `*.gta` `AssetType::Texture` asset
  (the exact same KTX2 pipeline a plain dropped PNG/JPEG already goes
  through) into a `"<meshFileStem>_Textures"` folder sitting right next to
  the model's own destination Mesh `*.gta`, and fills in each
  `MaterialTextureRef::guid` with the result — this is what closes the
  model previously carrying a machine-local absolute path (e.g. pointing
  back out at wherever the original source `.pmx`'s own texture folder
  lived on the importing machine) baked directly into its `*.gta`, which
  would silently break/dangle on any other machine or once the source
  files moved. `Game::EnsureMeshAsset()` (`src/Game/Game.cpp`) now resolves
  a material's texture PURELY by `Guid`, through a fresh `AssetDatabase`
  scan rooted at the Mesh `*.gta`'s own directory (guaranteed to also
  cover its sibling `"..._Textures"` folder) — the exact same "asset
  manager resolves a stable id to wherever the asset actually lives"
  convention Unity's own `AssetDatabase` uses, and already established in
  this engine by `Guid`/`AssetDatabase` (see this section's own opening
  paragraph, above) —
  rather than ever reading `MaterialTextureRef::sourcePath` at load time.
  A texture slot that fails to import (missing/undecodable source file at
  import time) simply stays `Guid::Invalid()` and degrades to the
  untextured "grey clay" submesh, same as before.
- **MikuMikuDance (`.vmd`) motion import → Animation `*.gta`**
  (`Assets/VmdLoader.h/.cpp`, `Assets/MotionData.h`, `Assets/MotionFile.h/.cpp`,
  `Assets/AssetImporter.h/.cpp`) — the motion-import equivalent of the `.pmx`
  model pipeline above, for MMD's companion animation format. A dropped
  `.vmd` file (`IsImportableAsMotionAsset()`) is parsed via
  `VmdLoader::LoadVmdMotion()`, which wraps `saba::ReadVMDFile()` — the same
  curated `benikabocha/saba` subset the `.pmx` importer already vendors, now
  additionally compiling `Model/MMD/VMDFile.{h,cpp}` (see
  `cmake/FetchSaba.cmake`). `LoadVmdMotion()` extracts every VMD track into a
  plain, engine-native `MotionData` (`src/Assets/MotionData.h`): bone
  keyframes (per-bone translation/rotation offset + raw bezier interpolation
  bytes, addressed by NAME rather than a model-specific index — a `.vmd` is
  authored independently of any one model's own bone numbering), morph
  (blend-shape weight) keyframes, and the camera/light/shadow/IK tracks a
  camera-work `.vmd` carries instead (any/all of these lists may legitimately
  be empty, depending on what kind of motion was imported). `MotionFile.h`'s
  `EncodeMotionDataToBytes()` serializes all of that into a simple,
  engine-private flat binary layout (magic + length-prefixed sections per
  track, mirroring `RigFile.h`'s own shape) and wraps it as a `*.gta`
  (`AssetType::Animation`) via `AssetDatabase::ImportAsset()` — as the
  PAYLOAD this time (a motion has no separate mesh geometry to keep a
  metadata/payload split for), same overall pipeline shape as the texture/
  mesh importers. A file that merely *looks* like a `.vmd` by extension but
  fails to actually parse degrades gracefully to a plain copy, same
  convention as a corrupt image/PMX file. Verified end-to-end against a real,
  non-vendored MMD motion file (see
  `tests/Assets/VmdLoaderTests.cpp`'s `VmdLoaderRealMotionSmokeTest`), plus a
  hand-built binary fixture covering every track. Still import/data-
  extraction only — no interpolation evaluation/keyframe playback happens
  anywhere in this engine yet; see `TODO.md` for that remaining runtime work.
