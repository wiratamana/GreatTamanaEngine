# PHASE2_COMPLETION_REPORT — RenderGraph Volume Auto-Registration (network-impl-6)

Parent: `PHASE0_MASTER_STRATEGY.md`. Task file:
`PHASE2_RENDERGRAPH_VOLUME_AUTO_REGISTRATION.md`. Previous phase:
`PHASE1_DEBUG_VOLUME_TEXTURE_REGISTRY.md` (`PHASE1_COMPLETION_REPORT.md`
read in full before starting this one — nothing in it superseded this
phase's own stale assumptions; the strategy document's Step 3 plan matched
the real code exactly).

## Summary

Wired `gte::rg::RenderGraph::ExecuteCompiledGraph()` to automatically
`Upsert()` every resolved volume texture into Phase 1's
`RenderGraphDebugVolumeTextureRegistry`, with zero opt-in required from
whichever pass declared it — exactly mirroring the pre-existing 2D-texture
auto-registration loop. Two new public accessors
(`DebugVolumeTextureSnapshotFor()`/`ListDebugVolumeTextures()`) now exist on
`RenderGraph`, but nothing outside `RenderGraph` calls them yet (that is
Phase 4's job, per the strategy document). No new query parameter, no new
endpoint, no new bridge type — this phase is purely internal `Renderer`-tier
wiring.

## What was done

Followed Step 3 of `PHASE2_RENDERGRAPH_VOLUME_AUTO_REGISTRATION.md` exactly,
with no deviations of substance:

1. **`src/Renderer/RenderGraph/RenderGraph.h`**
   - Added `#include "RenderGraphDebugVolumeTextureRegistry.h"` right next to
     the existing `#include "RenderGraphDebugTextureRegistry.h"`.
   - Added a new private member, `RenderGraphDebugVolumeTextureRegistry
     m_debugVolumeTextures;`, immediately after the existing
     `RenderGraphDebugTextureRegistry m_debugTextures;` member, with a doc
     comment cross-referencing why it shares `m_debugTextureFrameCounter`
     rather than needing its own counter.
   - Added two new public methods, `DebugVolumeTextureSnapshotFor(const
     std::string& name) const` and `ListDebugVolumeTextures() const`,
     placed directly after `ListDebugTextures()` (mirroring
     `DebugTextureSnapshotFor()`/`ListDebugTextures()`'s own doc-comment
     style, substituting "volume texture" for "texture" throughout).
   - Did **not** add a separate `CurrentDebugVolumeTextureFrameCounter()`,
     exactly as instructed — instead extended
     `CurrentDebugTextureFrameCounter()`'s own doc comment to explicitly
     state it now also stamps every `DebugVolumeTextureSnapshot`.
2. **`src/Renderer/RenderGraph/RenderGraph.cpp`**
   - Added a second registration loop immediately after the existing
     2D-texture Upsert loop inside `ExecuteCompiledGraph()`, iterating
     `physicalVolumeTextures`/`input.volumeTextureNames` in lockstep (both
     sized from `input.volumeTextureDescs.size()`, confirmed by reading
     `RenderGraphBuilder::ImportVolumeTexture()` — it always
     `push_back()`s onto `m_volumeTextureDescs` and `m_volumeTextureNames`
     together, in the same two lines, so the size invariant holds by
     construction; no extra bounds guard was needed beyond what the
     strategy document already anticipated). Skips an unresolved index or a
     null/empty name defensively, identical in shape to the 2D loop.
   - Implemented the two new accessors as one-line forwarders onto
     `m_debugVolumeTextures.FindByName()`/`ListAll()`, placed directly after
     `RenderGraph::ListDebugTextures()`'s own definition.
3. **Step 3.3 (do not touch `ApplyColorStateOverride()`/
   `NotifyDebugTextureStateOverride()` call sites)** — confirmed and left
   untouched. No new volume-side override call site was added.
4. **Step 3.4 (sanity-check `KeepVolumeTextureOutput()` is already
   sufficient)** — verified by reading `Application.cpp`: both
   `b.KeepVolumeTextureOutput(gameAtmosphere.aerialPerspectiveVolumeHandle);`
   (line ~422) and
   `b.KeepVolumeTextureOutput(sceneAtmosphere.aerialPerspectiveVolumeHandle);`
   (line ~498) already exist today, for both the Game View and Scene View
   Atmosphere passes respectively. This confirms both volumes are already
   marked as required roots, so `vol.resolved == true` for both every frame
   the Atmosphere feature runs (always — no switch, see `AGENTS.md`,
   "Atmosphere Scattering"). No code change was needed for this sub-step, as
   the strategy document anticipated — verified directly by the runtime
   smoke test below rather than by static reading alone.

## Deviations from the strategy document

None of substance. The plan's own illustrative code snippets (Step 3.2)
matched the real surrounding code exactly (same variable names —
`physicalVolumeTextures`, `input.volumeTextureNames`, `timingMode`,
`m_debugTextureFrameCounter` — all live and spelled exactly as the strategy
document assumed), so no design judgment calls were required. The only
minor, purely cosmetic adjustment made beyond the letter of the plan was
tightening up a stray double-blank-line left by the `edit_line` insertion
in both `RenderGraph.h` and `RenderGraph.cpp` after splicing in the new
blocks — not a functional change.

## Verification

- **Fast compile check** (per this phase's own Verification section):
  - `cmake --build build --target gte_core` — succeeded, zero warnings from
    the changed files.
  - `cmake --build build --target GreatTamanaEngineTests` — succeeded.
  - Also built the full `GreatTamanaEngine` app target (`cmake --build build
    --target GreatTamanaEngine`) — not strictly required by this phase's own
    instructions (that requirement is Phase 4/5/6-specific), but necessary
    anyway to perform this phase's own mandated runtime smoke check below.
    Succeeded with no warnings.
- **Runtime smoke check** (per this phase's own Verification section — no
  new automated test was expected/required, since `ExecuteCompiledGraph()`
  is Tier 2/GPU-touching code):
  - Launched `GreatTamanaEngine.exe` via `run_app_background`, let it render
    for a few seconds (the Atmosphere feature is always active, so both
    Game View and Scene View aerial-perspective volumes resolve every
    frame).
  - `GET /list_textures` via `gte_send_request` returned its usual 2D-only
    texture list (`AtmosphereTransmittanceLut`, `AtmosphereMultiScatteringLut`,
    `AtmosphereSkyViewLut_GameView`/`_SceneView`,
    `AtmosphereAerialPerspectiveVolumeDebugSlice`, `GameView`/
    `GameViewComposited`, `SceneView`/`SceneViewComposited`, `Swapchain`) —
    **unchanged** from before this phase, confirming this phase adds no
    visible behavior yet (`ListDebugVolumeTextures()` has no caller yet, and
    the existing `ListDebugTextures()`/`NamedTextureListEntry` path was
    never touched).
  - As an extra, not-strictly-required sanity check, also called `GET
    /get_texture?texture_name=AtmosphereTransmittanceLut&format=json` and
    confirmed it still returns a normal, correctly-rendered 200 PNG
    (visually inspected via `load_image` — a plausible transmittance-LUT
    gradient), confirming zero regression to the existing, shipped
    2D-texture capture path.
  - Stopped the background process (`stop_app_background`) once verification
    completed.
- Per this phase's own workflow rule (Phase 0's "No full build/full
  regression test until Phase 6"), no `ctest` run was performed this phase.

## Result

Phase 2 is complete: `RenderGraph` now automatically keeps
`RenderGraphDebugVolumeTextureRegistry` up to date every frame, for both
Atmosphere aerial-perspective volumes, in both `ExecuteTimingMode` regimes,
with zero opt-in from the passes that declared them. `DebugVolumeTextureSnapshotFor()`/
`ListDebugVolumeTextures()` are real, callable, and return live data — but
still have no caller anywhere else in the engine. Existing 2D-texture
behavior (`GET /get_texture`/`GET /list_textures`) is confirmed byte-for-byte
unchanged. Next: Phase 3
(`PHASE3_VOLUME_RAYMARCH_PREVIEW_RENDERER.md`), the actual raymarch
thumbnail renderer.
