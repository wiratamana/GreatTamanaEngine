# PHASE6 — Automated Tests, Documentation, and Full Regression Pass

> **Second-iteration audit note (this revision):** this file was re-checked
> against the CURRENT (already twice-audited) Phases 1–5 documents and against
> `PHASE0_MASTER_STRATEGY.md`'s own "Definition of Done" checklist, item by
> item. Several real gaps were found and are fixed in this revision:
> 1. **MAJOR — the manual end-to-end verification (Step 3.5) was missing the
>    single check `PHASE0_MASTER_STRATEGY.md`'s own "Definition of Done"
>    calls out BY NAME as the specific regression test for Locked Design
>    Decision 7's corrected, four-call-site scope:
>    `GET /get_texture?texture_name=Swapchain`.** Without it, the exact bug
>    class Phase 3's own audit fixed (a forgotten
>    `NotifyDebugTextureStateOverride()` correction call producing a stale
>    `previousState` and therefore a garbled image / validation error for
>    `"Swapchain"`) could regress silently even though every other check in
>    this file passed. Added as its own numbered step, plus a best-effort
>    companion check for `"BlurredSceneOutput"` (the fourth call site),
>    explicitly marked optional since it requires toggling an Editor-only
>    debug checkbox with no HTTP equivalent.
> 2. **MAJOR — the `/list_textures` manual check (Step 3.5) silently dropped
>    the `format` field entirely**, even though `PHASE0_MASTER_STRATEGY.md`'s
>    own endpoint contract AND its own "Definition of Done" both explicitly
>    require it ("correct regime/extent/**format**/hasDepth metadata") — this
>    mirrors exactly the gap Phase 5's own second-iteration audit found and
>    fixed in the response body itself; this file simply hadn't caught up to
>    that fix. Also hedged on the swapchain texture's own name ("whatever the
>    swapchain's own imported texture name turns out to be") where Phases
>    0–3's own audits have since definitively confirmed it is the literal
>    string `"Swapchain"` (never `"Present"`) — this file now asserts that
>    literal name directly, since confirming it is itself a regression check
>    against the exact naming confusion Phase 0's audit history records.
> 3. **Gap — the manual verification never exercised `/get_texture`'s own
>    JSON/base64 envelope at all**, meaning `frames_since_update` (Locked
>    Design Decision 4 — one of this whole campaign's headline features) was
>    never actually checked against a running engine anywhere in this
>    document. Added as its own step.
> 4. **Gap — Step 3.1's checklist collapsed two DIFFERENT files under one
>    vague "`CMakeLists.txt`" bullet, and mis-cited Phase 1/2's own note in a
>    way that could mislead an implementer into checking only ONE of them.**
>    There are two distinct files in play: the repository ROOT
>    `CMakeLists.txt` (registers new PRODUCTION `.cpp` files — Phase 1's
>    `RenderGraphDebugTextureRegistry.cpp`, Phase 3's
>    `DepthVisualization.cpp`) and `tests/CMakeLists.txt`'s own
>    `GTE_TEST_SOURCES` list (registers new TEST `.cpp` files — Phase 1's and
>    Phase 3's own new test files; Phase 4/5 add cases to already-registered
>    files, so they need no new registration). Both phases' own documents
>    already correctly distinguish these as two separate steps — this
>    checklist now does too, explicitly, plus adds the
>    `--gtest_list_tests` confirmation both of those phases call for.
> 5. **Gap — the checklist under 3.1 did not list the NEW test cases Phase
>    4's and Phase 5's own second-iteration audits added** (a concurrent
>    "second request while one is pending returns `alreadyPending`
>    immediately, never blocks, never clobbers the first request's payload"
>    case; a `SlotFor()` "three mutually distinct slots" regression case; a
>    `PublishTextureList()` "publishing an empty list after a non-empty one
>    actually clears it" case; a `BuildListTexturesResponseJson()`
>    double-quote-in-a-name escaping case) — a contributor relying ONLY on
>    this file's own checklist (rather than re-reading Phase 4/5 in full)
>    could easily miss that these are now required, not optional. All four
>    are now listed explicitly.
> 6. **Addition — `AGENTS.md`'s new sub-section (Step 3.2) previously didn't
>    say enough for a future contributor to avoid re-introducing two
>    already-diagnosed mistakes**: confusing a render-graph PASS name with a
>    texture's own registered NAME (the exact `"Present"` vs. `"Swapchain"`
>    confusion `PHASE0_MASTER_STRATEGY.md`'s own audit history records at
>    length), and collapsing `FrameCaptureBridge.h`'s `PublishedTextureListEntry`
>    with `NetworkRoutes.h`'s `TextureListEntryView` into one shared type
>    that would violate this codebase's own layering rules (see Phase 5's
>    own "Option 1 vs Option 2" resolution). Both are now explicitly called
>    for in Step 3.2's own bullet list, alongside the `NotifyDebugTextureStateOverride()`
>    call sites being named as FOUR (not two), matching the corrected count
>    Phase 0/2/3 settled on.
>
> Everything else in this file (the Tests checklist's overall shape, the
> `README.md` bullet wording, the build/`ctest` regression commands, and the
> campaign-completion-report guidance) was re-checked and found accurate — no
> further changes were needed there.

Parent: `PHASE0_MASTER_STRATEGY.md`. Depends on: Phases 1–5 (everything —
this phase closes out the whole campaign).

## Step 1: The Goal (Where are we going?)

Bring every new Tier-1-testable module up to full automated test coverage
(any left as a "see Phase N" placeholder in an earlier phase), update
`AGENTS.md`/`README.md` to document the new feature and its one new
sanctioned pattern (a request-scoped `vkDeviceWaitIdle()` stall — an
explicit, deliberate EXCEPTION to this engine's usual "zero added GPU
stall" networking rule, which needs recording so a future contributor
doesn't mistake it for an accident to "fix"), run a full build + `ctest`
regression pass, and manually verify the whole feature end-to-end against
a real, running `GreatTamanaEngine.exe`. This is the campaign's closing
phase — after this, `network-impl-4`'s `PHASE0_MASTER_STRATEGY.md`'s own
"Definition of Done" checklist must be fully satisfied — **every bullet
in that checklist, not just most of them; Step 3.5 below is written to
map onto it one-for-one, see the audit note above for why that mapping
previously had gaps.**

## Step 2: The Situation (Where are we now?)

- Every earlier phase already specified its OWN Tier-1 tests inline (Phase
  1's registry tests, Phase 3's depth-conversion tests, Phase 4's bridge
  extension tests, Phase 5's route/JSON tests) — this phase's job is to
  make sure all of them actually EXIST and PASS, not to invent yet another
  round of tests from scratch. Treat each earlier phase's own "### Tests"
  section as this phase's own checklist — **including whatever a phase's
  own second-iteration audit note ADDED to that section**, since Phases
  1–5 have all already been through their own double-check pass and several
  of them added new required test cases as part of that pass (see Step
  3.1's own explicit list below — this is not a hypothetical, it already
  happened for Phases 4 and 5).
- `AGENTS.md`'s existing "Networking" section (updated once already, by
  `network-impl-2`, to document `FrameCaptureBridge` as "the one sanctioned
  exception" to the "a route handler must be a PURE function of its own
  request data only" rule) needs a SECOND, narrower addendum: this
  campaign's `vkDeviceWaitIdle()` stall is itself a deliberate, narrow
  exception to a DIFFERENT existing rule (`network-impl-2`'s own "zero
  added GPU stall" swapchain-capture design principle) — both must be
  clearly written down as accepted, bounded, intentional exceptions, each
  scoped to exactly the one endpoint that needs it, not a general license
  to add more stalls elsewhere.
- **Confirmed live (spot-checked as part of this review): `AGENTS.md`
  really does already have an existing "Networking" section (added by
  `network-impl-1`, extended by `network-impl-2`/`network-impl-3`), and
  `README.md`'s "Status" section really does already carry prior
  `network-impl-2`/`network-impl-3` bullets in the same "Status" list this
  phase's own new bullet slots into** — both documents' existing tone/level
  of detail (a short paragraph per campaign, cross-referencing that
  campaign's own `PHASE0_MASTER_STRATEGY.md`) is what this phase's own
  additions below must match, not invent a new documentation style.
- `README.md`'s "Status" section already has bullets for `/get_swapchain`/
  `/get_game_view` (added by `network-impl-2`) and the `POST` endpoints
  (added by `network-impl-3`) — this campaign's own bullet slots in right
  alongside them.
- **Phase 5's own "Option 1 vs Option 2" ambiguity for `frames_since_update`
  plumbing into `/list_textures` has ALREADY been resolved** by Phase 5's
  own second-iteration audit (a third, small, plain-data struct,
  `PublishedTextureListEntry`, added to `FrameCaptureBridge.h`, kept
  deliberately separate from `NetworkRoutes.h`'s own `TextureListEntryView`
  — see that phase's own Step 2/3.3 for the full reasoning). This phase's
  own completion-report guidance (Step 3.6) should record WHICH resolution
  was actually shipped (there is no longer an open choice to record a
  decision about — Phase 5 already made and documented it), and Step 3.2's
  own `AGENTS.md` addition should explain the two-struct split so a future
  contributor doesn't "simplify" it back into one shared type.
- **There are TWO distinct `CMakeLists.txt` files this campaign touches, and
  they serve different purposes — do not conflate them:** the repository
  ROOT `CMakeLists.txt` (registers new PRODUCTION `.cpp` files — Phase 1's
  `RenderGraphDebugTextureRegistry.cpp`, Phase 3's `DepthVisualization.cpp`,
  both confirmed live as needing an entry there) and `tests/CMakeLists.txt`'s
  own `GTE_TEST_SOURCES` list (registers new TEST `.cpp` files — Phase 1's
  `RenderGraphDebugTextureRegistryTests.cpp` and Phase 3's
  `DepthVisualizationTests.cpp` specifically; Phase 4/5 only ADD test CASES
  to two already-registered files, `FrameCaptureBridgeTests.cpp`/
  `NetworkRoutesTests.cpp`, so neither needs a new registration entry).
  Confirmed live: `tests/CMakeLists.txt` already lists
  `Network/NetworkRoutesTests.cpp` as a flat, plain relative path in its
  `GTE_TEST_SOURCES`-style list, and the root `CMakeLists.txt` already lists
  `src/Renderer/RenderGraph/RenderGraphSnapshot.cpp` the same way — these
  are the two exact neighboring-line anchors Phase 1/3 each already tell you
  to grep for.

## Step 3: The Plan

### 3.1 — Confirm every earlier phase's own tests exist and pass

Checklist (cross-reference against each phase document's own "### Tests"
section, INCLUDING each phase's own second-iteration audit-note additions to
that section — re-read them fresh, do not rely on memory or on this
checklist alone as a substitute for that re-read):

- [ ] `tests/Renderer/RenderGraph/RenderGraphDebugTextureRegistryTests.cpp`
      (Phase 1) — `Upsert`/`FindByName`/`ListAll`/`ApplyColorStateOverride`
      coverage: empty-registry `FindByName`/`ListAll` behavior, a full
      round-trip including `lastUpdatedFrameCounter`, "overwrite in place,
      never grows past one entry per name", two different names preserved
      in first-seen order, "override on an EXISTING name touches ONLY
      `colorState`, nothing else" (this specifically protects the
      `frames_since_update` freshness metric from a silent regression), and
      "override on an unknown name is a safe no-op".
- [ ] `tests/Encoding/DepthVisualizationTests.cpp` (Phase 3) — all three
      supported depth formats converted byte-for-byte correctly, the
      "unrecognized format returns `false` and leaves the output buffer
      untouched" case, AND the "null `rawDepth`/`outRgba8` pointer with a
      genuinely positive width/height returns `false`" case (added by this
      phase's own second-iteration audit — easy to miss if only skimming
      the original Step 3.6 wording).
- [ ] `tests/Application/FrameCaptureBridgeTests.cpp` extensions:
  - [ ] Phase 4 — a `NamedTexture`-kind request/fulfill/fail round-trip,
        mirroring the existing `Swapchain`/`GameView` shapes.
  - [ ] Phase 4 (added by ITS OWN audit) — a pending `NamedTexture` request
        for one name, then a SECOND, concurrent `NamedTexture` request for
        a DIFFERENT name, returns `alreadyPending == true` immediately,
        never blocks, and never overwrites the first request's own
        recorded name/channel.
  - [ ] Phase 4 (added by ITS OWN audit) — `SlotFor()` still returns three
        MUTUALLY DISTINCT `Slot` objects (requesting all three kinds
        concurrently, e.g. from three simulated threads, and confirming
        none observes another's `requested`/`result` state) — the
        regression test for the ternary→switch rewrite.
  - [ ] Phase 5 — `PublishTextureList()`/`GetPublishedTextureList()`
        round-trip (publish from one simulated thread, read back from
        another).
  - [ ] Phase 5 — `GetPublishedTextureList()` returns an empty vector
        before ANY `PublishTextureList()` call has ever been made.
  - [ ] Phase 5 — publishing an EMPTY list after a non-empty one actually
        CLEARS it (never "sticky" leftover entries) — the regression test
        for `PublishTextureList()`'s own "overwrites wholesale, never
        merges" contract.
- [ ] `tests/Network/NetworkRoutesTests.cpp` extensions (Phase 5):
  - [ ] Table-driven `ParseGetTextureQuery()` coverage — missing/empty
        `texture_name`, `channel` absent/`"color"`/`"depth"`, and an
        invalid/wrong-case `channel` value (e.g. `"Depth"`/`"COLOR"`),
        confirming the exact-lowercase-only matching rule.
  - [ ] `BuildTextureCaptureJsonBody()` — re-parsed via
        `nlohmann::json::parse()`, individual fields asserted (never a raw
        literal string comparison — this function is `nlohmann::json`-built,
        not hand-formatted).
  - [ ] `BuildListTexturesResponseJson({})` — the one case safe to assert as
        a literal string, `{"textures":[]}`.
  - [ ] `BuildListTexturesResponseJson()` with 2+ entries — re-parsed,
        every field (`name`/`regime`/`format`/`width`/`height`/`has_depth`/
        `frames_since_update`) asserted by key.
  - [ ] A texture name containing a double-quote character round-trips
        correctly through `BuildListTexturesResponseJson()`'s JSON escaping
        (added by this phase's own second-iteration audit, mirroring the
        existing `InstantiatePrimitiveNameWithQuoteRoundTripsThroughJson`
        precedent) — proves `nlohmann::json`'s own escaping is what's
        actually protecting this endpoint, not hand-formatting.
- [ ] **Root `CMakeLists.txt`** includes the two new PRODUCTION source
      files: `RenderGraphDebugTextureRegistry.cpp` (Phase 1, alongside
      `RenderGraphSnapshot.cpp`) and `DepthVisualization.cpp` (Phase 3,
      alongside `PixelConversion.cpp`).
- [ ] **`tests/CMakeLists.txt`'s own `GTE_TEST_SOURCES` list** includes the
      two new TEST source files: `RenderGraphDebugTextureRegistryTests.cpp`
      (Phase 1) and `DepthVisualizationTests.cpp` (Phase 3). (Phase 4/5's
      own test additions extend `FrameCaptureBridgeTests.cpp`/
      `NetworkRoutesTests.cpp`, both already registered — no new entry
      needed for those two.)
- [ ] Confirm both of the above registrations actually took effect by
      checking the built test binary's own `--gtest_list_tests` output
      includes `RenderGraphDebugTextureRegistryTests.*` and
      `DepthVisualizationTests.*` — Phase 1/3 each call this out explicitly
      as the one way to catch a test file that compiles nowhere and
      silently never runs (a green `ctest` for the WRONG reason).

### 3.2 — `AGENTS.md` updates

In the existing "Networking" section, add (do not replace) a short
sub-section (naming it something like "Named Texture Capture
(`GET /get_texture`)"):

- Documents `RenderGraphDebugTextureRegistry` as the mechanism: every
  texture any pass declares via `RenderGraphBuilder::CreateTexture()`/
  `ImportTexture()` becomes automatically capturable by that exact name,
  with zero opt-in required — but that buffers (`CreateBuffer()`/
  `ImportBuffer()`) are NEVER visible this way, by design (a completely
  separate `BufferHandle`/`bufferNames` vocabulary this registry never
  touches).
- **Explicitly warns about the pass-name vs. texture-name distinction** —
  this campaign's own strategy documents record a real, corrected mistake
  where an earlier revision conflated a render-graph PASS's name (e.g.
  `"Present"`) with the TEXTURE name actually registered for capture (e.g.
  `"Swapchain"`, via `ImportTexture("Swapchain", ...)` inside that pass). A
  future contributor adding a new named texture must always check the
  literal string passed to `CreateTexture()`/`ImportTexture()`, never the
  name of the `AddPass()` call it happens to sit near.
- Documents the ONE, bounded, explicit exception this campaign makes to
  the "zero added GPU stall" rule `network-impl-2` established for
  `/get_swapchain`: `GET /get_texture` (and ONLY that endpoint) calls
  `Renderer::WaitForGpuIdle()` (a full `vkDeviceWaitIdle()`) once per
  request, BEFORE reading pixels back — acceptable because this endpoint is
  rare and human/LLM-triggered, never part of any per-frame path. State
  explicitly: **no other endpoint, and no per-frame engine code, may ever
  call `Renderer::WaitForGpuIdle()`** — this is a warning for future
  contributors, not just a historical note.
- Documents the `NotifyDebugTextureStateOverride()` correction hook and WHY
  it exists (a graph-external manual barrier is invisible to the render
  graph's own internal state tracking) — **naming all FOUR existing call
  sites** (not two — an earlier revision of this campaign's own strategy
  undercounted this before its own audit caught it): `Application::Run()`'s
  two `FinalizeRenderTextureForExternalSampling()` calls
  (`"GameView"`/`"SceneView"`), `FramePresenter.cpp`'s own `"Swapchain"`
  `PRESENT_SRC_KHR` finalize, and
  `ComputeBlurValidation::FinalizeForSampling()`'s `"BlurredSceneOutput"`
  finalize — so a future pass author adding a SIMILAR graph-external manual
  transition for some other named texture knows to add the matching
  correction call too, rather than leaving that texture's registry entry
  silently wrong.
- Documents the `frames_since_update` counter's own caveat (Locked Design
  Decision 4/Phase 2's own Step 3.3a): it only advances once per real
  `SynchronousImmediateReadback` `Execute()` call, so a texture registered
  ONLY by the pipelined regime (today: `"Swapchain"`) has its own freshness
  signal driven by how often the OFFSCREEN regime happens to run elsewhere,
  not by how often it itself updates — in a session where both Editor
  panels are hidden (or any `-DGTE_ENABLE_EDITOR=OFF` build), this reads as
  a constant, maximally-fresh value regardless of real elapsed frames. This
  is intentional, not a bug to "fix" with a second counter.
- Documents the `PublishedTextureListEntry` (`FrameCaptureBridge.h`) vs.
  `TextureListEntryView` (`NetworkRoutes.h`) split behind `/list_textures` —
  two small, nearly-identical, deliberately SEPARATE structs, one per
  layer, with `Application::Run()` resolving `rg::DebugTextureSnapshot` →
  `PublishedTextureListEntry` and `NetworkServer.cpp` doing the trivial 1:1
  copy into `TextureListEntryView` — never collapse these into one shared
  type crossing the `Application`/`Network` layer boundary; this is the
  exact same "don't take a foreign layer's struct" rule this section
  already establishes for `EngineCommandBridge`, applied here to a second,
  independent case.
- Documents the two new endpoints' exact contract (mirroring how
  `network-impl-2`'s own addition documented `/get_swapchain`/
  `/get_game_view`'s contract): `GET /get_texture?texture_name=<name>
  [&channel=color|depth][&format=png|base64|json]` (including its full
  failure-status mapping — 400/409/503/504) and `GET /list_textures`
  (`name`/`regime`/`format`/`width`/`height`/`has_depth`/
  `frames_since_update` per entry).

### 3.3 — `README.md` updates

Add one bullet to the "Status" section (matching the existing style/
location of the `network-impl-2`/`network-impl-3` bullets already there),
e.g.: *"Any render-graph texture can be captured as a PNG by name over
HTTP (`GET /get_texture?texture_name=...`), including a texture's depth
buffer (`&channel=depth`) — useful for debugging off-screen intermediate
passes (LUTs, etc.) that never otherwise appear on screen. `GET
/list_textures` lists every texture name registered so far this session."*

### 3.4 — Full build + regression

```
cmake --build build
cd /d C:\Users\F5954\Documents\TAMANA\GreatTamanaEngine\build && ctest -C Debug --output-on-failure
```

Both must succeed with zero failures/regressions in EVERY existing test
(not just the new ones this campaign added) before this phase — and the
whole campaign — is considered done.

### 3.5 — Manual end-to-end verification

Using `run_app_background`/`gte_send_request`-style tooling (or a plain
`curl`), against a running Editor build with the "Game" panel visible. This
list is written to map one-for-one onto `PHASE0_MASTER_STRATEGY.md`'s own
"Definition of Done" checklist — treat any item below that fails as that
checklist item failing too, not a separate, lesser concern:

1. `GET /get_texture?texture_name=GameView` → valid PNG, visually identical
   to what `GET /get_game_view` returns for a similarly-timed request (the
   two endpoints must agree on the SAME texture's contents — this is the
   single strongest end-to-end proof the new generic path is correct,
   since it can be checked against an ALREADY-TRUSTED, independent
   endpoint).
2. `GET /get_texture?texture_name=GameView&channel=depth` → a valid,
   visibly depth-map-shaped (not solid black/white/garbage) grayscale PNG.
3. `GET /get_texture?texture_name=GameView&format=json` (or
   `?format=base64`) → a JSON envelope containing `width`/`height`/
   `format:"png"`/`data_base64`/**`frames_since_update`** — decode the
   base64 and confirm it is the same valid PNG as step 1, and confirm
   `frames_since_update` is present and numeric (this is the one and only
   manual check anywhere in this campaign's documents that actually
   exercises Locked Design Decision 4's headline `frames_since_update`
   field end-to-end — do not skip it).
4. `GET /get_texture?texture_name=GameView&channel=bogus` → HTTP `400`.
5. `GET /get_texture` (no `texture_name` at all) → HTTP `400`.
6. `GET /get_texture?texture_name=ThisNameNeverExists` → HTTP `504` after
   ~3 seconds (never hangs forever, never crashes the engine).
7. `GET /get_texture?texture_name=SceneView&channel=depth` while the
   Editor's "Scene" panel is HIDDEN → confirm this behaves like any other
   "hasn't rendered yet" case (eventually 504 once the fixed timeout
   elapses, since "SceneView" simply isn't being resolved into the
   registry at all right now — not a crash, not a hang).
8. **`GET /get_texture?texture_name=Swapchain` → a valid PNG, visually
   plausible as a recent frame of the presented window content.** This is
   `PHASE0_MASTER_STRATEGY.md`'s own named, explicit regression check for
   Locked Design Decision 7's corrected, four-call-site scope — a failure
   here (a garbled image, a validation-layer error, or a crash) most likely
   means `FramePresenter.cpp`'s own `"Swapchain"` correction call
   (`NotifyDebugTextureStateOverride()`, Phase 3's Step 3.4a) was forgotten
   or broken. **Do not treat this file's own checklist as complete without
   this specific check having been run** — it is easy to accidentally only
   ever exercise `GameView`/`SceneView` (both wired via `Application.cpp`)
   and never notice a regression specific to the `FramePresenter.cpp` call
   site, which is the one of the four that lives in a completely different
   file.
9. **Best-effort: `GET /get_texture?texture_name=BlurredSceneOutput`** (the
   fourth `NotifyDebugTextureStateOverride()` call site,
   `ComputeBlurValidation::FinalizeForSampling()`) → a valid PNG, IF the
   Editor's "Show Compute Blur (debug)" toggle can be turned on and the
   "Scene" panel made visible for this test run. This texture name is only
   ever registered while that Editor-only debug checkbox is on — there is
   no HTTP-only way to enable it, so this check is optional/best-effort
   depending on what tooling is available for this verification pass, but
   should not be skipped without recording that it was skipped (and why)
   in the completion report (Step 3.6).
10. `GET /list_textures` → a JSON array containing, at minimum, the literal
    names `"GameView"` and `"Swapchain"` (and `"SceneView"`/
    `"BlurredSceneOutput"` if visible/enabled this session) — **explicitly
    confirm the swapchain entry's own `name` field reads exactly
    `"Swapchain"`, never `"Present"`** (this is the direct regression check
    for the pass-name/texture-name confusion `PHASE0_MASTER_STRATEGY.md`'s
    own audit history records at length — see Step 3.2 above). For each
    entry, confirm `width`/`height`/`regime`/`has_depth` are plausible AND
    **confirm `format` is a real, recognizable string (e.g.
    `"B8G8R8A8_UNORM"`/`"R8G8B8A8_UNORM"`) for every one of these specific,
    real engine targets — never the `"VkFormat(...)"` numeric fallback**;
    that fallback showing up for a known target would mean
    `DebugTextureColorFormatName()`'s own format list has drifted from what
    this engine's swapchain/render-textures actually negotiate.
11. Confirm the running engine's own window keeps rendering/updating at a
    normal frame rate the ENTIRE time these requests are being issued,
    INCLUDING the one visible hitch `WaitForGpuIdle()` causes on the exact
    frame each `/get_texture` request is actually serviced (this is the
    ONE expected, accepted deviation from "the engine never stutters for a
    network request" — confirm it is a single, bounded hitch, not a
    sustained stall/hang).
12. Confirm `/get_swapchain`/`/get_game_view`/`/http_hello_world`/
    `/instantiate_primitive`/`/delete_entity` (every endpoint from prior
    campaigns) still behave exactly as before — this campaign must not
    regress any of them.

### 3.6 — Campaign completion report

Write `NETWORK_IMPL_4_CAMPAIGN_COMPLETION_REPORT.md` (mirroring
`network-impl-1/NETWORK_IMPL_1_CAMPAIGN_COMPLETION_REPORT.md`'s own shape)
summarizing: what was built, any deviations from this strategy that turned
out to be necessary (note that Phase 5's own "Option 1 vs Option 2" choice
for `frames_since_update`/`/list_textures` plumbing was ALREADY resolved
during Phase 5's own second-iteration audit — record here which shape was
actually shipped, i.e. the third, separate `PublishedTextureListEntry`
struct, and why, rather than presenting it as still-open), whether the
`"BlurredSceneOutput"` best-effort manual check (Step 3.5, item 9) was
actually performed or skipped (and why, if skipped), and any real follow-up
work worth flagging for a FUTURE campaign (e.g. "once atmosphere scattering
is actually implemented, confirm its LUT pass names show up in
`/list_textures` with zero extra code, as designed").

### 3.7 — What this phase deliberately does NOT do

- Does not add any NEW feature/endpoint of its own — purely
  tests/docs/regression/verification for Phases 1–5's already-complete
  work.
- Does not attempt to implement or even prototype the atmosphere-
  scattering feature itself, even just to "prove" the new mechanism works
  end-to-end for a real LUT — the `GameView`/`SceneView`/`Swapchain`
  verification in 3.5 already proves the mechanism works generically
  across both `ExecuteTimingMode` regimes; inventing a fake LUT pass purely
  for this campaign's own testing would be scope creep into a genuinely
  separate, future feature (see `PHASE0_MASTER_STRATEGY.md`'s own
  Non-Goals).
