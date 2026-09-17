# PHASE4 — GameView Per-Entity Draw Tree Leaves (the actual user-facing feature)

_Read `PHASE0_MASTER_STRATEGY.md` (Locked Design Decision #1 and #4 especially)
and `PHASE3_PER_DRAW_ENTITY_ATTRIBUTION_CAPTURE_INFRASTRUCTURE.md` in full
first. This phase REQUIRES PHASE3's `FrameDebuggerCaptureContext::
DrawRecords()` to already exist and be populated correctly — do not start
until PHASE3's own Definition of Done is fully satisfied. This is the phase
that directly answers the user's own words: "frame debugger dont have exact
step where terrain got drawn."_

## Step 1: The Goal (Where are we going?)

Expanding the `"GameView"` tree node in the Frame Debugger must reveal one
real, individually selectable child leaf PER real draw call recorded that
frame — e.g., against `TestScene.gtscene`, expanding `"GameView"` shows two
children: `"terrain (Entity 2)"` and `"SmokeTestCube (Entity 3)"` (exact
index values depend on real ECS allocation order — verify live, do not assume
these exact numbers). Clicking `"terrain (Entity 2)"` selects it and the
right-hand detail pane shows THAT draw's own real shader name, its own real
triangle count (not the whole pass's aggregate — e.g. ~1,045,458 for terrain
alone, NOT 1,045,470), its own material texture (if any), and the shared
blend/Z/stencil/view-projection facts every draw in this one-Pipeline-config
engine already shares. This is now, permanently, "the exact step where
terrain got drawn."

## Step 2: The Situation (Where are we now?)

- `FrameDebuggerData.cpp::BuildRealFrameDebuggerSnapshot()` currently builds
  exactly one node for `"GameView"` via `BuildGameViewLeaf(*gameViewPass,
  capture, nextEventIndex++)` and pushes it straight into `root.children` with
  no children of its own.
- **Critical, confirmed gotcha in the PANEL renderer** —
  `src/Editor/Panels/FrameDebuggerPanel.cpp::RenderEventNode()` today is:
  ```cpp
  void FrameDebuggerPanel::RenderEventNode(const FrameDebuggerEventNode& node)
  {
      if (!node.isDrawCall) {
          // group branch: TreeNodeEx + recurse into node.children
          ...
          return;
      }
      // leaf branch: ImGui::Selectable(node.name.c_str(), isSelected) - DOES
      // NOT LOOK AT node.children AT ALL.
  }
  ```
  Every leaf produced by this engine UNTIL NOW never had children, so this was
  never a problem. **If PHASE4 gives `"GameView"`'s own `FrameDebuggerEventNode`
  (which is `isDrawCall == true`) real children without ALSO fixing this
  function, those children will be silently invisible in the UI** (though
  still reachable/selectable via `FrameDebuggerData.h`'s own
  `FindEventDetailsByIndex()`/the HTTP `select_event` route, since THAT
  recursive helper already recurses into every node's `children` regardless
  of `isDrawCall` — this mismatch is exactly why this gotcha is easy to miss:
  the HTTP automation would look like it "works" while the on-screen tree
  silently hides the new rows). Fixing `RenderEventNode()` is part of this
  phase's required scope, not optional polish.
- `FrameDebuggerData.h`'s `FrameDebuggerEventNode`/`FrameDebuggerEventDetails`
  struct shapes need no NEW fields for this phase — every new per-draw leaf
  reuses the exact same struct shapes `BuildComputeDispatchLeaf()`/
  `BuildGameViewLeaf()` already produce (name/isDrawCall/eventIndex/details
  with shaderName/textures/vectors/blend-Z-stencil strings) — only a new
  BUILDER function is needed, not a new data shape.

- **Second critical, confirmed gotcha (found during this campaign's own
  double-check pass, BEFORE implementation — not a shipped bug) — the new
  per-entity leaf's own `FrameDebuggerEventDetails::passName` must NEVER
  literally equal the string `"GameView"`.**
  `FrameDebuggerPanel::EnsurePreviewDescriptor()`
  (`src/Editor/Panels/FrameDebuggerPanel.cpp`, completely UNCHANGED by this
  phase — see Step 5's own "What We Will NOT Do") decides
  `isViewingGameViewLeaf` purely via an EXACT string compare,
  `details->passName == "GameView"` — the SAME check that correctly picks
  out the real `"GameView"` pass leaf itself today. If a per-entity child
  leaf's own `passName` ALSO literally read `"GameView"`, selecting it would
  silently take that SAME branch, forcing the raw pre-atmosphere-composite
  `preview` image every time, instead of falling back to the
  `compositedPreview`/`preview` rule every other non-`"GameView"` leaf
  already correctly falls back to — directly contradicting this phase's own
  Locked Design Decision #4 (`PHASE0_MASTER_STRATEGY.md`) and this phase's
  own Step 5 scope statement. See Step 3.1's `BuildGameViewDrawRecordLeaf()`
  for the fix (a distinct, human-readable `passName` value) and Step 4 for
  the regression test protecting it.

## Step 3: The Plan (exact changes)

### 3.1 New leaf builder in `FrameDebuggerData.cpp`

Add, in the same anonymous namespace as `BuildComputeDispatchLeaf()`/
`BuildGameViewLeaf()`:

```cpp
// frame-debugger-6 campaign, PHASE4 - one real, individually selectable leaf
// per real per-entity draw call this frame's "GameView" pass actually issued
// (FrameDebuggerCaptureContext::DrawRecords(), PHASE3). This is an EXPLICIT,
// user-approved breaking change to the old "one leaf per PASS, never one
// leaf per mesh" rule (see PHASE0_MASTER_STRATEGY.md's Locked Design
// Decision #1) - scoped ONLY to real children of the "GameView" leaf itself,
// nothing else about pass-level granularity elsewhere in this tree changes.
FrameDebuggerEventNode BuildGameViewDrawRecordLeaf(
    const FrameDebuggerDrawRecord& record, const Mat4& sharedViewProjection, int eventIndex)
{
    FrameDebuggerEventNode leaf;
    leaf.name = record.displayName + " (Entity " + std::to_string(record.entityIndex) + ")";
    leaf.isDrawCall = true;
    leaf.eventIndex = eventIndex;

    FrameDebuggerEventDetails details;
    details.eventIndex = eventIndex;
    details.eventLabel = "Draw Mesh";
    // IMPORTANT - deliberately NOT the literal string "GameView" (a real
    // self-contradiction this campaign's own double-check pass caught before
    // implementation): FrameDebuggerPanel::EnsurePreviewDescriptor()
    // (src/Editor/Panels/FrameDebuggerPanel.cpp, UNCHANGED by this phase - see
    // Step 5's own "What We Will NOT Do") decides `isViewingGameViewLeaf`
    // purely via `details->passName == "GameView"`, an EXACT string compare -
    // if a per-entity leaf's own passName ALSO literally read "GameView",
    // clicking it would incorrectly take the SAME branch the real "GameView"
    // leaf takes (forcing the raw pre-atmosphere-composite `preview` image,
    // always), contradicting this phase's own Step 5 "falls back to the
    // existing whole-frame compositedPreview/preview image" scope statement -
    // see Step 2's own second gotcha and Step 4's new regression test for the
    // full reasoning. "GameView (Entity Draw)" keeps the Pass row human-
    // readably tied to the real pass this draw happened inside, while
    // staying a string DISTINCT from the literal "GameView" pass leaf's own.
    details.passName = "GameView (Entity Draw)";
    details.shaderName = record.pipelineDebugName;

    if (!record.materialTextureDebugName.empty()) {
        FrameDebuggerTextureProperty texture;
        texture.name = "Material Texture";
        texture.valueLabel = record.materialTextureDebugName;
        details.textures.push_back(std::move(texture));
    }

    // vectors - this ONE draw's own real triangle count (never the whole
    // pass's aggregate - that stays on the "GameView" leaf itself, unchanged)
    // plus its own entity identity, useful for any HTTP-side consumer that
    // wants a stable, non-string key.
    {
        FrameDebuggerVectorProperty triangleCount;
        triangleCount.name = "Triangle Count";
        triangleCount.x = static_cast<float>(record.triangleCount);
        details.vectors.push_back(triangleCount);

        FrameDebuggerVectorProperty entityIdentity;
        entityIdentity.name = "Entity (Index, Generation)";
        entityIdentity.x = static_cast<float>(record.entityIndex);
        entityIdentity.y = static_cast<float>(record.entityGeneration);
        details.vectors.push_back(entityIdentity);
    }

    // matrices - the SAME shared view-projection every draw in this one real
    // Game-View pass this frame used (mirrors BuildGameViewLeaf()'s own
    // reasoning for why "last" is exactly as good as "the" here).
    {
        FrameDebuggerMatrixProperty viewProjection;
        viewProjection.name = "ViewProjection";
        for (int row = 0; row < 4; ++row) {
            for (int col = 0; col < 4; ++col) {
                viewProjection.values[static_cast<std::size_t>(row * 4 + col)] = sharedViewProjection(row, col);
            }
        }
        details.matrices.push_back(viewProjection);
    }

    // blend/Z/stencil - this engine's real, single, constant Pipeline
    // configuration, exactly like BuildGameViewLeaf()'s own identical rows
    // (there is nothing per-mesh to report here yet - see PHASE0's Locked
    // Design Decision #4).
    {
        const FrameDebuggerStandardPipelineState pipelineState = DescribeStandardPipelineState();
        details.blendMode = pipelineState.blendMode;
        details.zClip = pipelineState.zClip;
        details.zTest = pipelineState.zTest;
        details.zWrite = pipelineState.zWrite;
        details.cull = pipelineState.cull;
        details.stencilRef = pipelineState.stencilRef;
        details.stencilComp = pipelineState.stencilComp;
        details.stencilPass = pipelineState.stencilPass;
        details.stencilFail = pipelineState.stencilFail;
        details.stencilZFail = pipelineState.stencilZFail;
    }

    leaf.details = std::move(details);
    return leaf;
}
```

### 3.2 Wire it into `BuildRealFrameDebuggerSnapshot()`

Replace the existing single line
`root.children.push_back(BuildGameViewLeaf(*gameViewPass, capture, nextEventIndex++));`
with:

```cpp
FrameDebuggerEventNode gameViewLeaf = BuildGameViewLeaf(*gameViewPass, capture, nextEventIndex++);
// frame-debugger-6 campaign, PHASE4 - one real child leaf per real per-draw
// attribution record captured this frame (PHASE3), indexed strictly AFTER
// "GameView"'s own eventIndex and strictly BEFORE anything in the
// Post-GameView group below - preserves the exact same "pre < GameView <
// post" monotonic-eventIndex invariant this function's own PHASE2 (of
// frame-debugger-5) comment already documents, simply extended one level
// deeper (pre < GameView < GameView's own children < post).
for (const FrameDebuggerDrawRecord& record : capture.DrawRecords()) {
    gameViewLeaf.children.push_back(BuildGameViewDrawRecordLeaf(record, capture.LastViewProjection(), nextEventIndex++));
}
root.children.push_back(std::move(gameViewLeaf));
```

Place this exactly where the old single-line push currently sits (between the
pre-GameView loop/group-push and the post-GameView loop) — do not reorder
relative to the pre-/post-loops themselves.

### 3.3 Fix `FrameDebuggerPanel.cpp::RenderEventNode()` (REQUIRED, see Step 2's
gotcha)

Replace the function with one that branches on "does this node have
children" FIRST (regardless of `isDrawCall`), so a selectable leaf that also
has children (the new `"GameView"` shape) renders as an expandable AND
selectable row, while every pre-existing leaf/group shape (no children, or
`isDrawCall == false` with children) keeps rendering exactly as before:

```cpp
void FrameDebuggerPanel::RenderEventNode(const FrameDebuggerEventNode& node)
{
    if (!node.children.empty()) {
        // frame-debugger-6 campaign, PHASE4 - handles BOTH a pure group
        // (isDrawCall == false, e.g. "Compute Dispatches (Pre-GameView)")
        // AND a selectable leaf that ALSO has real children (new this
        // campaign - "GameView" itself, once it has per-entity draw-record
        // children). ImGuiTreeNodeFlags_Selected highlights the row exactly
        // like the leaf-only Selectable() branch below already does;
        // IsItemClicked() right after TreeNodeEx() is what makes clicking
        // the row (not just its arrow) select it, mirroring Selectable()'s
        // own click-to-select behavior.
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_DefaultOpen;
        if (node.isDrawCall && node.eventIndex == m_selectedEventIndex) {
            flags |= ImGuiTreeNodeFlags_Selected;
        }
        const bool open = ImGui::TreeNodeEx(node.name.c_str(), flags);
        if (node.isDrawCall && ImGui::IsItemClicked()) {
            m_selectedEventIndex = node.eventIndex;
        }
        if (open) {
            for (const FrameDebuggerEventNode& child : node.children) {
                RenderEventNode(child);
            }
            ImGui::TreePop();
        }
        return;
    }

    if (!node.isDrawCall) {
        // Defensive only - every real group this engine builds today only
        // gets added when it already has >= 1 child (see
        // BuildRealFrameDebuggerSnapshot()'s own "only add if non-empty"
        // rule) - this branch should be unreachable in practice.
        return;
    }

    // A leaf draw-call row with NO children - unchanged from every prior
    // campaign.
    const bool isSelected = (node.eventIndex == m_selectedEventIndex);
    if (ImGui::Selectable(node.name.c_str(), isSelected)) {
        m_selectedEventIndex = node.eventIndex;
    }
}
```

Re-read the REAL, current `RenderEventNode()`/`BuildEventTreePane()` in
`FrameDebuggerPanel.cpp` before editing — the exact surrounding code (includes,
member access, `ImGuiTreeNodeFlags` availability) may differ slightly from
this sketch; keep the same visual behavior (default-open groups, selected-row
highlight) that already exists today for every pre-existing node shape.

### 3.4 Doc updates (required, not optional)

- `docs/conventions/frame-debugger.md` — update the tree-shape section (the
  fenced-code-block diagram under "What is real today") to show `"GameView"`
  now has its own children (one per real draw), and add a new bullet
  explaining the per-entity attribution feature, its own "What We Will NOT
  Do" scope limit (no isolated per-mesh preview image — see PHASE0's Locked
  Design Decision #4), and that this is this campaign's own explicit,
  user-approved breaking change to the historical one-leaf-per-pass rule.
  Keep every existing `frame-debugger-2`/`3`/`4`/`5` historical prose intact —
  ADD a new `## Known limitation, now fixed (frame-debugger-6 campaign)` or
  `## What's new (frame-debugger-6 campaign)` section rather than rewriting
  history.
- `AGENTS.md`'s "Frame Debugger" section — update the summary paragraph the
  same way (shorter — this file is a summary + link, per its own top-of-file
  convention; the full detail belongs in `docs/conventions/frame-debugger.md`).
- `FrameDebuggerData.h`'s own header comment describing
  `BuildRealFrameDebuggerSnapshot()`'s tree shape — update alongside the code
  change in Step 3.2 (keep it truthful/current, this is what future engineers
  will actually read first).

## Step 4: Tests

- `tests/Editor/FrameDebuggerSnapshotBuilderTests.cpp` — add a case feeding a
  hand-built `FrameDebuggerCaptureContext`-equivalent (or whatever this test
  file's existing fixture pattern uses to fake capture data — read the
  existing tests first) with 2+ fake `FrameDebuggerDrawRecord` entries and
  asserting: (a) the `"GameView"` node in the resulting snapshot has exactly
  that many children, (b) each child's `name`/`eventIndex` is correct, (c)
  `eventIndex` stays strictly monotonic increasing across
  pre-group → GameView → GameView's children → post-group, (d)
  `snapshot.totalEventCount` correctly includes the new children.
- `tests/Editor/FrameDebuggerDataTests.cpp` — if `FindEventDetailsByIndex()`
  has its own dedicated test cases, add one confirming it can find a
  per-entity CHILD leaf's own details by index (proving the existing
  recursive search already works correctly against the new nested shape,
  since `FindEventDetailsByIndexRecursive()` itself needs NO changes for this
  phase — only `RenderEventNode()`, the ImGui-side panel code, needed a fix).
- **REQUIRED regression test (found by this campaign's own double-check
  pass, not a shipped bug)**: in `tests/Editor/FrameDebuggerSnapshotBuilderTests.cpp`,
  assert that a `BuildGameViewDrawRecordLeaf()`-produced child leaf's own
  `details->passName` is NEVER the literal string `"GameView"` — only the
  actual `"GameView"` pass leaf itself may ever carry that exact value.
  `FrameDebuggerPanel::EnsurePreviewDescriptor()`
  (`src/Editor/Panels/FrameDebuggerPanel.cpp`, UNCHANGED by this phase) decides
  `isViewingGameViewLeaf` via `details->passName == "GameView"` — an EXACT
  string compare — so a per-entity leaf whose own `passName` also happened to
  literally read `"GameView"` would incorrectly take the SAME branch the real
  `"GameView"` leaf takes (forcing the raw pre-atmosphere-composite `preview`
  image, always), directly contradicting Step 5's own "falls back to the
  existing whole-frame `compositedPreview`/`preview` image" scope statement.
  See Step 3.1's own `details.passName` value/comment for the fix this test
  protects, and Step 6's new manual live-check bullet for the matching
  end-to-end proof.
- No new HTTP route is needed (`select_event`/`state` already work generically
  off the flattened, already-correct `totalEventCount`/`FindEventDetailsByIndex()`
  path) — if `tests/Network/NetworkRoutesTests.cpp` or
  `tests/Application/FrameDebuggerCommandBridgeTests.cpp` have any hand-built
  snapshot fixtures that assume a fixed, old `totalEventCount` for a scene with
  draws, double check whether any such fixture needs updating (unlikely, since
  those tests almost certainly use fully synthetic/hand-built data
  independent of any real scene, but verify rather than assume).

## Step 5: What We Will NOT Do (explicit scope limits)

- We will NOT attempt to render an isolated/cropped/masked image of just one
  entity's own pixels for its own leaf's preview box — selecting a per-entity
  leaf still falls back to the existing whole-frame
  `compositedPreview`/`preview` image via the UNCHANGED
  `ChooseFrameDebuggerPreviewSource()` rule (this function needs NO changes in
  this phase — verify it is not accidentally touched).
- We will NOT add per-entity blend/Z/stencil variation — this engine still
  has exactly one real `Pipeline` configuration; every per-entity leaf reports
  the same `DescribeStandardPipelineState()` facts as `"GameView"` itself.
- We will NOT change anything about the Pre-/Post-GameView compute-dispatch
  groups in this phase (that was PHASE1/PHASE2, already done) — this phase
  only adds children under the `"GameView"` leaf itself.

## Step 6: Definition of Done for PHASE4

- [ ] `BuildGameViewDrawRecordLeaf()` added; `BuildRealFrameDebuggerSnapshot()`
      wires it in with correct, monotonic `eventIndex` bookkeeping.
- [ ] `FrameDebuggerPanel::RenderEventNode()` fixed so a selectable leaf with
      children (i.e. `"GameView"`) actually renders its children on screen —
      verified live, not just by reading the code (see the manual live-check bullets below).
- [ ] Doc files updated per Step 3.4.
- [ ] New/updated Tier-1 tests pass.
- [ ] Incremental build succeeds.
- [ ] Manual live re-check: launch the engine, load `TestScene.gtscene`,
      open+enable+capture the Frame Debugger over HTTP, `GET /get_swapchain`,
      and visually confirm `"GameView"` is now expandable and shows a
      `"terrain (Entity N)"` child leaf (plus `"SmokeTestCube (Entity M)"`).
      Select the terrain child via `GET /frame_debugger/select_event?index=<n>`
      (find its real index from the just-captured tree/state), screenshot
      again, and visually confirm the detail pane shows a triangle count
      close to terrain's own ~1,045,458 (NOT the aggregate 1,045,470) and a
      real shader name. Stop the background process when done.
- [ ] Manual regression check on the PREVIEW box specifically (added by this
      campaign's own double-check pass, which found a real self-contradiction
      risk in this phase's original Step 3.1 code sample): with the terrain
      child leaf still selected from the check above, confirm the preview
      texture shown is the SAME image as when nothing is selected (the real,
      final `compositedPreview` — atmosphere fog included), NOT the raw
      pre-atmosphere-composite image the literal `"GameView"` leaf itself
      shows when IT is selected. This confirms `BuildGameViewDrawRecordLeaf()`'s
      own `details.passName` value (Step 3.1) does not literally read
      `"GameView"` and therefore does not collide with
      `FrameDebuggerPanel::EnsurePreviewDescriptor()`'s existing
      `isViewingGameViewLeaf = (details->passName == "GameView")` exact-string
      check — a collision there would silently force every per-entity leaf to
      show the pre-composite-only image, contradicting Step 5's own "falls
      back to the existing whole-frame compositedPreview/preview" statement.
- [ ] Commit via `git_add`/`git_commit`.
