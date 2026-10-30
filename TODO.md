# TODO and roadmap
> Open work and known gaps in GTE. For contributors who want to pick a task.
> Verified against code: 2026-10-30

Every row was checked in `src/` on the date above. Finished work is not listed. Git history has it.
Status: `open` = nothing in code yet. `verify` = could not be proven from code alone. Check before you start.

## Editor

| Item | Why it matters | Status | Pointer |
|---|---|---|---|
| Hierarchy row menu: Delete, Duplicate | The right-click menu only creates things. Delete exists only as `POST /delete_entity`. Rename already works in the Inspector `Name` field. | open | `src/Editor/Panels/HierarchyPanel.cpp` |
| Undo and redo | Every edit is final. `Selection` is already the single gate for selection changes, so it is the place to start. | open | `src/Editor/Selection.h`, `src/Editor/EditorContext.h` |
| Pick meshes in the Scene view | A click picks only a camera, by its apex or frustum edge. A mesh cannot be picked, so no outline either. | open | `src/Editor/CameraPick.cpp`, `src/Editor/Panels/ScenePanel.cpp` |
| Save As and more than one scene | `File > Save Scene` always writes `<project root>/TestScene.gtscene`. The HTTP routes accept a `path`. | open | `src/Editor/SceneIO.cpp` |
| Bone Viewer shows the live pose | The window shows the bind pose only. It cannot compare a playing clip with the real bone position. | open | `src/Editor/BoneViewerWindow.h` |
| Own transform gizmo instead of ImGuizmo | ImGuizmo needs a source patch (`cmake/FetchImGuizmo.cmake`) and has its own conventions. Long-term, multi-day work. | open | `src/Editor/TransformGizmo.cpp` |

## Time and playback

| Item | Why it matters | Status | Pointer |
|---|---|---|---|
| `timeScale` (slow motion, fast forward) | Pause is on or off only. | open | `src/Core/Time.h` |
| Stop button with scene snapshot and revert | There is no Edit mode and Play mode split. Pause, Resume and Step are the only controls. | open | `src/Editor/PlaybackControls.cpp` |
| HTTP routes for pause, resume, step | Only the toolbar can do it. An AI agent cannot. The profiler `display_pause` flag is a different thing. | open | `src/Network/NetworkServer.cpp` |
| Keyboard shortcut for pause | Toolbar buttons only. | open | `src/Editor/PlaybackControls.cpp` |

## Frame Debugger

| Item | Why it matters | Status | Pointer |
|---|---|---|---|
| Capture the Secondary (Scene) view | The event tree skips passes scoped to the Secondary view. | open | `src/Editor/FrameDebuggerData.cpp` |
| Isolated pixels of one entity | A per-entity step shows the accumulated image, not only that entity. Needs an ID buffer, a stencil, or a draw-level replay. | open | `src/Editor/FrameDebuggerCapture.cpp` |
| Breakpoint inside a frame | You can freeze a whole frame. You cannot stop the GPU at one dispatch. Needs partial, resumable submission. | open | `src/Renderer/RenderGraph/` |

See [Frame debugger](docs/debugging/frame-debugger.md) for what works today.

## Memory profiler

| Item | Why it matters | Status | Pointer |
|---|---|---|---|
| `VkAllocationCallbacks` host-memory hook | Would show the CPU memory the Vulkan driver uses. Every create and destroy call must pass the same callbacks. No gap has been seen yet. | open | `src/Renderer/Vulkan/` |
| Per-block detail from `vmaBuildStatsString` | The panel shows heap totals only. It cannot say which resource sits in which block. | open | `src/Editor/MemoryPanelData.h` |

## Animation, physics, assets

| Item | Why it matters | Status | Pointer |
|---|---|---|---|
| Save `SkeletalAnimator` in scenes | A loaded scene loses its animations. Restore needs the `AnimationSystem` registration step, not only a field copy. | open | `src/ECS/Reflection/BuiltinComponentReflection.cpp` |
| Morph blending | Morph data is imported. Nothing applies it at runtime. | open | `src/Assets/MorphData.h` |
| Bezier curves for motion keyframes | Sampling uses linear interpolation and slerp. MMD curve bytes are kept but not decoded. | open | `src/Animation/MotionSampler.h` |
| Rigid-body and joint simulation for PMX | Rigid bodies and joints are imported as data only. Only Verlet dynamic chains and simple colliders run. | open | `src/Assets/PhysicsData.h`, `src/Physics/` |
| Skinning mode per model | `AnimationSystem::SkinningMode` is one switch for all models. It is set from the Jobs panel, not over HTTP. | open | `src/Game/Animation/AnimationSystem.h` |
| One mesh per GPU-skinned model, not two | A GPU-skinned model keeps its CPU-mode mesh and its GPU-mode mesh alive. | open | `src/Game/Animation/GpuSkinningRigCache.h` |
| Pose evaluation on the GPU | IK, append bones and forward kinematics run on the CPU. The GPU only skins. Large, long-term. | open | `src/Animation/AnimationPoseEvaluator.cpp` |
| Measured CPU versus GPU skinning cost | No saved comparison exists in the repo. Use the profiler workflow in the [HTTP cheat sheet](docs/debugging/http-cheat-sheet.md). | verify | `src/Game/Animation/` |
| MMD axis and winding check | `PmxLoader` does no axis or winding remap. A wrong convention may hide behind the preview camera. | verify | `src/Assets/PmxLoader.h` |
| Animation HTTP routes | No route plays a clip or changes skinning. This blocks AI-driven debugging of animation. | open | `src/Network/NetworkServer.cpp` |

## Rendering features

| Item | Why it matters | Status | Pointer |
|---|---|---|---|
| Save feature settings in scenes | `SceneDocument` holds entities only. Tuned Atmosphere, Shadow and other settings reset on load. | open | `src/Scene/SceneDocument.h` |
| Volumetric clouds | No cloud code exists. | open | `src/Features/` |
| Day-night cycle | The sun follows its `DirectionalLight` transform. Nothing rotates it over time. | open | `src/ECS/Components/DirectionalLight.h` |
| Point and spot lights | Only `DirectionalLight` exists as a light component. | open | `src/ECS/Components/` |

## Platform

| Item | Why it matters | Status | Pointer |
|---|---|---|---|
| Audio | `.wav`, `.mp3` and `.ogg` files are only classified as an asset type. Nothing plays them. | open | `src/Assets/AssetTypes.cpp` |
| Gameplay scripting layer | Gameplay is C++ in a Project Assembly. There is no script runtime. | open | [Project assembly guide](docs/guides/project-assembly.md) |

## How to take an item

1. Grep the pointer file. Confirm the gap is still there.
2. Read the owning doc in [docs/](docs/README.md).
3. Follow the rules in [Contributing](docs/conventions/contributing.md).
4. Add a test. Keep `ctest` green.
5. Delete the row in the same change.
