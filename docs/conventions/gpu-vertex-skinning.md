# GPU Vertex Skinning

_Part of [GreatTamanaEngine](../../AGENTS.md)'s contributor conventions. See
[docs/README.md](../README.md) for the full documentation index._

An eight-phase campaign (`task_manager/gpu_skinning/`,
`GPU_SKINNING_PHASE0_MASTER_STRATEGY_v2.md`) gave the engine a SECOND,
GPU-resident implementation of vertex skinning — a compute-shader mirror of
`Animation/VertexSkinning.h`'s CPU path (`SkinVertexRange()`), switchable at
runtime, specifically so the performance difference between the two tech
stacks can be observed (see `AnimationSystem::SkinningMode`,
`src/Game/Animation/AnimationSystem.h`, and the Editor "Jobs" panel's own
"Skinning Mode" toggle — `Panels/JobsPanel.cpp`, Phase 7). Follow these
rules whenever touching this feature:

- **The CPU path (`Animation/VertexSkinning.cpp`'s `SkinVertexRange()`) is
  the permanent ORACLE and must never be modified to "agree" with the GPU
  kernel.** Every GPU-side buffer layout (`src/Renderer/GpuSkinning/
  GpuSkinningTypes.h`) and both `.comp` kernels
  (`src/Shaders/SkinVerticesPositionNormal(Uv).comp`) were written by
  reading the CPU code line-by-line and mirroring its exact
  accumulate-then-normalize-once, no-valid-influence-falls-back-to-bind-pose
  behavior — see `GPU_SKINNING_PHASE2_COMPLETION_REPORT.md` for the exact
  differences found between an early illustrative sketch and the real CPU
  source. If the two paths ever disagree, the CPU path is right by
  definition and the GPU kernel is the one that needs fixing — never the
  reverse.
- **The graphics pass reading a GPU-skinned model's vertex buffer declares a
  "phantom" `ResourceAccess::VertexBufferRead`** even though it never
  actually reads that buffer through the render graph's own resolution
  machinery (it reads it via a real `vkCmdBindVertexBuffers` binding,
  outside `PassContext::resolveBuffer` entirely) — this exists PURELY to
  force the render graph's compiler/barrier planner to order the draw pass
  strictly after whichever compute pass wrote it this frame. Do not "clean
  this up" as dead code — see `RenderGraphTypes.h`'s own `ResourceAccess`
  doc comment and `GPU_SKINNING_PHASE3_COMPLETION_REPORT.md` for the
  write-after-write hazard this closes.
- **`GpuSkinningRigCache`'s `ComputeDescriptorSet::Rewrite()` is called
  EXACTLY ONCE per model, at registration time, never every frame** — a
  deliberate exception to `ComputeDescriptorSet`'s general "safe, and
  expected, to call every frame" convention, justified because every one of
  a GPU-skinned model's four buffers (bind pose, skin weights, bone
  matrices, output) has a permanently stable identity for the model's whole
  lifetime. Do not "fix" this into a per-frame rewrite.
- **A GPU-skinned model intentionally owns TWO separate `Mesh` objects for
  its whole lifetime — one CPU-mode, one GPU-mode — never a single Mesh
  that gets mutated in place when the mode switches.** This doubles that
  model's GPU memory footprint for as long as it exists, which is a
  deliberate, accepted trade (see `GPU_SKINNING_PHASE5_RUNTIME_CPU_GPU_SWITCH_STRATEGY_v2.md`,
  Step 3.5) for keeping `MeshRenderer`/`RenderSystem` completely unaware
  skinning mode exists at all — do not attempt to "optimize this away" by
  sharing one Mesh between the two modes.
- **`AnimationSystem::Update()`'s outer per-`SkeletalAnimator` loop must
  remain STRICTLY SEQUENTIAL regardless of skinning mode** — this rule
  predates GPU skinning (see the [Job System](job-system.md) section above) but applies
  doubly here: two entities sharing one model's GPU output buffer, if their
  own `Dispatch()`/GPU-mode work were ever allowed to run concurrently
  instead of one-at-a-time, would be a genuine, unsynchronized GPU
  write-after-write race, not merely the CPU path's existing harmless
  "last write wins" visual limitation.
- **The Editor's CPU/GPU skinning-mode toggle lives in the "Jobs" panel,
  not "Render Graph"** (see `Panels/JobsPanel.cpp`'s own
  `BuildSkinningModeControl()`) — a deliberate placement decision (Phase 7,
  Step 3.1): a user watching this panel's own worker timeline is exactly
  who benefits from seeing, right next to it, the control that makes
  "SkinVertices" entries appear/disappear from it. Neither this panel nor
  "Render Graph" gained a new "N/A"/fabricated-value state for the mode
  that ISN'T currently active — the absence of a row/segment already IS
  the honest signal (see [Profiling](profiling.md) above) — the toggle's own tooltip
  (`JobsPanelData.h`'s `SkinningModeCrossReferenceHint()`) is what tells a
  user where to look instead, never a new profiling code path.
