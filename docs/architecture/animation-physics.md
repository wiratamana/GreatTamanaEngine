# Animation and physics
> How a skinned MMD model gets its pose each frame: motion sampling, IK, Verlet hair physics, CPU or GPU skinning. For anyone touching pose, skinning or chain physics.
> Verified against code: 2026-11-02

## TL;DR
- Pose evaluation is pure CPU math in `src/Animation/`. It has no ECS, GPU or file code.
- Each frame runs three stages in this order: animate, physics, skin.
- Physics is secondary motion only: Verlet dynamic bone chains (hair, skirt). It is not a rigid-body engine.
- Skinning runs on the job system (default) or as a GPU compute pass. The CPU skinning code is the reference.
- Not implemented: morph blending, MMD bezier curves, camera/light keyframe playback.

## Diagram
```text
 Game::Update()   (skipped when time is frozen)
   |
   |  1. AnimationSystem::EvaluatePoses()
   |       sample keyframes -> IK -> append -> ResolvedAnimationPose
   |  2. PhysicsSystem::Update()
   |       fixed steps, edits ResolvedAnimationPose in place
   |  3. AnimationSystem::SkinAndUpload()
   |       FK -> skinning matrices -> CPU skin + upload, or GPU bone upload
   v
 GPU mode only: skinning compute pass records later in the render graph
```

## How it works
1. `Game::CreateMeshEntityFromGtaFile` spawns a model. For a rigged model it registers the rig with `AnimationSystem` and the chains with `PhysicsSystem`. The two registrations are separate.
2. `AnimationSystem::Play` adds a `SkeletalAnimator` to the root entity. It needs a live entity, a `MeshAssetSource`, skin data and a valid clip.
3. `EvaluatePoses` advances `frame` by `dt * 30 * speed` (the VMD grid is 30 fps), then writes `ResolvedAnimationPose`.
4. `PhysicsSystem::Update` finds entities with an enabled `DynamicChainRig` and a pose. It steps the chains and rewrites the physics bones in the pose.
5. `SkinAndUpload` builds skinning matrices from the pose. Each model is processed once, strictly one at a time.
6. On a frozen frame (paused, not stepping) none of the three stages run. The pending GPU skin list is cleared.

The only data shared between the stages is the `ResolvedAnimationPose` component.

## Pose pipeline
`EvaluateAnimatedPoseBeforePhysics` in `src/Animation/AnimationPoseEvaluator.h` is the only home of the order.

| Step | Function | Why here |
|---|---|---|
| 1 | `SampleAnimationPose` | Raw keyframe offsets, including the IK target |
| 2 | `SolveIkChains` | CCD. Needs the sampled IK target. |
| 3 | `ApplyAppendInheritance` | After IK, because an append source can be an IK link |
| 4 | `ComputeSkinningMatrices` | Forward kinematics. Runs after physics. |

- `EvaluateAnimatedSkinningPose` runs all four. It calls the "before physics" function, so they cannot drift.
- Bones match by name. A bone without a track keeps its bind pose. A track without a bone is dropped. This is normal, not an error.
- Interpolation is linear for translation and slerp for rotation. The bezier bytes of the VMD are stored, not evaluated. Time outside the range clamps.

## Skinning modes
| Mode | What happens |
|---|---|
| `CpuJobSystem` (default) | The job system skins vertices in batches, then the vertex buffers are re-uploaded. |
| `GpuCompute` | Bone matrices are uploaded. A compute pass skins the vertices later in the frame. |

- Switch in the editor Jobs panel only (`Panels/JobsPanel.cpp`). No HTTP route changes the mode.
- `CollectModelsNeedingGpuSkinningThisFrame()` lists each model once, so no output buffer is written twice.
- Every GPU-skinned part owns two `Mesh` objects: one for CPU mode and one for GPU mode. This doubles its memory on purpose. The mode switch swaps the handles.
- GPU buffers (bind pose, weights, bone matrices, output) live in `GpuSkinningRigCache`. They are created at registration and never evicted.
- Kernels: `SkinVerticesPositionNormal.comp` and `SkinVerticesPositionNormalUv.comp`. Tangent (PBR) meshes are not GPU-skinned.
- The draw pass declares a `VertexBufferRead` on each skin output (`DeclareGpuSkinningReads` in `src/Application/RenderPasses.cpp`). This orders the draw after the compute write. Do not remove it.

## Dynamic chain physics
A chain is a tree of simulated bones hanging from a pinned anchor bone.

| Part | Behavior |
|---|---|
| Detection | `DetectDynamicChains` walks the PMX rigid body and joint graph from Static anchors. A chain needs at least 2 joints. |
| Diagnostics | Orphan bodies, cross-chain joints, duplicate bone assignments and joints without a PMX bend limit are reported, not silently lost. |
| Anchor | The root bone is never written by physics. It always comes from the animated pose. |
| Solver | `StepDynamicChain`: seed, Verlet integration with step-length damping, rods and extra constraints, goal pull, bend cone, then collision and bend cone alternating (at most 4 rounds, cone last) with the bind-pose fit, divergence guard. |
| Bend limit | Each joint keeps its segment inside a cone around the animated direction. The half angle is the sum of the PMX joint limits from the anchor down. A limit of 0 on a root child is an exact weld. The cone pass rebuilds the chain parent first: each joint keeps its direction (clamped into the cone) at exactly its rest length. A welded joint skips collision. |
| Divergence guard | A particle that is not finite, or farther from its animated spot than 10 times the longest root path (at least 100 units), reseeds the chain. It counts in `nan_count` and `reseed_count`. |
| Colliders | Static PMX bodies become sphere, box or capsule colliders (`ModelColliderDetection`). All chains of a model share them. A joint inside a collider in the bind pose ignores that collider. A joint near one keeps half of its largest free radius, so resting contact is not pushed again every step (`FitJointCollisionToBindPose`). |
| Wind | One world wind (`WindSettings`): direction, base strength, gust strength and frequency. Default is zero. |
| Time | Fixed step with an accumulator. Simulation runs in true world space. With interpolation on, each step blends the anchor frame of the last two render frames (quaternion slerp plus lerp of the translation, always rigid) and lerps the animated joints in the anchor frame, so a welded joint keeps one anchor-local position at every render rate. The written pose is interpolated in anchor-local space between display knots; each knot is the mean of two consecutive steps (removes step-rate jitter, adds half a step of lag). |
| Parallel | Chains run in parallel when a model has more than one chain and at least 24 joints. The outer entity loop stays sequential. |

Defaults:

| Setting | Default | Where |
|---|---|---|
| Gravity | 98 down (model units, PMX: 1 unit = 8 cm) | `GlobalPhysicsSettings` |
| Fixed step | 1/120 s | `GlobalPhysicsSettings` |
| Max steps per frame | 10 | `GlobalPhysicsSettings` |
| Damping, stiffness, mass | 0.4, 0.02, 1.0. Damping and stiffness are the fraction per 1/60 s step. | `DynamicJointSettings` (per joint) |
| Damping from PMX | `1 - exp(-d * rate / 60)`, rate 1.0 per second | `DynamicChainDetectionDefaults::pmxDampingRatePerSecond` |
| Constraint iterations | 4 | `DynamicChainDefinition` |
| Collision | on | `DynamicChainDefinition::collisionEnabled` |

- The solver converts the step fractions to the real step length with `ScaleStepFraction`. Changing the step rate does not change the look.
- Stiffness is low on purpose. An idle T-pose model sags where you can see it.
- `DynamicChainRig::enabled = false` returns the bones to the animated pose. `frozen = true` holds the last shape and keeps it riding with the entity.
- Per-joint edits are saved into the mesh `.gta` metadata by bone index (`SaveJointPhysicsOverridesToGtaFile`). Re-import keeps them. Their numbers keep the same meaning (fraction per 1/60 s).
- Edit them in the Inspector section "Dynamic Chain Physics". Global switches and measurements live in the [physics debug toolbox](../debugging/physics-debug.md).
- A model with no PMX physics data has no chains. This is not an error.

## Key files
| File | Role |
|---|---|
| `src/Animation/AnimationPoseEvaluator.*` | The pose order |
| `src/Animation/MotionSampler.*`, `IkSolver.*`, `AppendBoneSolver.*` | Sample, IK, append |
| `src/Animation/SkeletonPose.*`, `VertexSkinning.*` | FK and the CPU skinning reference |
| `src/Animation/BoneChainResolver.h`, `BonePoseMath.h` | Shared bone-walk and local matrix |
| `src/Game/Animation/AnimationSystem.*` | Play, evaluate, skin, upload, caches |
| `src/Game/Animation/GpuSkinningRigCache.*` | GPU skinning buffers per model |
| `src/Physics/*` | Pure solver, detection, colliders, wind |
| `src/Physics/TimeScaling.h` | Step fraction scaling and PMX damping mapping |
| `src/Physics/CollisionFilter.h`, `JointCollisionFit.*` | Group/mask test, bind-pose collision radius fit |
| `src/Physics/JointBendLimit.*` | PMX joint to bend limit per chain joint |
| `src/Physics/StepInterpolation.*` | Display position between two fixed steps |
| `src/Physics/PhysicsDiagnostics.*`, `PhysicsDebugReport.h` | Pure metrics and the plain report structs of the debug toolbox |
| `src/Game/Physics/PhysicsSystem.*` | Orchestration and rig cache |
| `src/Game/Physics/PhysicsDebugRecorder.*`, `PhysicsDebugSettings.*` | Live frame recorder, runtime switch table |
| `src/Application/PhysicsDebugCommandBridge.h`, `PhysicsDebugCommandDispatch.*` | Bridge and main-thread executor of `/physics_debug/*` |
| `src/Editor/PhysicsDebugImage.*` | CPU picture of the chains (no ImGui, no Vulkan) |
| `src/ECS/Components/` | `SkeletalAnimator`, `ResolvedAnimationPose`, `DynamicChainRig` |

## Rules
1. Never hand-roll a bone-parent walk. Use `ResolveBoneChain` (memoized, whole skeleton) or `ResolveSingleBoneChain` (fresh each call, needed while IK or physics changes the pose).
2. The bind-relative local matrix lives only in `ComputeBoneLocalMatrix`.
3. A new pose stage goes into `AnimationPoseEvaluator`, nowhere else.
4. CPU skinning (`SkinVertexRange`) is the reference. If the GPU kernel disagrees, fix the kernel.
5. Physics runs after append and before final skinning. It edits the pose in place and never replaces it.
6. `PhysicsSystem` does not depend on `AnimationSystem` or the renderer. `DynamicChainRig` has its own `meshGtaPath`.
7. Keep the per-entity skinning loop sequential. Two entities share one model's output buffers.
8. Chain state is per entity. Never share it.
9. Mask the PMX collision group before you shift: `group & 0x0F`. The byte comes raw from the file.

## Gotchas
- Two entities from the same model `.gta` share GPU mesh buffers. The last pose written wins.
- CPU skinning transforms normals with the bone matrix directly. This is correct for rigid bone matrices only.
- `SkeletalAnimator` is not saved in a scene. After a load, play the clip again.
- Playing a clip is a UI action: drop an Animation asset from the Project panel on a rigged model row in the Hierarchy. `/hierarchy_action` only reparents and toggles.
- `Play` returns false and changes nothing for a dead entity, a model without skin data, or a bad clip.

## Known gaps
- Morphs are imported and stored. They are never blended.
- MMD bezier interpolation is not evaluated.
- Camera, light and shadow keyframes are parsed and not played.
- Collision is a point plus a radius per joint. Cloth panels between joints do not collide.
- Only the direction parent to child becomes a bone rotation. Twist is lost, and a leaf joint gets no rotation.
- A hub with several joint children keeps the rotation of the last child only.
- `DynamicAndBoneMerge` bodies get a translation written. PMX says they keep the bone position.
- PMX spring factors are not read.
- Chains made of fully locked PMX joints (limit 0) follow the body exactly, so hair and long strands look rigid. Knob: `bend_limit_min_degrees`.
- HTTP routes play a clip and edit physics (`/physics_debug/*`). There is still no route that sets the skinning mode.

## Related
- [Physics debug toolbox](../debugging/physics-debug.md) · [Asset pipeline](asset-pipeline.md) · [Scene serialization](scene-serialization.md) · [ECS](ecs.md)
- [Job system](../conventions/job-system.md) · [Render graph](render-graph.md) · [Profiling](../debugging/profiling.md)
