# Skeletal Animation Pose Resolution

_Part of [GreatTamanaEngine](../../AGENTS.md)'s contributor conventions. See
[docs/README.md](../README.md) for the full documentation index._

Every per-frame MMD skeletal-animation pose evaluation lives under
`src/Animation/` (`BoneLocalOffset.h`, `MotionSampler.h/.cpp`,
`IkSolver.h/.cpp`, `AppendBoneSolver.h/.cpp`, `SkeletonPose.h/.cpp`,
`VertexSkinning.h/.cpp`, `AnimationPoseEvaluator.h/.cpp` - see `README.md`,
"Status", for the full history of how this runtime was built up). Follow
these rules whenever touching bone-hierarchy-walking code in this module:

- **Never hand-roll a new cycle-guarded bone-ancestor-chain walk - use
  `Animation/BoneChainResolver.h`'s `ResolveBoneChain()`/
  `ResolveSingleBoneChain()` instead.** `SkeletonPose.cpp`'s whole-skeleton
  world-matrix pass, `AppendBoneSolver.cpp`'s append/grant-source
  resolution, and `IkSolver.cpp`'s per-CCD-iteration single-bone world-
  matrix query all build on these two generic, Tier-1-tested primitives
  (`tests/Animation/BoneChainResolverTests.cpp`) rather than each
  maintaining its own copy of the same cycle-guard/memoization logic - three
  independent, subtly different hand-rolled versions of this exact pattern
  is what this code used to be, before it was pulled out into one place.
  Pick `ResolveBoneChain()` (memoized, one pass over the WHOLE skeleton)
  when every bone genuinely needs resolving and the underlying pose data
  won't change mid-walk; pick `ResolveSingleBoneChain()` (recomputed fresh,
  no caching across calls) only when it might - e.g. a CCD solve mutating
  the very pose being queried between successive single-bone lookups (see
  `IkSolver.cpp`'s own comment on why it can't use the memoized flavor).
- **The bind-relative local-transform formula lives in exactly one place:
  `Animation/BonePoseMath.h`'s `ComputeBoneLocalMatrix()`.** Both
  `SkeletonPose.cpp` and `IkSolver.cpp` compose a bone's world matrix as
  `parentWorld * ComputeBoneLocalMatrix(...)` - never reintroduce a second
  copy of `bone.position - parentBindPosition` plus `Mat4::TRS(...)`
  anywhere else; add a parameter to this one function instead if a future
  caller needs a variation on it.
- **The animation pipeline's per-frame execution ORDER (sample -> IK ->
  append -> forward-kinematics) has exactly one home:
  `Animation/AnimationPoseEvaluator.h`'s `EvaluateAnimatedSkinningPose()`.**
  This order is correctness-critical (an append source that's also an IK
  link must already carry its IK-solved rotation - see
  `Animation/AppendBoneSolver.h`'s own file comment) and used to be
  reproduced by hand inside `Game::UpdateSkeletalAnimators()`
  (`src/Game/Game.cpp`) - the only call site at the time. Any new call site
  that needs a fully-resolved animated pose (e.g. the Bone Viewer's planned
  live-pose overlay - see `TODO.md`) must call this one function rather than
  re-inlining the same four-call sequence; if the pipeline itself ever needs
  a new stage (e.g. future morph blending), add it here, once, not at every
  caller. Covered by a genuine ordering-regression test
  (`tests/Animation/AnimationPoseEvaluatorTests.cpp`'s
  `AppendedBoneInheritsIkSolvedRotationNotRawBindPose`) that fails if a
  future edit ever swaps IK solving and append inheritance.
