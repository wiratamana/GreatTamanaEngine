# PHASE5_COMPLETION_REPORT — List Textures Volume Surfacing (network-impl-6)

Parent: `PHASE0_MASTER_STRATEGY.md`. Task file:
`PHASE5_LIST_TEXTURES_VOLUME_SURFACING.md`. Previous phase:
`PHASE4_NAMED_TEXTURE_ENDPOINT_VOLUME_BRANCH_WIRING.md`
(`PHASE4_COMPLETION_REPORT.md` read in full before starting this one —
nothing in it superseded this phase's own assumptions; the three-struct-hop
shape it describes, `RenderGraph::DebugVolumeTextureSnapshotFor()`'s real
signature, and the "not wired to `GET /list_textures` yet" starting state all
matched exactly what this phase's own Step 3 plan assumed).

## Summary

`GET /list_textures` now lists every registered VOLUME texture alongside
every existing 2D texture, in one flat JSON array. Every entry (2D or
volume) gained exactly two new fields, `kind` (`"texture2d"`/`"texture3d"`)
and `depth` (the volume's Z/texel-count dimension, always `0` for a 2D
entry) — every OTHER field on an existing 2D entry is untouched. Verified
end-to-end against a live running engine: the response now includes 10
pre-existing `"kind":"texture2d"` entries plus 2 new `"kind":"texture3d"`
entries (`AtmosphereAerialPerspectiveVolume_GameView`/`..._SceneView`, both
reporting the correct `"depth":32`).

## What was done

Followed Step 3 of `PHASE5_LIST_TEXTURES_VOLUME_SURFACING.md` exactly, in
order:

1. **`src/Application/FrameCaptureBridge.h`** (Step 3.1) — added `kind`
   (defaulting to `"texture2d"`) and `depth` (defaulting to `0`) as two new
   trailing fields on `PublishedTextureListEntry`, with doc comments matching
   the strategy document's own text verbatim.
2. **`src/Application/Application.cpp`** (Step 3.2) — inside the existing
   `GET /list_textures` data-publishing block (right after Phase 4's new
   volume-branch `else if`):
   - Every entry built from the pre-existing `rg::DebugTextureSnapshot` loop
     now explicitly sets `entry.kind = "texture2d";` (previously implicit via
     the struct's own default).
   - A second loop, immediately after the first, iterates
     `m_renderGraph.ListDebugVolumeTextures()` and appends one more
     `PublishedTextureListEntry` per volume — `regime`/`format` resolved via
     the exact same `ToDebugTextureRegimeString()`/`DebugTextureColorFormatName()`
     helpers the 2D loop already uses (confirmed reusable as-is: both take a
     plain `rg::ExecuteTimingMode`/`VkFormat` value, exactly what
     `DebugVolumeTextureSnapshot::regime`/`target.format` already are),
     `hasDepth` always `false`, `depth` from `target.extent.depth`, `kind`
     `"texture3d"`.
3. **`src/Network/NetworkRoutes.h`/`.cpp`** (Step 3.3) — mirrored the same two
   new fields on `TextureListEntryView`, and `BuildListTexturesResponseJson()`
   now always emits `"kind"`/`"depth"` for every entry. Updated the function's
   own doc-comment example JSON to show both a 2D and a 3D entry side by
   side, per the strategy document's own instruction.
4. **`src/Network/NetworkServer.cpp`** (Step 3.4) — confirmed (as the
   strategy document itself already flagged) that `RegisterListTexturesRoute()`
   builds each `TextureListEntryView` via a single POSITIONAL aggregate
   initializer, not field-by-field assignment — appended `entry.kind,
   entry.depth` as two more positional values, in declared field order,
   rather than introducing a new assignment-statement style at that call
   site.
5. **`GET /get_texture`'s own response shape** (Step 3.5) — confirmed
   untouched, exactly as instructed; `BuildTextureCaptureJsonBody()` was not
   modified at all.

### Test coverage added (Tier 1)

`tests/Network/NetworkRoutesTests.cpp`'s `BuildListTexturesResponseJsonTests`
suite (pure, Tier-1-testable JSON-building logic — per `AGENTS.md`'s
"Testability & Regression Safety", every change to Tier 1 code needs a
matching test change):

- `MultipleEntriesRoundTripEveryField` — extended to also assert both
  pre-existing entries (built with no `kind`/`depth` supplied at their own
  call sites) emit the correct implicit defaults, `"kind":"texture2d"` and
  `"depth":0`.
- `VolumeEntryReportsTexture3dKindAndDepth` (new) — a `TextureListEntryView`
  with `kind = "texture3d"`/`depth = 32` explicitly set round-trips correctly
  through `BuildListTexturesResponseJson()`, including `has_depth == false`
  for a volume entry.

Both pass (`tests/GreatTamanaEngineTests.exe
--gtest_filter=BuildListTexturesResponseJsonTests.*` — 4/4 passed).

## Deviations from the strategy document

**One incidental, self-inflicted mistake made and fully corrected during this
phase's own editing process — not a deviation from the plan's design.** The
first `edit_line` call that spliced the new volume-publishing loop into
`Application.cpp`'s existing block used a `length` that also removed the
pre-existing `m_captureBridge.PublishTextureList(std::move(published));`
call and the block's own closing brace (the replacement range ended exactly
at the line that call sat on). This was caught immediately by the very next
manual runtime smoke test — `GET /list_textures` kept returning
`{"textures":[]}` even though `GET /get_texture`/`GET /http_hello_world`
both worked correctly against the same live engine, which was the tell that
the publish call itself had gone missing (not a data problem) — confirmed by
re-reading the file and finding the call genuinely absent. Fixed with one
follow-up `edit_line` call re-inserting `m_captureBridge.PublishTextureList(std::move(published));`
and the closing brace right after the new volume loop, then re-built and
re-ran the exact same smoke test, which then produced the correct combined
2D+3D response documented below. Flagged here for transparency, not because
it represents a design deviation — the final, committed `Application.cpp`
matches the strategy document's Step 3.2 exactly (two loops, one
`PublishTextureList()` call covering both), and this mistake was caught and
fixed entirely within this phase's own verification step, before any commit.

No other deviations. Every other concrete detail in the strategy document
(the exact struct field additions/order, the positional-initializer
correction already called out in Step 3.4, the "don't touch
`BuildTextureCaptureJsonBody()`" instruction) matched the real source tree
exactly and needed no further correction.

## Verification

Per this phase's own "Verification" section:

1. **Fast compile check**:
   - `cmake --build build --target gte_core` — succeeded cleanly (after the
     one incidental mistake above was caught and fixed by this same
     verification step, then re-run clean).
   - `cmake --build build --target GreatTamanaEngineTests` — succeeded,
     including the two new/updated `BuildListTexturesResponseJsonTests`
     cases.
   - `cmake --build build --target GreatTamanaEngine` (the full app target,
     as this phase's own Verification section explicitly calls for) —
     succeeded, shader staging log unchanged (no new shaders this phase).
2. **Targeted Tier-1 test run**: `tests\GreatTamanaEngineTests.exe
   --gtest_filter=BuildListTexturesResponseJsonTests.*` — 4/4 passed
   (`EmptyListProducesLiteralEmptyShape`, `MultipleEntriesRoundTripEveryField`,
   `VolumeEntryReportsTexture3dKindAndDepth`, `NameWithQuoteRoundTripsThroughJson`).
3. **Real runtime smoke test** (this phase's actual acceptance criterion),
   against the freshly rebuilt `GreatTamanaEngine.exe`:
   1. Launched via `run_app_background` (PID 13752), waited a few seconds for
      the Atmosphere feature's volumes/GameView/SceneView to start
      resolving.
   2. `GET /list_textures` returned HTTP 200 with a combined array of 12
      entries: 10 `"kind":"texture2d"` entries (`AtmosphereTransmittanceLut`,
      `AtmosphereMultiScatteringLut`, `AtmosphereSkyViewLut_GameView`,
      `AtmosphereAerialPerspectiveVolumeDebugSlice`, `GameView`,
      `GameViewComposited`, `AtmosphereSkyViewLut_SceneView`, `SceneView`,
      `SceneViewComposited`, `Swapchain` — every one carrying `"depth":0`)
      plus 2 `"kind":"texture3d"` entries
      (`AtmosphereAerialPerspectiveVolume_GameView`/`..._SceneView`), both
      reporting `"has_depth":false` and the correct, non-zero `"depth":32`
      matching the Atmosphere volume's real Z dimension.
   3. Re-verified `GET /get_texture?texture_name=AtmosphereAerialPerspectiveVolume_GameView`
      still returns the identical 9805-byte PNG (same byte count, same
      visual raymarch thumbnail) `PHASE4_COMPLETION_REPORT.md` already
      recorded — confirms zero regression to Phase 4's own capture path.
   4. `stop_app_background` (PID 13752).
4. Per this phase's own workflow rule (Phase 0's "No full build/full
   regression test until Phase 6"), no `ctest` run was performed this phase
   — only the targeted `BuildListTexturesResponseJsonTests` filter run above.

## Result

Phase 5 is complete: `GET /list_textures` now genuinely surfaces both 2D and
volume textures side by side, with a `kind` field letting a caller
distinguish them and a `depth` field giving a volume's full 3D extent — an
LLM/AI agent can now discover every `texture_name` worth calling
`GET /get_texture` with, including the Atmosphere feature's volumes, without
any prior knowledge of the engine's internal naming convention. Every
pre-existing 2D-texture behavior (both the per-field data in
`GET /list_textures` and the whole of `GET /get_texture`) is confirmed
unchanged. Next: Phase 6 (`PHASE6_DOCS_AND_REGRESSION_SAFETY.md`) —
documentation (`AGENTS.md`/`README.md`/`TODO.md`) plus a full clean
build/regression pass, closing out the campaign.
