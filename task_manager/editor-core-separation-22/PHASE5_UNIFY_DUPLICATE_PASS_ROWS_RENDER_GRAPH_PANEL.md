# PHASE5 — Make the "Render Graph" panel's pass table and its "Disabled Built-In Passes" section agree on how many rows one pass name is

Parent: `PHASE0_MASTER_STRATEGY.md` (read it fully first — Step 2.1 has the
full root-cause trace this phase implements against).
Campaign folder: `task_manager/editor-core-separation-22/`
Previous phase report to read first: `PHASE4_COMPLETION_REPORT.md` — confirm
no PHASE4 change touched `rg::RenderGraphPassMetadata`/`BuildRenderGraphMetadata()`
in a way that changes this phase's own starting assumptions.

## Step 1: The Goal (Where are we going?)

The "Render Graph" panel must never again present two DIFFERENT,
unexplained row-counts for the exact same pass name across its own two
sections. Concretely: for a `ProviderScope::PerActiveView` pass like
`"RenderOpaque"`/`"DrawSkyBackground"` that legitimately runs once per
active view, the panel's live pass table must show this as **one row**
that is CLEARLY annotated with which view(s) actually contributed it this
frame and the SUMMED stats across those views — matching, one-to-one, the
exact same cardinality (one row per unique name) the "Disabled Built-In
Passes" section already has. A user must never again have to wonder "why
are there two RenderOpaque rows here but only one there".

## Step 2: The Situation (Where are we now?)

Re-read `PHASE0_MASTER_STRATEGY.md`'s Step 2.1 in full. Summary:
- `RenderPipeline::DeclareOnePhase()` (`src/Renderer/RenderGraph/RenderPipeline.h`
  ~line 548-601) genuinely, correctly, by design, produces one
  `RenderPassDesc` (and therefore one real `RenderGraphPassSnapshot` entry)
  per active view for a `PerActiveView` provider — this is NOT the bug and
  must not be changed; two views genuinely both run `"RenderOpaque"` this
  frame, and the toggle registry correctly, consistently gates BOTH by the
  shared name.
- `src/Editor/Panels/RenderGraphPanel.cpp`'s `BuildPassTable()`/`BuildPassRow()`
  (~line 51-170) renders one row per raw snapshot entry (2 rows for a
  2-view pass) — its own doc comment (line 53-63) already documents this as
  deliberate, but ALSO acknowledges (line 76-81's own tooltip text) that a
  user cannot easily tell that BOTH rows share one toggle.
- `BuildDisabledBuiltInPassesSection()` (~line 355-387) reads
  `RenderPassToggleRegistry::ListAll()` (`RenderPassToggleRegistry.h` ~line
  99, backed by `std::unordered_map<std::string, ...>`) — inherently one
  entry per unique name.
- These two sections currently have NO shared, common data representation
  — fixing this requires a real data-model change in the layer that
  BUILDS the panel's own input (`rg::BuildRenderGraphMetadata()`/
  `rg::RenderGraphPassMetadata`, which live in
  `src/Renderer/RenderGraph/RenderGraphMetadata.h`/`.cpp` — confirmed by
  direct reading, NOT `RenderGraphSnapshotFormatting.h`/`.cpp`, which only
  holds smaller formatting helpers like `ToString(ViewScope)`/
  `FormatGpuTiming()` that `RenderGraphMetadata.cpp` itself calls into — do
  not go looking for the struct/function itself in that file), not merely a
  cosmetic ImGui-layer tweak.

## Step 3: The Plan (detailed strategy)

### 3.1 — Locate and understand the current metadata layer

Before writing any code, `search_in_dir` for `struct RenderGraphPassMetadata`
and `BuildRenderGraphMetadata(` to find their real, current definitions and
every field `RenderGraphPanel.cpp`'s `BuildPassRow()` currently reads
(`pass.name`, `pass.isCulled`, `pass.drawCallCount`, `pass.triangleCount`,
`pass.gpuTimingText`, `pass.reads`, `pass.writes` — confirmed from the code
already read during PHASE0's own investigation, but re-confirm the exact
field list and types live, since this phase's own new grouped struct must
be able to losslessly represent everything the CURRENT ungrouped one does,
for at least one representative view, plus the new cross-view annotation).

### 3.2 — Design the new, GROUPED pass-row representation

Add a new, pure, Tier-1-testable grouping function — e.g.
`rg::GroupPassMetadataByName(const std::vector<RenderGraphPassMetadata>& ungrouped)
-> std::vector<RenderGraphGroupedPassMetadata>` — living alongside the
existing `src/Renderer/RenderGraph/RenderGraphMetadata.h`/`.cpp` (the real,
confirmed home of `RenderGraphPassMetadata`/`BuildRenderGraphMetadata()` —
see Step 2 above; NOT `RenderGraphSnapshotFormatting.h`/`.cpp`) or a new
sibling header,
whichever the existing file's own size/scope makes more appropriate; use
judgment matching this codebase's own file-organization conventions seen
throughout PHASE0's investigation). `RenderGraphGroupedPassMetadata` needs
at minimum:
- `name` (the shared pass name — the grouping key).
- `viewLabels` — e.g. `std::vector<std::string>` or a small fixed
  enum/bitmask if this codebase's own `ViewScope`/view-identity vocabulary
  already has a clean "which named views" representation reusable here
  (check `rg::ViewScope`/`RenderViewId` first before inventing a new
  string-based one) — "Game", "Scene", or both, reflecting EXACTLY which
  view(s) actually contributed a real, non-culled instance this frame.
- Summed `drawCallCount`/`triangleCount` across every instance sharing this
  name this frame.
- `gpuTimingText` — decide via direct code reading (or `ask_questions` if
  genuinely ambiguous) whether to show a per-view breakdown (e.g. "Game:
  0.12ms, Scene: 0.08ms") or a single summed/maximum value — a per-view
  breakdown is almost certainly the MORE honest choice (GPU timing is not
  meaningfully additive across two logically-separate view passes the way
  draw/triangle counts are) — lean toward per-view breakdown unless a
  concrete technical blocker is found.
- Combined, de-duplicated `reads`/`writes` resource name lists (or, if a
  per-view breakdown is clearly more informative and the existing
  `rg::JoinNames()` helper already handles a list gracefully, keep them
  per-view-annotated too — use judgment, matching whichever presentation is
  LEAST likely to look like a NEW inconsistency).
- `isCulled` — true only if EVERY instance sharing this name was culled
  this frame (a pass genuinely running in Game View but culled in Scene
  View, or vice-versa, must not silently read as fully culled — this is
  exactly the kind of subtle honesty bug this whole campaign exists to stop
  introducing MORE of; get this right, test it explicitly).
- Every instance's own toggle-registry `enabled`/`everDeclaredThisSession`
  state is IDENTICAL by construction (same name, same registry lookup) —
  no per-instance divergence is possible here, confirm this with an
  explicit test rather than assuming it.

### 3.3 — Rework `RenderGraphPanel.cpp`'s pass table to consume the grouped representation

1. `BuildPassTable()` calls `rg::GroupPassMetadataByName(regime.passes)`
   once, then iterates the GROUPED list instead of `regime.passes` directly
   — `BuildPassRow()` is rewritten (or a new `BuildGroupedPassRow()`
   replaces it) to render exactly ONE row per group: the shared "Enabled"
   checkbox (unchanged mutation logic — `renderPassToggleRegistry.SetEnabled(pass.name, enabled)`,
   now genuinely representing "the one and only row for this name", not
   "one of several rows that happen to share state"), the view-label
   annotation (e.g. a small "(Game+Scene)"/"(Game only)"/"(Scene only)"
   text badge next to the pass name), and the summed/per-view stats
   columns from 3.2.
2. Keep the EXACT SAME `ScopedUniqueId` discipline
   `editor-core-separation-10`'s own PHASE2 already established (index-
   scoped, not name-scoped) — the grouped list still has one entry per
   unique name per regime, so a simple loop index remains perfectly safe
   and collision-free; no new ID-collision risk is introduced.
3. `BuildDisabledBuiltInPassesSection()` itself needs NO change — it
   already has the correct, one-row-per-name cardinality this phase is
   bringing the OTHER section into agreement with. Re-confirm this by
   reading it once more after 3.2/3.3's changes land, to be certain no
   incidental refactor accidentally touched it.
4. Update the panel's own tooltip text (currently at `BuildPassRow()`'s own
   ~line 76-81, warning about the "no per-view control" caveat) to match
   the NEW, now-unified single-row reality — the caveat itself
   ("toggling here affects every instance of this name at once") is still
   true and still worth stating, but the PREMISE ("if this same name
   appears in BOTH tables") no longer applies once this phase lands, since
   there is only ever one row now.

### 3.4 — Do not lose any information the old, ungrouped view provided

If any live user (or a future debugging session) genuinely needs to see
the RAW, per-view-instance breakdown (e.g. to debug exactly why Scene
View's own instance of a pass got culled while Game View's did not), do not
simply delete that capability. Add a lightweight way to reveal it without
reintroducing the confusing default — e.g. an ImGui tooltip on hover
showing the raw per-instance breakdown, or a collapsible "▸" row expansion,
whichever fits this codebase's own existing ImGui idioms (check how OTHER
expandable rows are already done in this codebase, e.g. the Frame Debugger
panel's own tree, before inventing a new interaction pattern) — use
`ask_questions` if genuinely unsure which the user would prefer.

### 3.5 — Tests

Add the new coverage to the existing, already-confirmed-correct
`tests/Renderer/RenderGraph/RenderGraphMetadataTests.cpp` (mirrors
`RenderGraphMetadata.h`/`.cpp`'s own real location — see Step 2/3.1 above;
this is the SAME file that already tests `BuildRenderGraphMetadata()` and
`RenderGraphPassMetadata`, so the new grouping function's tests belong right
alongside them, not in a new, disconnected file) — or, if you judge the new
grouping surface deserves its own dedicated file for size/clarity reasons,
name it `tests/Renderer/RenderGraph/RenderGraphGroupedPassMetadataTests.cpp`
(a sibling of `RenderGraphMetadataTests.cpp`, never anything referencing
"RenderGraphSnapshotFormatting", which is a different, smaller, unrelated
file). Covering: (a) two same-named instances (Game+Scene) both non-
culled group into one row with summed stats and a "both views" label; (b)
one instance culled, the other not, produces `isCulled == false` for the
GROUP with a label reflecting only the surviving view; (c) both culled
groups to `isCulled == true`; (d) a name appearing in only one view groups
correctly with a single-view label; (e) grouping is stable/deterministic
regardless of input order (mirrors `RenderPassToggleRegistry::ListAll()`'s
own "sorted, deterministic iteration order" discipline — the panel's own
row order should not visibly jitter frame-to-frame for no reason).

### 3.6 — Live, HTTP-driven verification (mandatory)

1. `GET /render_graph` / `/render_graph/passes` with both Game View and
   Scene View panels visible — confirm the JSON (if this endpoint reflects
   the grouped shape once wired through) or, at minimum, a fresh
   `gte_send_request` screenshot of the "Render Graph" panel itself (the
   panel is an ImGui window inside the Editor — capture via
   `GET /get_swapchain`/whatever endpoint captures the Editor's own UI, not
   just the Game View sub-texture) shows exactly ONE `"RenderOpaque"` row
   and one `"DrawSkyBackground"` row, each annotated "(Game+Scene)".
2. Hide the Scene View panel (or otherwise make only Game View active) and
   confirm the SAME pass names now show a "(Game only)" annotation with
   correspondingly smaller summed stats.
3. Toggle `"RenderOpaque"` off and confirm it now appears ONCE (not twice,
   not zero-and-confusingly-different) in "Disabled Built-In Passes",
   matching the exact same name the main table used to show — the
   cardinality mismatch from the original bug report must be gone.
4. Confirm `GET /get_logs?category=RenderPassHonesty` (and any new
   detector from PHASE6, once it exists — re-run this check again after
   PHASE6 lands too) stays empty throughout — this phase is a pure
   presentation/data-model fix with no execution-order or toggle-logic
   change, so it must not perturb the existing detector's own silence.

### 3.7 — End of phase

1. Incremental build succeeds.
2. New/extended Tier-1 tests pass (targeted filter).
3. Write `PHASE5_COMPLETION_REPORT.md` with the exact diff summary, the
   live-verification evidence from 3.6, and any `ask_questions` interaction
   (expect at least one, per 3.2's `gpuTimingText` decision and 3.4's
   expansion-UX decision, unless a clearly superior option was found by
   direct code reading alone).
4. `git_add` + `git_commit` covering every file changed and the report.
