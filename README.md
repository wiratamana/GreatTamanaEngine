# GreatTamanaEngine

A raw game engine built from scratch.

## Goal

The plan is to develop a raw game engine from scratch, with the very foundation
built on **SDL3** (the new generation after SDL2) for window and event handling.

## Architecture

The very first architecture layers the engine like this:

```
SDL -> Application -> Window and Renderer -> Game
```

- **Application** is the only layer that knows about SDL directly. It owns the
  main loop and is responsible for initializing/shutting down SDL.
- **Window** and **Renderer** are custom objects that act as an abstraction
  layer on top of SDL. Other layers (like Game) interact with these custom
  objects instead of touching SDL directly.
- **Game** sits on top of Window and Renderer, and has no direct knowledge of
  SDL either.

At this stage, Window and Renderer will still internally depend on SDL
objects — that's okay for now. The abstraction can be tightened later as the
engine evolves.

### Event handling

SDL's raw event stream never reaches `Game` (or anything else past
`Application`) directly. Each frame, `Application::Run()` polls SDL and turns
every event into the engine's own vocabulary before anything else sees it:

```
SDL_Event -> EventTranslator -> gte::Event -> InputState.Apply() + Game::OnEvent()
                                                                 \-> Game::Update(dt, InputState)
```

- **`EventTranslator`** (`src/Application/EventTranslator.h/.cpp`) is the only
  other place besides `Application` itself allowed to touch `SDL_Event`. It
  translates each raw SDL event into `gte::Event` (`src/Event/Event.h`), using
  engine-owned `KeyCode`/`MouseButton` enums instead of SDL's — so nothing
  past this point ever needs to know SDL exists.
- **`InputState`** (`src/Input/InputState.h/.cpp`) tracks continuous,
  queryable input state (held keys/buttons, mouse position/delta), built up
  by applying translated events. It's passed into `Game::Update()` for
  polling-style input, e.g. `input.IsKeyDown(KeyCode::W)` for movement while a
  key is held.
- **`Game::OnEvent(const Event&)`** is called once per translated event, for
  discrete/one-shot reactions (window resized, a key just pressed, quit) —
  as opposed to the continuous polling done via `InputState` in `Update()`.

### Math

`src/Math/` (`Vec2`/`Vec3`/`Vec4`/`Mat4`/`Quat`) is a from-scratch math
library — no GLM dependency, the same "own the core data model" philosophy
as the hand-rolled ECS below. `Mat4` is column-major/column-vector (matches
GLSL's `mat4` layout exactly, so `Mat4::Data()` uploads to a push
constant/uniform with zero transpose) and the engine's coordinate system is
left-handed, Y-up, Z-forward.

### Rendering

`Renderer` (`src/Renderer/Renderer.h/.cpp`) owns a real Vulkan pipeline built
on top of a set of small RAII wrappers under `src/Renderer/Vulkan/`
(`VulkanInstance` -> `VulkanSurface` -> `VulkanDevice` -> `VulkanSwapchain`),
using **dynamic rendering** (no `VkRenderPass`/`VkFramebuffer`) instead of
SDL's `SDL_Renderer`. Its public surface is still just `Clear()`/`Present()`,
plus `RenderOffscreen()`/`CreateRenderTexture()` for drawing into an
off-screen `RenderTexture` instead of the swapchain — the primitive behind
the Editor's Unity-style "Game"/"Scene" panels, each with its OWN
`RenderTexture` now (see "Editor / Debug UI" below) that the entity holding
the active `Camera` component renders into, which the Editor then displays
inside its own `ImGui::Image()` panel.
Vulkan itself is accessed exclusively through **volk** (a dynamic meta-loader,
see `BUILDING.md`) — nothing in the engine links a classic Vulkan loader
import lib or calls `vulkan.h` functions directly without going through it.
GPU memory is allocated exclusively through **VMA** (Vulkan Memory
Allocator, see `BUILDING.md`) via `VulkanAllocator`
(`src/Renderer/Vulkan/VulkanAllocator.h/.cpp`) — an RAII wrapper owning a
single `VmaAllocator` that `Renderer` creates once alongside its
instance/device and hands to every GPU resource type
(`RenderTexture`/`Buffer` today) to create its images/buffers through
(`vmaCreateImage`/`vmaCreateBuffer`) instead of each one hand-rolling its own
memory-type lookup and alloc/bind/free calls. `Buffer`
(`src/Renderer/Buffer.h/.cpp`) is the general-purpose GPU buffer primitive
for vertex/index/uniform/staging data, created via
`Renderer::CreateBuffer()`/`CreateDeviceLocalBuffer()` — see
`BufferMemoryUsage` (`Buffer.h`) for the `GpuOnly` (device-local,
not CPU-mappable) vs. `CpuToGpu`/`GpuToCpu` (persistently host-mapped)
distinction. `Renderer::CreateDeviceLocalBuffer()` covers the common
"static GPU-only buffer initialized once" case (vertex/index buffers) by
uploading through a temporary staging `Buffer` and copying it in via
`Renderer::ImmediateSubmit()` — a general one-time-submit-and-wait command
buffer helper, also reusable for future one-off GPU work (e.g. image layout
transitions, mipmap generation) outside the per-frame `Present()`/
`RenderOffscreen()` recording. `Mesh`/`Pipeline` themselves are still
returned by value from `Renderer::CreateMesh()`/`CreatePipeline()`
unchanged — `Renderer` has zero knowledge that an ECS exists; see
"Entity-Component-System" below for how something else (`RenderSystem`)
owns/addresses them by handle. `Pipeline` carries one push constant range: a
`mat4 model` immediately followed by a `mat4 viewProj` (vertex stage, 128
bytes total — the guaranteed minimum `maxPushConstantsSize` on every
conformant Vulkan implementation), and `Renderer::Submit()`/
`FrameRecorder::Submit()` take an optional model matrix AND an optional
view-projection matrix (both `Mat4::Identity()` by default) recorded via
`vkCmdPushConstants` right before each draw as
`pc.viewProj * pc.model * vec4(position, 1.0)` — see
`Shaders/Triangle.vert`'s matching `layout(push_constant)` block. A scene
with no active `Camera` pushes an identity `viewProj`, preserving this
engine's original "vertices already authored directly in clip space"
triangle-demo behavior.
`Mesh` (`src/Renderer/Mesh.h`) now optionally carries a real INDEX buffer
alongside its vertex buffer (a second, indexed constructor — the original
non-indexed one is unchanged and still what every built-in primitive shape
uses), and `Pipeline` (`src/Renderer/Pipeline.h/.cpp`) picks its vertex
binding/attribute description via a `VertexLayout` enum —
`PositionColor` (the original `Vertex.h`, the default, used everywhere
today except below) or `PositionNormal` (a new `MeshVertex.h`: position +
a real per-vertex normal, no color). `FrameRecorder` issues
`vkCmdDrawIndexed` instead of `vkCmdDraw` whenever the submitted `Mesh` has
an index buffer. This is what lets a real imported mesh (see "Asset
Pipeline" below) be drawn through the SAME shared `Renderer`/`RenderSystem`
path as everything else — a `*.gta` `AssetType::Mesh` payload's
positions/normals/triangle indices are uploaded as-is (no per-triangle
vertex duplication) and rendered via a small, always-compiled "grey clay"
shader pair (`Shaders/Mesh.vert/.frag` — fixed-direction lambert + ambient,
no textures yet, since a Mesh asset carries no material data — see "Asset
Pipeline" below), through `Game::CreateMeshEntityFromGtaFile()`
(`src/Game/Game.h/.cpp` — mirrors `CreatePrimitiveEntity()` below).
Every render target (the swapchain, or a `RenderTexture`) is paired with a
real **`DepthBuffer`** (`src/Renderer/DepthBuffer.h/.cpp`) at a format
queried once from the physical device (`VulkanDevice::PickDepthFormat()`,
surfaced as `Renderer::DepthFormat()` — the exact depth counterpart to
`ColorFormat()`, same "Render Target Format Matching" discipline), and
`Pipeline` always depth-tests/writes (`VK_COMPARE_OP_LESS`). The swapchain
gets one `DepthBuffer` per swapchain image (`FramePresenter`, indexed by
swapchain image index rather than frame-in-flight slot — the same "a
just-acquired image is guaranteed free to reuse" guarantee
`VulkanFrameSync`'s render-finished semaphores already rely on); each
`RenderTexture` (Editor "Game"/"Scene" views) owns one companion
`DepthBuffer` of its own, resized alongside its color image. This is what
actually makes real (non-coplanar) 3D geometry — the built-in primitive
shapes below — render correctly occluded instead of drawing in whatever
order it happened to be submitted in; the engine's original hardcoded
triangle demo never exposed this gap since its geometry was always flat and
non-overlapping in screen space.

### Entity-Component-System (ECS)

The engine's Scene/World data model lives under `src/ECS/`: `Entity`
(cheap, generational index+id, never a pointer/string), `EntityManager`
(id allocation/recycling), `ComponentStorage<T>` (a sparse-set pool per
component type), and `Registry` (owns one of each). Rolled by hand rather
than via a third-party library (EnTT), the same "own the core data model"
choice as `src/Math/` not depending on GLM. `Transform`
(`ECS/Components/Transform.h`), `MeshRenderer`
(`ECS/Components/MeshRenderer.h`), and `Camera` (`ECS/Components/Camera.h`)
are the three components that exist today —
all plain data, no behavior beyond small pure-math helpers, no GPU/SDL
ownership of their own.
`MeshRenderer` references a mesh/pipeline purely by handle
(`MeshHandle`/`PipelineHandle`, `src/Renderer/MeshHandle.h`/
`PipelineHandle.h`) — the exact same cheap, generational, index+generation
shape as `Entity` and `GpuResourceHandle`, minted by a generic
`ResourcePool<T, HandleT>` (`src/Renderer/ResourcePool.h`) rather than ever
embedding a live `Mesh`/`Pipeline` in a component. `Camera` is
perspective-only for now (`fovYDegrees`/`nearZ`/`farZ`/`active`), with two
pure-math helpers — `ProjectionMatrix(aspect)` (via
`Mat4::PerspectiveFovLH_ZO`) and the static `ViewMatrix(transform)` (via
`Mat4::LookAtLH`, looking down the `Transform`'s rotated `Vec3::Forward()`)
— rather than a bespoke eye/target/up triple, so a camera entity is edited
exactly like any other (Transform in the Inspector, same as everything
else).

`RenderSystem` (`src/Game/RenderSystem.h/.cpp`) is the one piece of the
engine allowed to depend on both the ECS world and `Renderer` — the same
"only one layer crosses this boundary" rule this engine already applies to
SDL (only `Application` touches it directly). `Renderer` itself never
depends on ECS in any way: `Submit()` takes plain `Mat4`s, never an
`Entity`/`Registry`. `RenderSystem::CollectRenderables()` (every entity with
a `MeshRenderer` becomes one `DrawCommand`, using its `Transform`'s world
matrix if present) and `RenderSystem::ResolveActiveCameraViewProjection()`
(the first entity with an active `Camera` becomes a combined
view-projection matrix, `Mat4::Identity()` if none exists) are both pure
functions that need nothing but a `Registry` — no live Renderer/GPU device —
so both are unit-tested exactly like the rest of ECS (see `TESTING.md`).
`RenderSystem::Draw()` is the one non-pure step that resolves DrawCommand
handles against its own `ResourcePool<Mesh, MeshHandle>`/
`ResourcePool<Pipeline, PipelineHandle>` and calls `Renderer::Submit()` with
both the per-object model matrix and the resolved view-projection matrix.
`Game` no longer holds a hardcoded `Pipeline`/`Mesh` pair at all — it owns a
`Registry` + `RenderSystem` and just creates entities/components.

### Asset Pipeline

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
  import (see "Editor / Debug UI" below) now GATES every dropped file
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
  `AssetType::Texture` (`AssetPreviewTexture::Resolve()`, see "Editor /
  Debug UI" below) decodes its KTX2 payload instead of calling stb_image,
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
  follow-up. A Mesh `*.gta` CAN now be spawned as a real, rendered
  `Transform`+`MeshRenderer` entity via `Game::CreateMeshEntityFromGtaFile()`
  (see "Rendering" above and "Editor / Debug UI" below for the Editor's own
  drag-and-drop trigger for it) — but always in its ORIGINAL BIND POSE,
  since none of the skinning/IK/morph/physics evaluation this paragraph
  describes actually runs yet.
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

### Editor / Debug UI

An optional in-engine Editor module lives under `src/Editor/`, gated by the
`GTE_ENABLE_EDITOR` CMake option (`ON` by default). `Application` only ever
talks to the `IEditorLayer` interface (`src/Editor/EditorLayer.h`); exactly
one of two implementations gets compiled in, selected purely by which `.cpp`
CMake adds:

- **`ImGuiEditorLayer`** (real, `GTE_ENABLE_EDITOR=ON`) — owns the Dear ImGui
  context (fetched from ImGui's **docking** branch — see
  `cmake/FetchImGui.cmake` — with `ImGuiConfigFlags_DockingEnable` set) plus
  its SDL3 and Vulkan backends (routed through volk), and TWO
  `RenderTexture`s — one for "Game", one for "Scene" — that Game's camera
  renders into independently, each tracking its own panel's content-region
  size/aspect ratio (Unity's "Free Aspect" behavior). Lays out a Unity-style
  default arrangement the first time it runs (built once via the
  `DockBuilder` API, then left to the user/`imgui.ini` afterwards): a
  full-viewport `DockSpace` with a top menu bar (`File > Exit`, wired to
  `IEditorLayer::WantsExit()` so `Application::Run()` can end its main loop
  the same way closing the OS window does), **"Hierarchy"** docked left,
  **"Inspector"** docked right, and **"Scene"**/**"Game"** tabbed together in
  the remaining center — drag the "Scene" tab out to split it side-by-side
  with "Game" at any time, exactly like Unity. "Hierarchy" lists every entity
  that has a `Transform` (via `Game::GetRegistry()` — the Editor's only,
  read/write, view into Game's ECS world), tags one with "(Camera)" if it
  also has a `Camera` component, and lets you select one; "Inspector"
  shows/edits the selected entity's `Transform` (position/rotation/scale),
  `Camera` (active/field of view/near-far planes) if present, and displays
  its `MeshRenderer` handles read-only.
  **Drag-and-drop instantiation:** a file dragged out of "Project" (see the
  Project panel below — `Panels/ProjectPanel.cpp`'s `BeginDragDropSource()`,
  payload = the file's absolute path,
  `EditorContext::kProjectAssetDragDropPayloadType`) can be dropped onto
  either "Hierarchy" (anywhere in the panel — `Panels/HierarchyPanel.cpp`) or
  directly onto the "Scene" viewport image (`Panels/ScenePanel.cpp`) to
  instantiate it, Unity's own "drag a model into the scene" convention — both
  drop targets just call `Game::CreateMeshEntityFromGtaFile()` and select the
  freshly spawned entity. Dropping anything other than a valid `*.gta`
  `AssetType::Mesh` file is silently ignored (see "Asset Pipeline" above).
  **Visibility-driven rendering:** `IEditorLayer::GameViewTarget()`/
  `SceneViewTarget()` each return `nullptr` (skipping that view's
  `Renderer::RenderOffscreen()` pass entirely) whenever `ImGui::Begin()`
  reported that panel wasn't actually visible last frame (an inactive dock
  tab hidden behind the other one) — while "Scene"/"Game" are tabbed
  together, only the active tab is ever rendered, at zero extra GPU cost for
  the hidden one; split them apart and both become visible/rendered
  simultaneously, each into its own `RenderTexture`.
  **Independent Scene camera:** "Game" still renders through whichever ECS
  entity currently has the active `Camera` component
  (`RenderSystem::ResolveActiveCameraViewProjection()`), but "Scene" now
  renders through its own independently-orbitable `EditorCamera`
  (`src/Editor/EditorCamera.h`) instead — Unity-style middle-mouse-drag pan
  (camera-local X/Y), mouse-wheel dolly (camera-local Z), and right-mouse-
  drag look (yaw around world up, pitch around camera-local right, clamped
  to ±89°), read from ImGui's mouse state in `Panels/ScenePanel.cpp` and fed
  into `EditorCamera::Update()` as plain values — `EditorCamera` itself has
  no ImGui/SDL/Vulkan dependency at all, so it is Tier-1-testable like the
  rest of the engine (see `tests/Editor/EditorCameraTests.cpp`).
  `Application::Run()` passes `IEditorLayer::SceneViewProjection()`'s result
  straight into `Game::Render()`'s `viewProjectionOverride` parameter for
  the Scene view specifically, bypassing ECS camera resolution for that
  view only — `Game` itself has no idea the Editor or `EditorCamera` exist
  either way.
  **Transform gizmo:** whichever entity is currently selected in "Hierarchy"
  gets a Unity-style translate/rotate/scale gizmo drawn directly over
  "Scene" (never "Game") via **ImGuizmo** (`third_party/imguizmo/`, fetched
  the same way as Dear ImGui itself — see `cmake/FetchImGuizmo.cmake`,
  wrapped by `src/Editor/TransformGizmo.h/.cpp`), plus a top-left
  Move/Rotate/Scale switcher overlay (`EditorContext::gizmoOperation`,
  `DrawGizmoOperationSwitcher()`). Always manipulates in ImGuizmo's `LOCAL`
  space (identical to `WORLD` today, since `Transform` has no
  parent-hierarchy field yet), using the Scene view's own `EditorCamera`
  (never the gameplay `Camera` entity's view/projection — see above).
  `ManipulateTransformGizmo()` decomposes the manipulated 4x4 matrix back
  into position/rotation/scale by hand rather than via
  `ImGuizmo::DecomposeMatrixToComponents()` — translation/scale are read
  straight off the matrix's own columns, and rotation goes through
  `Quat::FromMat4()` on the (unscaled) rotation columns, sidestepping any
  Euler-angle-order mismatch between this engine's own convention
  (`Quat::FromEulerDegrees()`) and ImGuizmo's, which would otherwise fight
  the mouse mid-drag. Left-click-to-select an entity by ray-casting into
  the Scene view (with a highlighted outline around the picked mesh) is a
  deliberately deferred follow-up — see `TODO.md` ("Editor / Debug UI");
  for now, selection is manual, via "Hierarchy" only.
  **Memory panel:** a Unity-Memory-Profiler-style **"Memory"** panel
  (`src/Editor/Panels/MemoryPanel.cpp`, docked full-width along the bottom —
  see `DockLayout.cpp`) shows exactly what's contributing to memory usage
  right now, across three sections: **"CPU (Engine Dependencies)"** — exact
  live byte/allocation totals for SDL and Dear ImGui specifically
  (`SdlMemoryTracker`/`ImGuiMemoryTracker`, below — each installs a
  byte-counting wrapper around that library's own allocator, so these are
  measured, not estimated); **"GPU (Tracked by Engine)"** — a header of
  aggregate totals (`Renderer::GetMemoryTotals()` — total bytes, buffer vs.
  texture bytes/count, device-local vs. host-visible vs. shared bytes)
  followed by a sortable table of every currently-live GPU resource
  (`Renderer::GetMemoryResources()`), biggest first, each row showing its
  debug name (if any — `Renderer::GetMemoryDebugName()`, Editor-only, empty/
  "(unnamed)" otherwise), type (Buffer/Texture), memory location, and size;
  and **"GPU Heap Budgets (Driver-Reported)"** — the REAL, driver-reported
  usage/budget for every Vulkan memory heap (`Renderer::GetVmaHeapBudgets()`,
  via `vmaGetHeapBudgets()`), fetched straight from VMA rather than tallied
  by this engine, letting you directly compare "what GpuMemoryTracker thinks
  is live" against "what the driver/Task Manager actually reports" for the
  same heap. Each heap row's "VMA Allocated" column shows not just a byte
  count but the full `VmaStatistics` story behind it — e.g. "64.00 MB across
  1 block (3 sub-allocations)" — since a `GpuMemoryTracker` total that looks
  much smaller than VMA's own block size isn't a tracking gap: VMA reserves
  whole `VkDeviceMemory` blocks up front (avoiding a slow, per-resource
  `vkAllocateMemory` call, and staying under `maxMemoryAllocationCount`) and
  sub-allocates individual resources out of them, so a block is often mostly
  unused headroom, not "missing" memory. The row-shaping logic itself
  (sorting, name resolution, human-readable byte formatting, heap-budget
  reshaping) lives in
  `src/Editor/MemoryPanelData.h/.cpp` as plain, ImGui-free functions
  (`BuildMemoryRows()`/`BuildHeapBudgetRows()`/`FormatBytes()`/`ToString()`),
  Tier-1-tested exactly like `EditorCamera` despite living under
  `src/Editor/` (see `tests/Editor/MemoryPanelDataTests.cpp`) — the panel
  itself (`Panels/MemoryPanel.cpp`) is a thin ImGui-table wrapper around
  them. The GPU section is the primitive the underlying `GpuMemoryTracker`
  (`src/Renderer/Memory/GpuMemoryTracker.h`) was already carrying every
  frame for every `Buffer`/`RenderTexture` this engine creates — no new
  bookkeeping was needed there, only a UI to surface it.
  **`SdlMemoryTracker`** (`src/Memory/SdlMemoryTracker.h`, class always
  compiled — SDL is used regardless of `GTE_ENABLE_EDITOR` — so it stays
  available/testable in every build config) and **`ImGuiMemoryTracker`**
  (`src/Editor/ImGuiMemoryTracker.h`, Editor-only) each install a
  byte-counting wrapper around their respective library's own allocator
  (`SDL_SetMemoryFunctions()`/`ImGui::SetAllocatorFunctions()`) — installed
  before that library's very first call
  (`Application::SdlContext`'s constructor, before `SDL_Init()`;
  `ImGuiEditorLayer`'s constructor, before `ImGui::CreateContext()`) since
  both APIs document that swapping allocators later risks a free() using a
  different allocator than whatever alloc() originally served that pointer.
  **Neither is actually installed/active in a release build**
  (`-DGTE_ENABLE_EDITOR=OFF`): `ImGuiEditorLayer` itself never compiles into
  that build (`NullEditorLayer` replaces it), and `Application::SdlContext`'s
  call to `SdlMemoryTracker::Install()` is explicitly wrapped in
  `#if GTE_ENABLE_EDITOR` for the same reason — a release build has no
  "Memory" panel to show these numbers and must not pay their real
  per-allocation tracking cost for nothing. Both are static/process-global
  (SDL's and ImGui's allocator callbacks carry no `this`-sized userdata to do
  otherwise) and Tier-1-tested despite touching a third-party library's own
  allocator directly — see `tests/Memory/SdlMemoryTrackerTests.cpp`/
  `tests/Editor/ImGuiMemoryTrackerTests.cpp` and AGENTS.md ("CPU Dependency
  Memory Tracking") for the full rationale, including why calling
  `SDL_malloc()`/`ImGui::MemAlloc()` directly in a test needs neither
  `SDL_Init()` nor a live `ImGuiContext`.
- **Project panel:** a Unity/Windows-Explorer-style **two-pane "Project"**
  panel (`src/Editor/Panels/ProjectPanel.h/.cpp`, docked alongside "Memory"
  along the bottom — see `DockLayout.cpp`), gated by its own
  `GTE_ENABLE_PROJECT_PANEL` switch (separate from `GTE_ENABLE_EDITOR` — see
  `BUILDING.md`), rooted at a real **"Project" folder created automatically
  next to the built `.exe`** (`SDL_GetBasePath()` + `"Project"`) if it
  doesn't already exist. **Left pane** — a folders-only tree of the whole
  Project (like Explorer's own left tree); clicking a folder both selects it
  and makes it the "open" folder. **Right pane** — the immediate files AND
  subfolders of whichever folder is open, behind a clickable breadcrumb;
  single-click selects an entry, double-clicking a subfolder navigates into
  it (same as Explorer). A draggable splitter sits between them. The tree is
  rebuilt from disk on a throttle (twice a second, or immediately after any
  operation below) rather than caching filesystem handles/pointers across
  frames, so anything deleted *externally* (Explorer, git, another process)
  while the Editor is running is simply gone from the next scan — never a
  dangling reference the Editor could crash on; if the currently *open*
  folder itself vanishes this way, it's walked back up to its nearest
  still-existing ancestor automatically. Right-click either pane for
  **Refresh**/**New Folder**/**Delete Selected**, or **drag a file (or
  folder) in from Windows Explorer**: dropping it directly onto a specific
  folder row (in EITHER pane) puts it inside that folder; dropping it
  anywhere else in the right pane puts it in the currently open folder
  (auto-renaming — `"name (1).ext"`, `"name (2).ext"`, ... — rather than
  clobbering an existing same-named item). The OS-level drop itself
  (`SDL_EVENT_DROP_FILE`, entirely separate from ImGui's own widget-to-widget
  drag-and-drop) is caught in `ImGuiEditorLayer::ProcessEvent()` and handed
  to `ProjectPanel::HandleExternalFileDrop()` with the drop's absolute
  desktop coordinates, resolved to an actual target folder by
  `ProjectPanelData::ResolveDropTarget()` against every folder row's own
  on-screen hit-box recorded while rendering the last visible frame. Every
  filesystem/geometry operation (`ScanProjectDirectory()`/
  `EnsureProjectRootExists()`/`ResolveDropTargetDirectory()`/
  `MakeUniqueDestinationPath()`/`FindEntryByRelativePath()`/
  `ParentRelativePath()`/`ResolveDropTarget()`, plus the
  `PathToUtf8()`/`Utf8ToPath()` UTF-8-safe path helpers so non-ASCII
  filenames display and round-trip correctly) lives in pure, ImGui-free
  `src/Editor/ProjectPanelData.h/.cpp`, Tier-1-tested (see
  `tests/Editor/ProjectPanelDataTests.cpp`) exactly like `MemoryPanelData`
  above — `Panels/ProjectPanel.cpp` itself (the one place holding
  cross-frame state: the cached tree, which folder is open vs. selected,
  both panes' rects/splitter position, a transient status message) is a
  thin class wrapper around them, never unit-tested directly, same division
  of labor as the "Memory" panel.
- **Inspector asset preview (texture + 3D mesh):** selecting a `*.gta` asset
  in "Project" makes "Inspector" show that file's real GTA-format metadata
  (GUID/`AssetType`/flags/payload size) in a scrollable region on top, a
  draggable splitter, then a Unity-style live preview pinned to the BOTTOM
  (`EditorContext::inspectorPreviewHeight`, shared by both preview kinds —
  see `Panels/InspectorPanel.cpp`'s `BuildAssetInspector()`). A
  `AssetType::Texture` asset (or any plain, not-yet-imported image file)
  gets `AssetPreviewTexture`'s contain-fit static image, decoded/uploaded
  once and cached until the selected path or its last-write-time changes
  (`src/Editor/AssetPreviewTexture.h/.cpp`). A `AssetType::Mesh` asset (the
  result of importing a `.pmx` — see "Asset Pipeline" above) instead gets
  `AssetPreviewMesh`'s LIVE, auto-rotating 3D view
  (`src/Editor/AssetPreviewMesh.h/.cpp`) — re-rendered every call (the spin
  is driven directly off `ImGui::GetTime()`, no per-frame state to track),
  auto-framed to the mesh's own bounding sphere, lit with a small,
  self-contained shader pair (`Shaders/MeshPreview.vert/.frag` — a
  position+normal vertex layout and fixed-direction lambert shading,
  deliberately separate from the engine's shared position+color `Vertex`/
  `Pipeline`/`Renderer::CreateMesh()`/`Submit()`, which have no normal
  attribute or index-buffer support at all). `AssetPreviewMesh` builds its
  own `VkPipeline`/`VkPipelineLayout` directly and records its own indexed
  draw call via a `Renderer::RenderOffscreen()` `recordExtra` callback — the
  same "an external Vulkan-based rendering backend owned by the Editor
  module" pattern Dear ImGui's own backend already uses (see AGENTS.md,
  "Editor Module Structure") — rather than extending the shared pipeline.
  Only the uploaded GPU vertex/index buffers and bounding sphere are
  cached per selected asset; the `VkPipeline` itself is built once and
  reused across every mesh asset selected afterwards. Neither preview kind
  is treated as "should have worked but failed" for a `*.gta` wrapping
  something else (a future `Scene`/`Material`/... asset) — it just falls
  through to plain file metadata, exactly like a non-image/non-mesh
  extension always has.
- **Inspector animation metadata (no live preview):** selecting a
  `AssetType::Animation` asset (the result of importing a `.vmd` — see
  "Asset Pipeline" above) shows a decoded metadata summary instead of a
  live viewer — a flat keyframe list has nothing to rasterize, so there is
  no bottom-pinned preview pane/splitter for this asset type at all, unlike
  the texture/mesh cases above. `BuildGtaAnimationMetadata()`
  (`Panels/InspectorPanel.cpp`) decodes the `*.gta`'s payload directly via
  `MotionFile.h`'s `DecodeMotionDataFromBytes()` (a plain CPU-side binary
  decode, no GPU involved) and shows: the VMD's own target model name (when
  set), the combined frame range across every populated track, per-track
  keyframe counts (bone/morph/camera/light/shadow/IK), the number of
  distinct bones/morphs actually driven, and a collapsible ("TreeNode",
  collapsed by default so a several-hundred-bone motion doesn't dominate the
  panel) scrollable list of every distinct bone/morph name — handy for
  eyeballing which rig a motion expects without leaving the Inspector. Falls
  back to header-only fields + a "Failed to decode motion data" notice if
  the payload is corrupt/truncated despite a valid `*.gta` header, same
  degrade-gracefully convention as the texture/mesh cases.
- **`NullEditorLayer`** (`GTE_ENABLE_EDITOR=OFF`) — every method is a no-op;
  `GameViewTarget()`/`SceneViewTarget()` always return `nullptr`, meaning
  "render straight to the swapchain, fullscreen". This is what makes
  `-DGTE_ENABLE_EDITOR=OFF` a genuine release/final-game build: no ImGui
  fetch, no ImGui sources compiled, no ImGui symbols linked at all — not
  just a runtime flag.

`Game` never depends on the Editor at all, in either direction — that's what
keeps turning the Editor off a zero-touch operation for gameplay code; the
Editor only ever *observes*/edits Game's ECS world through
`Game::GetRegistry()`, a public accessor Game exposes without knowing or
caring who calls it.

## Building

See **[BUILDING.md](BUILDING.md)** for prerequisites and build instructions.

## Testing

See **[TESTING.md](TESTING.md)** for how to build and run the test suite.

## Status

Early foundation stage, but past the basic-scaffolding phase for several
pieces:

- Window/Renderer/Game scaffolding is in place, and event handling flows
  through `EventTranslator`/`InputState` as described above instead of raw
  SDL events reaching `Game` directly.
- `Renderer` owns a real Vulkan pipeline (instance/device/swapchain/command
  buffers, using dynamic rendering) instead of SDL's `SDL_Renderer`, including
  off-screen rendering into a `RenderTexture` for Editor panels.
- The Editor module is wired up end-to-end: Dear ImGui (docking branch)'s
  SDL3 + Vulkan backends are integrated behind `IEditorLayer`, with a full
  Unity-style docked layout — top menu bar (`File > Exit`), "Hierarchy"
  (left), "Inspector" (right), and "Scene"/"Game" tabbed in the center, all
  freely rearrangeable/splittable via ImGui docking. "Game" and "Scene" each
  display Game's camera output via their OWN `RenderTexture` now (each
  tracking its own panel's size/aspect ratio independently), and each is
  only actually rendered into when its own panel is visible — tabbed
  together, only the active one costs any GPU time; split apart, both do;
  "Hierarchy"/"Inspector" list and edit entities/components straight from
  Game's ECS world via `Game::GetRegistry()`. Toggling `GTE_ENABLE_EDITOR`
  fully includes/excludes the whole module, down to CMake never fetching or
  compiling ImGui at all when it's off.
- "Scene" now has its own independently-orbitable Editor-only camera
  (`EditorCamera`, `src/Editor/EditorCamera.h`) with Unity-style
  middle-drag pan / wheel dolly / right-drag look controls, wired through
  a new `IEditorLayer::SceneViewProjection()` and
  `Game::Render()`'s `viewProjectionOverride` parameter — "Game" is
  unaffected and still renders through the ECS's own active `Camera`
  component. Fully unit-tested (pan/dolly/rotate math, pitch clamping,
  `ViewProjection()`) despite living under `src/Editor/`, since it has no
  ImGui/SDL/Vulkan dependency at all — see `tests/Editor/EditorCameraTests.cpp`.
- "Scene" also now has a Unity-style translate/rotate/scale **transform
  gizmo** via **ImGuizmo** (`src/Editor/TransformGizmo.h/.cpp`,
  `third_party/imguizmo/` — fetched the same way as Dear ImGui itself, see
  `cmake/FetchImGuizmo.cmake`) for whichever entity is currently selected in
  "Hierarchy", plus a top-left Move/Rotate/Scale switcher overlay
  (`EditorContext::gizmoOperation`). `ManipulateTransformGizmo()` writes the
  dragged result straight back into that entity's `Transform`, decomposed by
  hand rather than via `ImGuizmo::DecomposeMatrixToComponents()` — the
  manipulated matrix's translation/scale are read straight off its own
  columns, and rotation goes through `Quat::FromMat4()` on the (unscaled)
  rotation columns, sidestepping any Euler-angle-order mismatch between this
  engine's own convention and ImGuizmo's that would otherwise visibly fight
  the mouse mid-drag. Click-to-select via ray casting + a Scene-view outline
  highlight for the picked entity is a deliberately deferred follow-up — see
  `TODO.md` ("Editor / Debug UI"); selection today is manual, via
  "Hierarchy" only.
- The Editor now has a Unity-Memory-Profiler-style **"Memory"** panel
  (`src/Editor/Panels/MemoryPanel.cpp`, docked full-width along the bottom),
  now covering CPU AND GPU memory across three sections: **"CPU (Engine
  Dependencies)"** — exact, measured (not estimated) live byte/allocation
  totals for SDL and Dear ImGui specifically, via `SdlMemoryTracker`
  (`src/Memory/SdlMemoryTracker.h`, class always compiled for testability)
  and `ImGuiMemoryTracker` (`src/Editor/ImGuiMemoryTracker.h`, Editor-only),
  each installing a byte-counting wrapper around that library's own
  allocator (`SDL_SetMemoryFunctions()`/`ImGui::SetAllocatorFunctions()`)
  before its very first call — but neither actually installed/active in a
  release build (`-DGTE_ENABLE_EDITOR=OFF`): the install call site is
  explicitly `#if GTE_ENABLE_EDITOR`-gated (`SdlMemoryTracker`) or simply
  never compiled at all (`ImGuiMemoryTracker`, via `NullEditorLayer`
  replacing `ImGuiEditorLayer` entirely), so a shipped game pays zero
  per-allocation tracking overhead for a panel it doesn't have; **"GPU
  (Tracked by Engine)"** — aggregate totals
  (`Renderer::GetMemoryTotals()`) plus a sortable, biggest-first table of
  every currently-live GPU resource (`Renderer::GetMemoryResources()`) with
  its debug name/type/memory location/size (needed zero new bookkeeping —
  `GpuMemoryTracker`, see below, already carried all of this data every
  frame; only the debug-name forwarding, `Renderer::GetMemoryDebugName()`,
  was new here); and **"GPU Heap Budgets (Driver-Reported)"** — the REAL,
  driver-reported usage/budget for every Vulkan memory heap
  (`Renderer::GetVmaHeapBudgets()`, via VMA's `vmaGetHeapBudgets()`), the
  cross-check for whether the "Tracked by Engine" section plausibly accounts
  for everything a real GPU tool/Task Manager would report - each heap row
  also shows the `VmaStatistics` story behind its "VMA Allocated" bytes
  (`FormatBlockSummary()` - e.g. "64.00 MB across 1 block (3
  sub-allocations)"), since VMA reserves whole `VkDeviceMemory` blocks up
  front and sub-allocates resources out of them, so a much-smaller
  `GpuMemoryTracker` total is expected block-reservation headroom, not a
  tracking gap. All of the
  row-shaping logic (`BuildMemoryRows()`/`BuildHeapBudgetRows()`/
  `FormatBytes()`/`ToString()`, `src/Editor/MemoryPanelData.h/.cpp`) plus
  both CPU trackers are Tier-1-tested despite living under `src/Editor/`
  (`SdlMemoryTracker` lives outside it, in `src/Memory/`, and is tested the
  same way) - same as `EditorCamera` - see
  `tests/Editor/MemoryPanelDataTests.cpp`,
  `tests/Memory/SdlMemoryTrackerTests.cpp`, and
  `tests/Editor/ImGuiMemoryTrackerTests.cpp`.
- The Editor's **"Project"** panel is now a Unity/Windows-Explorer-style
  **two-pane** browser (`src/Editor/Panels/ProjectPanel.h/.cpp`, docked
  alongside "Memory" along the bottom), gated by its own
  `GTE_ENABLE_PROJECT_PANEL` switch (a build can disable just this panel
  independently of the rest of the Editor - see `BUILDING.md`): a
  folders-only tree on the left (click a folder to open it) and that
  folder's own files/subfolders on the right, behind a clickable breadcrumb
  (single-click selects, double-click a subfolder navigates into it), split
  by a draggable splitter. Rooted at a real **"Project" folder auto-created
  next to the built `.exe`**, rebuilt from disk on a throttle rather than
  caching filesystem handles across frames (so anything deleted
  *externally* while the Editor is running just quietly disappears from the
  next scan, and an open folder that vanishes is walked back up to its
  nearest still-existing ancestor - never a dangling reference to crash
  on), plus **drag-and-drop import**: drop a file/folder from Windows
  Explorer directly onto a specific folder row (in EITHER pane) to land it
  in that folder, or anywhere else in the right pane to land it in the
  currently open folder (auto-renaming to avoid clobbering an existing
  item) - caught via the raw `SDL_EVENT_DROP_FILE` OS event
  (`ImGuiEditorLayer::ProcessEvent()`), entirely separate from ImGui's own
  widget drag-and-drop, and resolved to a specific folder via
  `ProjectPanelData::ResolveDropTarget()` against every folder row's
  recorded on-screen hit-box. Right-click either pane for Refresh/New
  Folder/Delete Selected. All the actual filesystem/geometry logic
  (`ScanProjectDirectory()`/`EnsureProjectRootExists()`/
  `ResolveDropTargetDirectory()`/`MakeUniqueDestinationPath()`/
  `FindEntryByRelativePath()`/`ParentRelativePath()`/`ResolveDropTarget()`/
  `PathToUtf8()`/`Utf8ToPath()`) lives in pure, ImGui-free
  `src/Editor/ProjectPanelData.h/.cpp`, Tier-1-tested against a real temp
  directory - see `tests/Editor/ProjectPanelDataTests.cpp`.
- GPU memory allocation goes through **VMA** (Vulkan Memory Allocator) via
  the `VulkanAllocator` RAII wrapper (`src/Renderer/Vulkan/`) — `Renderer`
  owns a single `VmaAllocator`. `RenderTexture` creates its `VkImage` through
  `vmaCreateImage`/`vmaDestroyImage`, and `Buffer`
  (`src/Renderer/Buffer.h/.cpp`) creates `VkBuffer`s through
  `vmaCreateBuffer`/`vmaDestroyBuffer` — both replacing what used to be a
  manual `FindMemoryType()` + `vkAllocateMemory`/`vkBindMemory`/`vkFreeMemory`
  dance. `Renderer::CreateBuffer()`/`CreateDeviceLocalBuffer()` cover
  host-mapped (uniform/staging) and device-local-via-staging-upload
  (vertex/index) buffers respectively; `Renderer::ImmediateSubmit()` is the
  reusable one-shot command buffer helper behind the latter. Verified with a
  runtime smoke test (mapped-buffer round-trip + a full staging-buffer ->
  device-local-buffer copy) actually executing against a live Vulkan device,
  and building cleanly with both `GTE_ENABLE_EDITOR` `ON` and `OFF`.
- A from-scratch **Math library** (`src/Math/`: `Vec2`/`Vec3`/`Vec4`/`Mat4`/
  `Quat`) backs everything above and below — no GLM dependency. Fully
  unit-tested (multiply/transpose/inverse/`LookAtLH`/`PerspectiveFovLH_ZO`,
  `Quat` slerp/nlerp/axis-angle/Euler round-trips) against hand-verified
  exact values.
- A hand-rolled **Entity-Component-System** (`src/ECS/`: `Entity`/
  `EntityManager`/`ComponentStorage<T>`/`Registry`) is the engine's Scene/
  World data model — no third-party ECS library (EnTT), same "own the core
  data model" choice as Math. `Transform`, `MeshRenderer`, and `Camera` are
  the three components that exist today. Fully unit-tested, including
  generation-guarded stale-handle safety.
- The ECS is wired all the way into actual rendering, not just present as
  inert data: `RenderSystem` (`src/Game/RenderSystem.h/.cpp`) is the one
  class allowed to depend on both the ECS world and `Renderer` — `Renderer`
  itself gained zero ECS awareness in the process. A generic
  `ResourcePool<T, HandleT>` (`src/Renderer/ResourcePool.h`) mints
  generational `MeshHandle`/`PipelineHandle` values a `MeshRenderer`
  component can safely hold instead of ever embedding a live GPU resource.
  `Pipeline` carries a push-constant `mat4 model` immediately followed by a
  `mat4 viewProj`, threaded through `Renderer::Submit()`/`FrameRecorder`
  down to `vkCmdPushConstants`, so each entity's `Transform` genuinely
  drives where it's drawn AND a real `Camera` entity genuinely drives how
  the whole scene is viewed (rather than vertices sitting directly in clip
  space). `Game` builds a small demo scene (three entities sharing one
  mesh/pipeline, positioned via `Transform` alone, plus one `Camera` entity
  sitting back along -Z looking at them) proving the whole ECS ->
  `RenderSystem` -> `Renderer` pipeline end to end — verified both by the
  test suite (`RenderSystem::CollectRenderables()`'s pure ECS ->
  draw-command logic, `RenderSystem::ResolveActiveCameraViewProjection()`'s
  pure ECS -> camera logic, and `Camera`'s own `ProjectionMatrix()`/
  `ViewMatrix()` math) and visually (three independently-positioned
  triangles on screen, seen through a real perspective camera, in both the
  "Game" and "Scene" panels' own separate `RenderTexture`s).
- The engine can now create real, non-flat 3D geometry instead of only the
  one hardcoded 2D-on-the-XY-plane triangle: `Vertex::position` grew from a
  `vec2` to a `vec3` (`src/Renderer/Vertex.h`, `Shaders/Triangle.vert`
  updated to match), and a new `PrimitiveMeshGenerator`
  (`src/Renderer/Primitives/PrimitiveMeshGenerator.h/.cpp`) generates
  Unity-equivalent built-in primitive shapes — Cube, Sphere, Capsule, Cone,
  Plane — as plain CPU-side vertex data, entirely independent of any GPU
  device/Renderer/ECS (Tier-1-tested against hand-derived geometric
  invariants — bounding box, distance from center/core segment — see
  `tests/Renderer/PrimitiveMeshGeneratorTests.cpp`). Each vertex's color
  bakes a simple fixed-direction "faux-lit" shade (flat per-face for
  Cube/Cone/Plane, smooth per-vertex for Sphere/Capsule) rather than a flat
  placeholder gray, so a freshly spawned shape actually reads as 3D despite
  the engine's one unlit vertex-color shader. `Game::CreatePrimitiveEntity()`
  (a RUNTIME API, not Editor-only — this engine's equivalent of Unity's
  `GameObject.CreatePrimitive()`) spawns a `Transform` + `MeshRenderer`
  entity from one of these shapes, reusing one shared `Pipeline` and one
  shared `Mesh` per shape across every instance, exactly like the existing
  demo triangles share theirs. The Editor's "Hierarchy" panel exposes this
  via a Unity-style right-click **"Create 3D Object"** menu that spawns and
  immediately selects the new entity — the first concrete way to build up a
  non-hardcoded scene in the Editor, and the planned way to exercise scene
  serialization (see `TODO.md`) once that lands.
- A unified binary asset container format, `*.gta` ("Great Tamana Asset" -
  see "Asset Pipeline" above), plus an `AssetDatabase`
  (`src/Assets/AssetDatabase.h/.cpp`) tracking every one found under a
  directory tree by its embedded `Guid`. The Editor's "Project" panel
  drag-and-drop import now GATES on file type: dropping a PNG/JPEG/etc.
  decodes it and re-encodes it as an uncompressed KTX2 container (via the
  statically-linked KTX-Software library), wraps it as a `*.gta`
  (`AssetType::Texture`), and registers it immediately - every other file
  extension still imports as a plain, unmodified copy. The Editor's
  "Inspector" panel shows a live texture preview for a selected `*.gta`
  asset the same way it already does for a plain, not-yet-imported
  PNG/JPEG (`Assets/Ktx2Decoder.h/.cpp`'s `DecodeKtx2ToRgba8()`, the
  pixel-exact inverse of the encode step, feeds the exact same
  `Renderer::CreateTexture2D()` upload path `AssetPreviewTexture` already
  used) - a `*.gta` wrapping anything other than a texture just falls back
  to plain file metadata, with no spurious error message. Fully unit-tested
  (`*.gta` header/round-trip I/O, `AssetDatabase`'s scan/import/lookup
  behavior, and the PNG/JPG <-> KTX2 encode/decode steps themselves, all
  genuinely Tier 1 - no GPU device/ImGui/SDL involved) and verified
  building/passing its full test suite with `GTE_ENABLE_EDITOR` both `ON`
  and `OFF`.
- **MikuMikuDance (`.pmx`) model import**, the same "gate on file type"
  pipeline extended to a second asset kind: dropping a `.pmx` file now
  parses it via a curated, from-scratch-fetched subset of
  [benikabocha/saba](https://github.com/benikabocha/saba) (`PmxLoader.h/.cpp`,
  `cmake/FetchSaba.cmake` — no Bullet/skinning-runtime/viewer vendored, and
  its own spdlog dependency patched out), extracts per-vertex positions/
  normals/UVs plus triangle indices into a plain `MeshData`
  (`src/Assets/MeshData.h`), and wraps it as a `*.gta` (`AssetType::Mesh`)
  via a new `MeshFile.h/.cpp` binary format — the mesh equivalent of
  `Ktx2Encoder`. The Editor's "Inspector" panel shows a LIVE, auto-rotating
  3D preview for a selected Mesh asset (`AssetPreviewMesh.h/.cpp`, its own
  small position+normal Vulkan pipeline built directly in the Editor layer —
  `Shaders/MeshPreview.vert/.frag`), pinned to the bottom exactly like the
  existing texture viewer, above a metadata panel showing the mesh's real
  vertex/triangle counts. Verified end-to-end against a real, large MMD
  model (~30k vertices/~37k triangles) in addition to hand-built binary
  fixtures. Vertex-geometry import only for now — no bones/morphs/
  materials/textures/rigid bodies, no real skinning or VMD motion playback,
  and no GAMEPLAY consumption path yet (only the Editor's own Inspector
  preview renders it; nothing yet spawns a `MeshRenderer` entity from an
  imported Mesh asset) - see `TODO.md`.
- **PMX bone weights/skinning, bones, morphs, and rigid-body/joint physics
  import** — closes the "no bones/morphs/materials/rigid bodies" gap the
  previous entry called out. `PmxLoader::LoadPmxModel()` now also extracts:
  per-vertex skin weights covering all of BDEF1/BDEF2/BDEF4/SDEF/QDEF
  (bundled straight into `MeshData::skinWeights` — see `Assets/MeshData.h`),
  the full bone hierarchy including IK chains/limits and append/fixed-axis/
  local-axis bones (`Assets/SkeletonData.h`), all seven PMX morph kinds —
  Position/UV/Bone/Material/Group/Flip/Impulse (`Assets/MorphData.h`), and
  rigid bodies + joints (`Assets/PhysicsData.h`, DATA only — no Bullet or
  equivalent simulation backend is vendored). A new sibling binary format,
  `Assets/RigFile.h`'s `EncodeRigDataToBytes()`/`DecodeRigDataFromBytes()`,
  serializes all of that into the `*.gta`'s previously-always-empty
  METADATA section (`AssetImporter.cpp`), alongside the unchanged
  `MeshFile.h` geometry payload — so a boneless/riggless `.pmx` still
  imports exactly as before, and a rigged one now carries its full rig data
  along for free. Verified against the same real ~30k-vertex MMD model as
  the previous entry, which turned out to carry 387 bones, 63 morphs, 267
  rigid bodies, and 368 joints — all now correctly parsed end-to-end (see
  `tests/Assets/PmxLoaderTests.cpp`'s `PmxLoaderRealModelSmokeTest`), plus
  hand-built binary fixtures exercising every weight type/bone flag/morph
  kind/physics shape individually (`tests/Assets/RigFileTests.cpp` for the
  new binary format's own round-trip). Still import/data-extraction only —
  no GPU skinning, IK solving, morph blending, or physics simulation
  happens anywhere in this engine yet; see `TODO.md`.
- **MikuMikuDance (`.vmd`) motion import** — the model importer's companion:
  a dropped `.vmd` motion file now goes through the exact same "gate on file
  type, parse into an engine-native struct, wrap as `*.gta`" pipeline as
  `.pmx`, via a new `Assets/VmdLoader.h/.cpp` (wrapping
  [benikabocha/saba](https://github.com/benikabocha/saba)'s
  `Model/MMD/VMDFile.{h,cpp}` — newly added to the already-vendored curated
  saba subset, see `cmake/FetchSaba.cmake`) and a new `Assets/MotionData.h`/
  `Assets/MotionFile.h/.cpp`. Extracts every VMD track: bone keyframes
  (translation/rotation offset + raw bezier interpolation bytes, addressed
  by bone NAME rather than a model-specific index, matching how a `.vmd` is
  actually authored/reused across different models), morph keyframes, and
  the camera/light/shadow/IK-enable tracks a camera-work `.vmd` carries
  instead — wrapped as a new `AssetType::Animation` `*.gta`. Verified against
  the real motion file this integration was tested with (a 690-bone-keyframe
  character motion, `ChatanyaraKuushanku_bassui260717a.vmd`) via a machine-
  gated smoke test (`tests/Assets/VmdLoaderTests.cpp`'s
  `VmdLoaderRealMotionSmokeTest`), plus hand-built binary fixtures exercising
  every track (`BuildRichVmd()`) and `Assets/MotionFile.h`'s own encode/
  decode round-trip (`tests/Assets/MotionFileTests.cpp`). Import/data-
  extraction only, same as the model importer — no interpolation evaluation,
  keyframe playback, or wiring onto a model's own `SkeletonData`/`MorphData`
  by name happens anywhere in this engine yet; see `TODO.md`.
- **A dropped Mesh `*.gta` can now be instantiated AND actually rendered** —
  closes the "no gameplay consumption path" gap the PMX-import entry above
  used to call out. `Mesh` (`src/Renderer/Mesh.h`) gained a real, optional
  index buffer (a second, indexed constructor; the original non-indexed one
  is unchanged), `Pipeline` (`src/Renderer/Pipeline.h/.cpp`) gained a
  `VertexLayout` selector (`PositionColor` — the original `Vertex.h` — vs.
  `PositionNormal` — a new `MeshVertex.h` carrying a real per-vertex normal
  instead of a color), and `FrameRecorder` now issues `vkCmdDrawIndexed`
  whenever the submitted `Mesh` has one. `Game::CreateMeshEntityFromGtaFile()`
  (mirroring `CreatePrimitiveEntity()`) decodes a Mesh `*.gta`'s payload,
  uploads it once (cached per absolute path), and spawns a
  `Transform`+`MeshRenderer` entity for it, drawn through a shared,
  always-compiled "grey clay" pipeline (`Shaders/Mesh.vert/.frag` —
  fixed-direction lambert + ambient; no textures, since a Mesh asset carries
  no material data yet). The Editor wires this up as real drag-and-drop:
  dragging a file out of "Project" (`Panels/ProjectPanel.cpp`'s
  `BeginDragDropSource()`) onto either "Hierarchy" or directly onto the
  "Scene" viewport image (`Panels/HierarchyPanel.cpp`/`ScenePanel.cpp`'s
  `BeginDragDropTarget()`) instantiates and selects it, Unity's own "drag a
  model into the scene" convention. The spawned entity always renders in its
  ORIGINAL BIND POSE — no skinning/morph/IK evaluation runs yet (that
  remains explicitly deferred, see `TODO.md`). Verified against the real
  ~31k-vertex/~39k-triangle MMD model already used elsewhere in this
  session's testing, and the full test suite (342 tests) still passes.

## Roadmap

See **[TODO.md](TODO.md)** for known limitations, deliberately deferred
follow-ups (Editor and Memory Profiler), and longer-term engine roadmap
ideas.
