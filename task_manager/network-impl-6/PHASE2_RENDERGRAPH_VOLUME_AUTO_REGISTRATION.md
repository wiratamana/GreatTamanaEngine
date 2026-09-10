# PHASE2_RENDERGRAPH_VOLUME_AUTO_REGISTRATION

Parent: `PHASE0_MASTER_STRATEGY.md` — **READ THAT FILE FIRST.**
Previous: `PHASE1_DEBUG_VOLUME_TEXTURE_REGISTRY.md` — read its own
`PHASE1_COMPLETION_REPORT.md` before starting.

## Step 1 — The Goal

Make `gte::rg::RenderGraph` automatically upsert EVERY volume texture it
resolves this frame into Phase 1's new
`RenderGraphDebugVolumeTextureRegistry`, with **zero opt-in** required from
whichever pass declared it — exactly mirroring how every 2D texture a pass
declares via `RenderGraphBuilder::CreateTexture()`/`ImportTexture()`
already becomes automatically capturable via
`RenderGraphDebugTextureRegistry` today. By the end of this phase,
`RenderGraph::DebugVolumeTextureSnapshotFor("AtmosphereAerialPerspectiveVolume_GameView")`
returns a real, populated snapshot after a single frame has rendered — but
nothing outside `RenderGraph` calls this new accessor yet (that's Phase 4).

## Step 2 — The Situation / The Problem

Open `src/Renderer/RenderGraph/RenderGraph.cpp` and find
`ExecuteCompiledGraph()`. Near its end (after the main per-pass recording
loop, before the `RenderGraphSnapshot` build at the very end of the
function), there is already a loop that does exactly the job this phase
needs, just for 2D textures:

```cpp
for (std::size_t i = 0; i < physicalTextures.size(); ++i) {
    const PhysicalTexture& tex = physicalTextures[i];
    if (!tex.resolved) {
        continue;
    }
    const char* name = input.textureNames[i];
    if (name == nullptr || name[0] == '\0') {
        continue;
    }

    DebugTextureSnapshot snapshot;
    snapshot.name = name;
    snapshot.regime = timingMode;
    snapshot.target = tex.target;
    snapshot.hasDepth = tex.hasDepth;
    snapshot.colorState = tex.colorState;
    snapshot.depthState = tex.depthState;
    snapshot.lastUpdatedFrameCounter = m_debugTextureFrameCounter;
    m_debugTextures.Upsert(snapshot);
}
```

Earlier in the same function, `physicalVolumeTextures` (a
`std::vector<PhysicalVolumeTexture>`, sized from
`input.volumeTextureDescs.size()`) is already built and resolved by the
existing `EnsureVolumeTextureResolved()` machinery (Atmosphere Scattering
campaign, Phase 2) — it is a fully parallel array to `physicalTextures`,
just for volumes, and `input.volumeTextureNames` (a
`std::vector<const char*>`, populated by
`RenderGraphBuilder::ImportVolumeTexture()`, see
`RenderGraphBuilder.cpp`'s `m_volumeTextureNames.push_back(name)`) is
already the exact volume-texture counterpart of `input.textureNames`. **All
of the plumbing this phase needs already exists** — this phase is almost
entirely "add one more loop, in the same shape as the one above, right
next to it".

`m_debugTextureFrameCounter` (the freshness clock `lastUpdatedFrameCounter`
is compared against, surfaced publicly as
`RenderGraph::CurrentDebugTextureFrameCounter()`) is a single, SHARED
counter today — used by both regimes (synchronous offscreen AND pipelined
present), per that method's own existing doc comment ("both regimes share
this same counter"). This campaign reuses that exact same counter for
volumes too — there is no reason to invent a second, parallel counter; a
volume's "how many frames old" freshness should be measured on the exact
same clock a 2D texture's is, so the two are directly comparable in a
future `GET /list_textures` response (Phase 5).

## Step 3 — The Plan

### 3.1 — `RenderGraph.h` changes

1. `#include "RenderGraphDebugVolumeTextureRegistry.h"` alongside the
   existing `#include "RenderGraphDebugTextureRegistry.h"`.
2. Add a new private member, right next to the existing
   `RenderGraphDebugTextureRegistry m_debugTextures;` member:
   `RenderGraphDebugVolumeTextureRegistry m_debugVolumeTextures;`.
3. Add two new PUBLIC methods, placed directly next to
   `DebugTextureSnapshotFor()`/`ListDebugTextures()` (around line 288-292),
   with doc comments mirroring those two exactly, substituting "volume
   texture" for "texture":

   ```cpp
   std::optional<DebugVolumeTextureSnapshot> DebugVolumeTextureSnapshotFor(const std::string& name) const;
   std::vector<DebugVolumeTextureSnapshot> ListDebugVolumeTextures() const;
   ```

   Do **not** add a separate `CurrentDebugVolumeTextureFrameCounter()` —
   the existing `CurrentDebugTextureFrameCounter()` already serves both
   kinds (see Step 2 above); a caller comparing a volume snapshot's
   `lastUpdatedFrameCounter` against freshness must call the SAME existing
   method. Update that existing method's own doc comment to explicitly
   say so, so a future reader doesn't have to guess.

### 3.2 — `RenderGraph.cpp` changes

Immediately after the existing 2D-texture Upsert loop shown in Step 2
above (same location, same function, same scope — both loops iterate
different parallel-array pairs but share every other piece of context:
`timingMode`, `m_debugTextureFrameCounter`), add:

```cpp
for (std::size_t i = 0; i < physicalVolumeTextures.size(); ++i) {
    const PhysicalVolumeTexture& vol = physicalVolumeTextures[i];
    if (!vol.resolved) {
        continue;
    }
    const char* name = input.volumeTextureNames[i];
    if (name == nullptr || name[0] == '\0') {
        continue;
    }

    DebugVolumeTextureSnapshot snapshot;
    snapshot.name = name;
    snapshot.regime = timingMode;
    snapshot.target = vol.target;
    snapshot.state = vol.state;
    snapshot.lastUpdatedFrameCounter = m_debugTextureFrameCounter;
    m_debugVolumeTextures.Upsert(snapshot);
}
```

Then implement the two new public accessors (near wherever
`DebugTextureSnapshotFor()`/`ListDebugTextures()` are implemented in this
same `.cpp`), each a one-line forward to the new registry member:

```cpp
std::optional<DebugVolumeTextureSnapshot> RenderGraph::DebugVolumeTextureSnapshotFor(const std::string& name) const
{
    return m_debugVolumeTextures.FindByName(name);
}

std::vector<DebugVolumeTextureSnapshot> RenderGraph::ListDebugVolumeTextures() const
{
    return m_debugVolumeTextures.ListAll();
}
```

**Double-check `input.volumeTextureNames.size() == physicalVolumeTextures.size()`
holds** (it must, by construction — both are sized from
`input.volumeTextureDescs.size()`/the same builder-side vector lengths,
mirroring the 2D pair's existing invariant) before indexing both by `i` in
lockstep; if you find any code path where this could legitimately differ
(it shouldn't), add an explicit bounds guard rather than assuming.

### 3.3 — Do NOT touch `ApplyColorStateOverride()`'s call sites yet

`Application.cpp`'s four existing
`RenderGraph::NotifyDebugTextureStateOverride()` call sites (see
`AGENTS.md`, "Named Texture Capture") are all for 2D textures presented/
sampled externally — none of them concern a volume texture today (the
Atmosphere aerial-perspective volume is never externally
barrier-transitioned outside the graph's own compiled barrier plan). Do
not invent a new volume-side override call site speculatively in this
phase; Phase 1's `ApplyStateOverride()` method exists so the OPTION is
available later, not because this phase needs to call it.

### 3.4 — Sanity-check `KeepVolumeTextureOutput()` is already sufficient

Confirm (by reading `AtmospherePassSequence.cpp`/`Application.cpp`, around
the `KeepVolumeTextureOutput(gameAtmosphere.aerialPerspectiveVolumeHandle)`/
`KeepVolumeTextureOutput(sceneAtmosphere.aerialPerspectiveVolumeHandle)`
call sites already present today) that BOTH Atmosphere volumes are already
marked as required roots, so `RenderGraphCompiler`/`EnsureVolumeTextureResolved()`
never culls them and `vol.resolved` is `true` for both every frame once the
Atmosphere feature is active. This should require no code change at all —
this sub-step is a verification-only sanity check confirming Phase 2's new
loop will actually have real data to Upsert once this phase lands, not a
place to write new code. (If it turns out NOT already sufficient for any
reason, that is a real finding — note it plainly in this phase's own
completion report and fix it as part of this phase, since Phase 2's loop is
otherwise dead code with nothing to register.)

### Verification

- Fast compile check: build `gte_core` only first (catches any
  header/include-order mistake cheaply), then `GreatTamanaEngineTests`.
- No new automated test is expected/required for this phase's own change
  (`RenderGraph::ExecuteCompiledGraph()` requires a live `VkCommandBuffer`
  and is Tier 2, no automated coverage — see `TESTING.md`) — instead, do a
  real runtime smoke check: launch the engine
  (`run_app_background`), let it render at least one frame with the
  Atmosphere feature active (it always is — no switch, see AGENTS.md,
  "Atmosphere Scattering"), then use `gte_send_request` against
  `GET /list_textures` and confirm the response is UNCHANGED from before
  this phase (this phase adds no visible behavior yet — `ListDebugTextures()`
  is untouched; the new `ListDebugVolumeTextures()` accessor has no caller
  yet). This is a "did I break anything" check, not a "does the new
  feature work" check (there is nothing user-visible to check yet).
- Write `PHASE2_COMPLETION_REPORT.md`, commit.
