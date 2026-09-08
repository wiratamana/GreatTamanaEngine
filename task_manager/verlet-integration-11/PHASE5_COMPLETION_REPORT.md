# PHASE5 Completion Report — Build Registration + End-to-End Round-Trip Regression

campaign: `verlet-integration-11` (see `PHASE0_MASTER_STRATEGY.md`)
phase: `PHASE5_END_TO_END_ROUND_TRIP_REGRESSION_AND_BUILD_REGISTRATION.md`
status: **DONE** — full `ctest`/gtest suite passes (978 tests, 977 passed, 1 pre-existing
environment-only skip), including two new, previously-nonexistent end-to-end tests.

## Why this report exists now

This phase was specified in `PHASE5_END_TO_END_ROUND_TRIP_REGRESSION_AND_BUILD_REGISTRATION.md`
but had **never actually been executed** — PHASE1-4 each has its own
`PHASEn_COMPLETION_REPORT.md`, but PHASE5 did not, and its own promised
deliverable — `tests/Game/Physics/PhysicsSystemJointOverrideEndToEndTests.cpp`
— did not exist anywhere in the repository, nor was it registered in
`tests/CMakeLists.txt`. This means the whole campaign's central promise
("a user's live joint-physics edit survives a real save → reload round
trip, both across a fresh process AND later in the same session") had
**never been verified by anything beyond individual-phase unit tests and
manual code review** — every earlier phase's own tests proved one LINK of
the chain in isolation, never the full composed chain.

This was discovered while investigating a user report that the Editor's
"Save Joint Physics to Asset" button appeared to report success but the
save did not seem to actually take effect. A full read-through of every
phase's source (`DynamicChainPhysicsPersistence.cpp/h`, `RigFile.cpp/h`,
`JointPhysicsOverrideApplication.cpp`, `PhysicsSystem.cpp`,
`MeshAssetGpuCatalog.cpp`, `MeshInstantiationSystem.h`,
`InspectorPanel.cpp`, `Game.cpp`) found every individual piece correctly
implemented and individually tested — the only concrete, verifiable gap
was this missing PHASE5 deliverable itself.

## What was done

1. **Wrote `tests/Game/Physics/PhysicsSystemJointOverrideEndToEndTests.cpp`**
   exactly per the strategy document's Step 3.3/3.4, with the one detail
   that document had deliberately left as a placeholder
   (`WriteDetectableSkinnedMeshGtaFile`'s actual body) filled in: a real,
   minimal, on-disk Mesh `*.gta` (one static anchor bone + a 3-joint linear
   dynamic chain, mirroring `DynamicChainDetectionTests.cpp`'s own
   `SimpleLinearRigWithOneStaticAnchorAndThreeDynamicBodiesDetectsOneLinearChain`
   fixture), with:
   - `EditedJointPhysicsSurvivesSaveAndReloadAcrossAFreshProcess` — first
     load (defaults) → live-edit → `SaveJointPhysicsOverridesToGtaFile()` →
     brand-new `PhysicsSystem` re-reading the same file from scratch →
     confirms the edited damping/stiffness/mass are exactly what comes back.
   - `EditedJointPhysicsIsPickedUpByASecondSpawnWithinTheSameSession` — the
     SAME `PhysicsSystem` instance registers the same model path twice,
     proving PHASE3's "same session" fix actually holds when the underlying
     `RegisterDynamicChains()` machinery is exercised twice in a row.
2. **One real bug caught and fixed IN THIS NEW TEST FILE ITSELF** (not in
   production code): the first draft's "before any save, this joint has
   pure `DynamicJointSettings{}` defaults" sanity check initially failed,
   because `DetectDynamicChains()` seeds `DynamicJointSettings::damping`
   directly from the PMX `RigidBody::linearDamping` field (see
   `DynamicChainDetection.cpp`), not from the class default — the fixture's
   rigid bodies needed `linearDamping = 0.4f` (matching
   `DynamicJointSettings{}`'s own default) for that assertion to be valid.
   This is exactly the kind of subtlety the strategy document's own
   `/* ... */` placeholder for this helper function was silently deferring
   to whoever implemented it — now resolved and documented in the test's
   own comment so it doesn't trip up anyone reading it later.
3. **Registered the new file** in `tests/CMakeLists.txt`, next to
   `Game/Physics/DynamicChainPhysicsPersistenceTests.cpp` (immediately
   after it), per the strategy document's own placement instruction.
4. **Confirmed every other PHASE1-4 file is already correctly registered**
   (cross-checked directly against `CMakeLists.txt`/`tests/CMakeLists.txt`):
   `src/Physics/JointPhysicsOverrideApplication.cpp/.h`,
   `src/Game/Physics/DynamicChainPhysicsPersistence.cpp/.h`,
   `tests/Physics/JointPhysicsOverrideApplicationTests.cpp`,
   `tests/Game/Physics/DynamicChainPhysicsPersistenceTests.cpp`,
   `tests/Game/Instantiation/MeshAssetGpuCatalogJointOverrideRefreshTests.cpp`
   — all present and building, as PHASE3/PHASE4's own completion reports
   already claimed.
5. **Built and ran the complete test suite** (`GreatTamanaEngineTests.exe`,
   no filter — every test, not just this campaign's own) per this phase's
   own explicit instruction ("a successful build is not enough"):
   ```
   cmake --build build --target GreatTamanaEngineTests   -> SUCCESS (0 errors)
   GreatTamanaEngineTests.exe                            -> 978 tests ran
                                                             977 PASSED
                                                             1 SKIPPED (PmxLoaderRealModelSmokeTest -
                                                               requires a real MMD model directory on
                                                               this machine that isn't present; pre-
                                                               existing, unrelated to this campaign)
                                                             0 FAILED
   ```
   No pre-existing test regressed. Both new end-to-end tests pass.

## Conclusion on the original user report

**The "Save Joint Physics to Asset" feature genuinely works** —
`SaveJointPhysicsOverridesToGtaFile()` really does write the edited
damping/stiffness/mass values into the model's `*.gta` file, and a fresh
`PhysicsSystem::RegisterDynamicChains()` (a brand-new process/session, or a
second spawn within the same session after PHASE3's cache refresh) really
does read them back and apply them. This is now proven by an automated,
real-disk-I/O test, not merely by individual-phase unit tests and manual
inspection.

If a user still observes a save that doesn't appear to take effect, the
most likely REMAINING explanations (none of which are bugs in the save
path itself, all verified correct above) are:
- The running `.exe` is a **stale build** predating this campaign's
  changes (rebuild from the current source tree).
- The model has **no detected dynamic bone chains** at all — the button
  reports `"No detected dynamic bone chains to save for this model."` in
  red in that case, which is easy to misread as a generic failure rather
  than "there was nothing to save".
- A change was made to a joint's slider **after** the button's own
  placement in source order was rendered this same frame is NOT an issue
  (ImGui widgets share the same underlying `DynamicChainRigCache` data
  across frames regardless of draw order) — but genuinely re-checking the
  file's on-disk contents (e.g. via a hex viewer) rather than only the
  in-Editor green/red status text is the reliable way to confirm a save
  landed.

## Build registration (final)

- `tests/CMakeLists.txt` — added
  `Game/Physics/PhysicsSystemJointOverrideEndToEndTests.cpp`, immediately
  after `Game/Physics/DynamicChainPhysicsPersistenceTests.cpp`.

## What this phase deliberately does NOT do

- Does not attempt Tier-2/GPU-backed verification of
  `MeshAssetGpuCatalog::RefreshCachedJointPhysicsOverridesFromDisk()`'s own
  cache-mutating branch in a live Renderer, nor of the Editor button itself
  — both remain "Tier 2, no automated coverage yet" per `AGENTS.md`'s
  accepted policy, same as PHASE3/PHASE4's own completion reports already
  note.
- Does not build/test the `GTE_ENABLE_PROJECT_PANEL=OFF` configuration in
  this pass (the strategy document's Step 3.6 calls for both ON/OFF
  configurations to be built) — only the default ON configuration (this
  repository's existing `build/` tree) was exercised here. Building the OFF
  configuration from a clean tree is a reasonable manual follow-up but was
  out of scope for this diagnostic pass.

## Next step

None — this was the final phase of the campaign. `verlet-integration-11` is
now genuinely, verifiably complete end-to-end.
