# Requirements: Getting Hair/Skirt Physics-Driven Animation Actually Working

Compiled from a direct source-code investigation of the `verlet-integration-1`
campaign (`src/Physics/`, `src/Game/Physics/PhysicsSystem.*`,
`src/Game/Animation/AnimationSystem.*`) triggered by a real-world report:
*"I put the Furina model in, and it doesn't physically simulate."* This is a
requirements/checklist document only — no code is included here.

## 1. Model/Asset-level requirements (the `.pmx` source file itself)

1. **The model must contain at least one bone flagged `DeformAfterPhysics`**
   in its PMX bone data (MMD-authoring term: a "physics-driven / jiggle"
   bone). This is the ONLY signal `DetectDynamicChains()`
   (`src/Physics/DynamicChainDetection.cpp`) uses to decide a bone is
   physics-simulated. No bone-name pattern matching, no material/mesh
   heuristic — purely this one PMX flag.
2. **A run of consecutive `DeformAfterPhysics` bones must be at least
   `minimumChainLength` (default 2) bones long.** A single isolated
   physics-flagged bone with no `DeformAfterPhysics` child is discarded —
   nothing to simulate as a "chain."
3. **Every physics-flagged bone must have a real ancestor to anchor to.** A
   physics-flagged bone with no parent at all (`parentBoneIndex < 0`, i.e. it
   IS the skeleton's literal root) causes that entire run to be discarded
   outright — this is a deliberate safety rule, not a bug, to avoid anchoring
   hair to the world origin instead of the character.
4. **(Optional, improves quality) The model's PMX rigid bodies should mark
   the corresponding bones' `RigidBody::motionType` as `Dynamic` (not
   `Static`), with reasonable `mass`/`linearDamping` values.** When present,
   these values seed each joint's simulated mass/damping automatically. When
   absent, every joint silently falls back to hand-tuned generic defaults
   (`damping = 0.08`, `mass = 1.0`) — the model will still simulate, just with
   generic-feeling weight/damping rather than whatever the original PMX
   author intended.
5. **`stiffness` (how much a chain tries to keep its animated shape) has NO
   PMX equivalent at all** — it always starts from one hardcoded engine
   default (`0.35`) regardless of what's in the model file, and can currently
   only be changed live, per joint, through the Editor Inspector (no way to
   author it in the source model).

## 2. Import-pipeline requirements

1. The model must be imported through this engine's own `.pmx → .gta`
   importer (`src/Assets/AssetImporter.cpp` → `PmxLoader.cpp` → `RigFile.h`'s
   `RigFileData`), not loaded as a raw `.pmx` directly — there is no runtime
   `.pmx` loading path in this engine at all, only at asset-import time.
2. The resulting `.gta` file's embedded rig metadata must include `physics`
   data (`RigFileData::physics`, populated directly from the same PMX rigid
   body/joint parse the engine has always done) — confirmed present in every
   currently supported `RigFileData` version; no separate/older `.gta` should
   need re-importing for this specific reason.

## 3. Runtime/ECS wiring requirements (spawning the model)

1. The model must be spawned through `Game::CreateMeshEntityFromGtaFile()` —
   this is the one and only call site that wires a spawned model into BOTH
   `AnimationSystem::RegisterSkinnedMesh()` AND
   `PhysicsSystem::RegisterDynamicChains()` /
   `PhysicsSystem::AttachDynamicChainRigIfNeeded()`. Every current in-engine
   spawn path (Hierarchy panel drag-drop, Scene panel drag-drop, Project
   panel drag-drop) already goes through this one function — confirmed
   directly.
2. This registration/attachment step happens **automatically and
   unconditionally** for any skinned model — no Editor action, no opt-in
   checkbox, no CMake build flag gates it. If the model has at least one
   valid detected chain, its root entity automatically receives a
   `DynamicChainRig` ECS component the moment it's spawned.

## 4. ⚠️ THE REQUIREMENT MOST LIKELY TO BE MISSED: an animation clip must
   actually be playing

This is the requirement that is easiest to overlook and is the most likely
explanation for "I put the model in and nothing jiggles":

1. **`PhysicsSystem::Update()` only ever touches an entity that already has a
   `ResolvedAnimationPose` ECS component.** If that component doesn't exist
   on the entity this frame, the entity is silently skipped — every frame,
   forever.
2. **`ResolvedAnimationPose` is created in exactly one place in the whole
   codebase: `AnimationSystem::EvaluatePoses()`.**
3. **`EvaluatePoses()` only visits entities that already carry a
   `SkeletalAnimator` ECS component, AND only actually writes a pose for one
   when `SkeletalAnimator::playing == true` and `animationGtaPath` is
   non-empty.**
4. **`SkeletalAnimator` is never added automatically when a model is
   spawned.** It is added ONLY by `Game::PlayAnimationOnEntity()` (→
   `AnimationSystem::Play()`), which must be called explicitly, separately,
   after spawning, with a real imported `.vmd`-derived animation `.gta` path.

**Net requirement: a real motion clip (`.vmd`, imported the same way as the
model) must be actively assigned and playing on the entity before any
physics simulation will ever run on it — a model just sitting in bind/T-pose
with no animation assigned will NEVER show any hair/skirt movement, no
matter how correctly its chains were detected or how its parameters are
tuned.** This is a genuine, confirmed architectural gap (not a data problem
with any specific model) — there is currently no "simulate physics on an
idle/T-pose character" path anywhere in the engine.

## 5. Parameter/tuning requirements (optional, for a good-looking result)

1. Open the Editor Inspector on the spawned entity; a "Dynamic Chain
   Physics" section appears automatically for any entity carrying a
   `DynamicChainRig`.
2. Confirm the section's "N chain(s), N joint(s) total" summary shows a
   non-zero chain count — if it reads "0 chains," go back to Section 1
   (nothing was detected in the model data at all) rather than tuning
   parameters that don't exist yet.
3. Per-joint `damping` / `stiffness` / `mass` sliders are live-editable here
   and shared across every instance of that same model — tune until the
   motion looks right. There is currently no per-instance override (every
   entity spawned from the same `.gta` shares one tuning set).
4. `GlobalPhysicsSettings` (gravity vector, wind) is shown read-only in the
   same panel — it is shared by every model in the scene; there is currently
   no live-editing surface for it, only a source-level default
   (`gravity = (0, -9.8, 0)`, a mild default wind).
5. (Optional) A per-chain "Head Collider" checkbox + bone index + radius is
   available in the same Inspector section, but is **disabled by default**
   even when auto-detected — must be manually enabled if you want hair to
   avoid clipping through the head.

## 6. Verification checklist (in order, cheapest check first)

1. Does the model have `DeformAfterPhysics` bones at all? (Requirement 1) —
   if unknown, this requires either inspecting the source `.pmx` in an
   external MMD tool, or checking the Inspector's chain/joint count (item
   5.2 above) after spawning.
2. Is the Inspector's "Dynamic Chain Physics" section showing at least 1
   chain? If 0 → go back to Requirement 1 (model data), not further tuning.
3. **Is an animation clip actually playing on this entity right now?** (the
   single most likely missed requirement, Section 4) — if the model is just
   sitting still with no VMD assigned, this is almost certainly the actual
   cause of "no simulation," independent of everything else being correct.
4. If a chain exists AND an animation is playing AND it still doesn't
   visibly move: check `stiffness` isn't left near `1.0` (fully locks the
   chain to the animated/bind shape, visually indistinguishable from "no
   physics") and that `gravityScale`/`windScale`/global gravity aren't all
   zeroed out.

## 7. Known, explicitly out-of-scope items (do not expect these to work)

- No physics while idle/T-posed (Section 4) — an explicitly identified,
  currently-unimplemented gap, not a hidden requirement you're missing.
- No per-instance parameter override — all instances of one model share one
  tuning set.
- No cloth/mesh (2D) simulation — only single linear bone-chain runs.
- No chain-vs-chain or chain-vs-world collision — only one optional sphere
  per chain (typically against the head).
- No live-editing of global gravity/wind through the Editor yet (read-only
  display only).
- This entire feature stack has never been verified against a real,
  live-Vulkan-rendered MMD model — only via ~50 automated unit/integration
  tests using synthetic data. A real visual pass (with all of the above
  requirements satisfied) has not yet been performed by anyone before this
  investigation.
