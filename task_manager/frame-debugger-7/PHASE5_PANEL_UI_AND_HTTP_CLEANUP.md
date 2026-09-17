# PHASE5 — Panel UI polish and final HTTP/dead-code cleanup

_Parent: `PHASE0_MASTER_STRATEGY.md` — read it first. Builds on Phases 1-4._

## Step 1: The Goal

With Phases 1-4 done, the underlying mechanisms are correct. This phase
makes sure the surface (UI text/empty states, HTTP responses, leftover
identifiers) is clean, consistent, and fully matches the new behavior — no
stale comments, no half-removed code, no confusing leftover HTTP fields.

## Step 2: The Situation

Phases 1-4 touched a lot of surface area quickly, focused on correctness.
This phase is the "make sure nothing was left half-done" pass.

## Step 3: The Plan

### 3.1 — Full identifier sweep

Run `search_in_dir` (recursive, across `src/` and `tests/`) for each of the
following and resolve every hit (fix, delete, or confirm it's a comment
correctly describing history, never live code):

- `FrameDebuggerHistory` (should now only appear as the historical campaign
  name in comments/doc, and as the actual file name — the class itself was
  renamed in Phase 1).
- `step_history` / `StepFrameHistoryFromCommand` / `historyCount` /
  `historyCursor` (should not exist anywhere anymore).
- `computePassPreviews` / `CollectComputePassTextureWrites` /
  `CollectComputePassVolumeTextureWrites` / `ComputePassCopySource` /
  `HdrComputePassCopySource` (should not exist anywhere anymore — Phase 4
  removed these; this phase double-checks nothing was missed).
- `BuildFrameHistoryToolbarRow` (should not exist).

### 3.2 — `FrameDebuggerCommandBridge`/`NetworkRoutes` final pass

Confirm the `/frame_debugger/*` route family
(`src/Application/FrameDebuggerCommandBridge.h/.cpp`,
`src/Network/NetworkRoutes.h/.cpp`, `NetworkServer.cpp`) matches reality:
`open`/`enable`/`capture`/`select_event`/`set_channel`/`set_levels`/`state`
remain; `step_history` is gone. Update `docs/conventions/networking.md` if
it lists the `/frame_debugger/*` routes explicitly (grep it first — only
touch it if it actually enumerates them).

### 3.3 — Empty/placeholder state polish

In `Panels/FrameDebuggerPanel.cpp`, make sure there are exactly three,
clearly distinguishable messages, never conflated:

1. `"No frame captured yet."` — the event tree itself is empty (nothing
   captured this session, or cleared by Disable/Resume — Phase 1).
2. `"Nothing drawn yet at this point in the frame."` — a real, valid
   Pre-GameView compute leaf is selected (Phase 4's `NotYetDrawn`).
3. `"No event selected."` — the existing message for the details section
   when `m_selectedEventIndex == -1` (unchanged, still correct).

Also confirm the "Frame History" toolbar's removal (Phase 1) did not leave
a visually empty gap or an orphaned `ImGui::Separator()` — re-read
`Build()`'s current layout top-to-bottom and tidy spacing if needed.

### 3.4 — `FrameDebuggerStateSnapshotView`/HTTP `/state` response sanity

Confirm the JSON shape `GET /frame_debugger/state` now returns makes sense
end-to-end (no leftover `historyCount`/`historyCursor` fields, the new
`hasCapturedFrame`-style field from Phase 1 is present and correctly
reflects `FrameDebuggerCurrentCapture::HasCapture()`). Update
`tests/Network/NetworkRoutesTests.cpp` if any JSON-shape assertion still
expects the old fields.

## Step 4: Definition of Done

- Zero hits for any of the Step 3.1 dead identifiers (other than clearly
  historical comments).
- The three empty/placeholder states above are each independently
  reachable and readable in the UI.
- Quick compile check only. `PHASE5_COMPLETION_REPORT.md` written (list
  every dead-code hit found and how it was resolved), code committed.

Use `ask_questions` for any genuine ambiguity — and require the same from
any further delegation.
