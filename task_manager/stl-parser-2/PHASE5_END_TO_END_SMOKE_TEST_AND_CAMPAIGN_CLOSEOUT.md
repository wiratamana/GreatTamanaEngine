# PHASE5_END_TO_END_SMOKE_TEST_AND_CAMPAIGN_CLOSEOUT.md

**Campaign:** `stl-parser-2` (parent: `PHASE0_MASTER_STRATEGY.md` — READ FIRST)
**Branch:** `feature/stl-parser-impl`
**Depends on:** `PHASE1`, `PHASE2`, `PHASE3`, `PHASE4` (both endpoints must
be fully implemented, tested, and merged into this same branch already).

## Step 1: The Goal (Where are we going?)

Prove, with a REAL running engine and REAL HTTP calls (no mocks, no
stand-ins), that this entire campaign's stated purpose actually works: import
the real reference asset `terrain.stl` (living OUTSIDE "Project", under the
gitignored `_reference/` folder) via `POST /import_asset`, instantiate the
resulting `*.gta` via `POST /instantiate_asset`, and visually confirm (via
this engine's existing capture endpoints) that a real, rendered terrain mesh
now sits in the Scene — entirely over HTTP, with zero mouse/keyboard
interaction. Then run the full regression suite one final time and write
this campaign's closing report.

This phase is explicitly a MANUAL/LIVE verification pass, not a new
permanent automated test — mirroring `stl-parser-1`'s own
`PHASE3_COMPLETION_REPORT.md` precedent ("Ran the headless `--reimport` CLI
against the real reference asset... Then launched the built
`GreatTamanaEngine.exe` in the background and confirmed via `GET
/get_swapchain`..."). No new source files are expected in this phase unless
this phase's own manual run surfaces a genuine bug in `PHASE1`–`PHASE4`'s
work (in which case, fix it here, in the affected phase's own files, and
say so plainly in this phase's completion report).

## Step 2: The Situation (Where are we now?)

- After `PHASE1`–`PHASE4`, both endpoints are implemented and covered by
  fast, synthetic-fixture automated tests — but NEITHER endpoint has ever
  actually been exercised against the real, large, real-world
  `terrain.stl` this whole campaign exists for.
- `gte_send_request` (this delegated task's own available tool) can POST to
  `/import_asset`/`/instantiate_asset` and can capture `/get_swapchain`/
  `/get_game_view` as real, viewable images — everything needed for this
  phase's own verification is already available with no new tooling.
- `run_app_background`/`stop_app_background` (this delegated task's own
  available tools) are how the engine itself gets launched/torn down for
  this manual session — non-blocking, so HTTP requests can be sent to it
  while it keeps running.

## Step 3: The Plan

### 3.1 — Full regression build + test pass FIRST

Before touching the network at all: `cmake --build build` (both
`GreatTamanaEngine` and `GreatTamanaEngineTests` targets), then `cd /d
C:\Users\F5954\Documents\TAMANA\GreatTamanaEngine\build && ctest -C Debug
--output-on-failure`. Confirm 100% pass (aside from any PRE-EXISTING,
documented skip — e.g. `PmxLoaderRealModelSmokeTest`'s own
machine-dependent skip, already noted in `stl-parser-1`'s completion
reports) before proceeding. Any genuine new failure must be fixed (in the
relevant earlier phase's own files) before continuing.

### 3.2 — Confirm the reference asset is actually present

Verify `C:\Users\F5954\Documents\TAMANA\GreatTamanaEngine\_reference\pl-sky\assets\terrain.stl`
exists and is the expected ~52MB/1,045,458-triangle binary STL (same file
`stl-parser-1` already used — see that campaign's own
`PHASE0_MASTER_STRATEGY.md` for exactly how its facts were derived, e.g.
reading the triangle count at byte offset 80). If this file is missing on
this machine (it is gitignored, fetched by a separate CMake step — see
`PHASE0_MASTER_STRATEGY.md`'s own note that this is a "downloaded reference
project"), this phase's own completion report must say so honestly and this
manual step is skipped — exactly like `stl-parser-1`'s own precedent for a
missing real-fixture smoke test. Do not fabricate or fake this step's
results if the file is absent.

### 3.3 — Launch the engine

Build (if not already built by 3.1) and launch
`GreatTamanaEngine.exe` via `run_app_background`, with
`GTE_ENABLE_EDITOR=ON`/`GTE_ENABLE_PROJECT_PANEL=ON`/`GTE_ENABLE_NETWORK=ON`
(confirm these are the active build's actual configuration — e.g. via
`build/CMakeCache.txt`, same check `stl-parser-1`'s own `PHASE3_COMPLETION_REPORT.md`
already performed — before assuming `/import_asset` will report
`projectAvailable == true`). Give it a moment to fully start (e.g. an
initial `GET /get_swapchain` poll/retry) before sending the real requests
below.

### 3.4 — `POST /import_asset` against the real `terrain.stl`

Send (via `gte_send_request`, using its `payload` parameter for the POST
body):

```
POST /import_asset
{"source_path": "C:\\Users\\F5954\\Documents\\TAMANA\\GreatTamanaEngine\\_reference\\pl-sky\\assets\\terrain.stl",
 "destination_folder": ""}
```

**Expect this call to take a genuinely noticeable amount of time** (see
`PHASE0`'s Locked Design Decision 1/6 — the main thread is synchronously
parsing a 52MB/1,045,458-triangle file; the engine's rendering will visibly
stutter/freeze for the duration). This is EXPECTED, not a bug. Confirm the
response is HTTP 200 with `"success":true`, `"converted_to_mesh_asset":true`,
`"mesh_source_format":"stl"`, `"mesh_vertex_count":3136374`,
`"mesh_triangle_count":1045458` (matching `stl-parser-1`'s own already-measured
facts exactly), and record the response's own `"final_absolute_path"` value
— that is the exact string the next step needs.

If `gte_send_request`'s own default timeout is shorter than what this
request actually needs, retry with a larger explicit `timeout_seconds`
argument (it accepts one) rather than concluding the endpoint is broken —
this specific call is EXPECTED to be slow, per Locked Design Decision 1.

### 3.5 — `POST /instantiate_asset` with the path from 3.4

```
POST /instantiate_asset
{"gta_path": "<final_absolute_path from 3.4's own response>"}
```

Confirm HTTP 200, a real `entity.index`/`entity.generation`, and `"name":
"terrain"` (the file's own stem, per `PHASE3`'s bare-bones, unchanged
`CreateMeshEntityFromGtaFile()` naming behavior).

**This call may ALSO take a genuinely noticeable amount of time**, unlike a
typical `/instantiate_asset` call against a small mesh — `PHASE4`'s own doc
explicitly flags that this specific route still uses `EngineCommandBridge`'s
UNCHANGED 3000ms default timeout (deliberately not pre-enlarged, since
nothing had yet observed a real timeout at the time `PHASE4` was written),
and uploading `terrain.stl`'s real 3,136,374 vertices to the GPU for the
first time is genuine, non-trivial work. If this call comes back HTTP `504`
(a real bridge timeout, not a crash), that is exactly the scenario `PHASE4`
anticipated and asked THIS phase to notice and report — do not treat a `504`
here as an unrelated failure: (1) retry `gte_send_request` with a larger
explicit `timeout_seconds` first (the HTTP client's own wait, not the
bridge's), and (2) if the response body itself confirms the ENGINE's own
bridge timed out (not just the HTTP client), that is a genuine, real finding
worth fixing — per `PHASE4`'s own guidance, enlarge `EngineCommandBridge`'s
default (or pass an explicit longer timeout directly at the
`/instantiate_asset` route's own `SubmitAndWait()` call site) and document
this as a deviation in this phase's own completion report, exactly like any
other genuine bug found and fixed during this manual verification pass.

### 3.6 — Visual confirmation

Call `gte_send_request` against `/get_swapchain` (and/or `/get_game_view`)
and actually LOOK at the returned image (this tool returns a real, viewable
image). Confirm a real, rendered mesh (the terrain's geometry — a large,
irregular, presumably grey/flat-shaded surface, matching
`stl-parser-1`'s own "flat grey clay" untextured-material rendering path,
since `terrain.stl` carries no materials) is now visibly present. It may not
be perfectly framed in view (the entity spawns at the world origin — Locked
Design Decision 4 — and the default Scene/Game camera may or may not be
looking at the origin) — if nothing is visible, do not treat this alone as
a failure; first try orbiting/checking via a wider capture, or spawning a
primitive (`POST /instantiate_primitive`) at the origin as a sanity
landmark, before concluding something is actually wrong. If, after
reasonable effort, nothing resembling a terrain mesh is visible at all
(zero triangles, an obvious rendering error, an engine crash, ...), that IS
a real bug — diagnose and fix it in the relevant earlier phase's own files,
then re-run this whole section.

### 3.7 — Clean up

`stop_app_background` the launched engine process. Do not leave it running
after this phase completes.

### 3.8 — Write the campaign completion report

New file `task_manager/stl-parser-2/CAMPAIGN_COMPLETION_REPORT.md` (mirroring
`stl-parser-1`'s own `PHASE3_COMPLETION_REPORT.md` structure/tone): a
Summary section, a "Files added"/"Files changed" section aggregating EVERY
file touched across `PHASE1`–`PHASE5` (not just this phase's own — this is
the one, final, whole-campaign report), a "Deviations from the strategy
docs" section (name any — there is no shame in a deviation as long as it's
disclosed, mirroring `stl-parser-1`'s own "None." precedent when nothing
deviated), a "Build & test results" section (the full `ctest` output
summary from 3.1), a "Manual sanity check" section (the full
import→instantiate→screenshot walkthrough from 3.2–3.7, including the
actual measured response JSON bodies and an honest note if the reference
file was missing on this machine), and a Definition-of-done checklist
copied from `PHASE0_MASTER_STRATEGY.md`'s own "Definition Of Done" section
with each item checked off.

## Definition of Done (this phase's slice, and the whole campaign)

- Full regression suite passes with zero new failures.
- The real `terrain.stl` was successfully imported via `POST /import_asset`
  (or the file's absence on this machine was honestly documented instead of
  faked).
- The resulting `*.gta` was successfully instantiated via `POST
  /instantiate_asset`.
- A real rendered terrain mesh was visually confirmed via `GET
  /get_swapchain`/`GET /get_game_view` (or, if genuinely not visible after
  reasonable troubleshooting, a real bug was found, fixed, and re-verified).
- The launched engine process was cleanly stopped afterward.
- `task_manager/stl-parser-2/CAMPAIGN_COMPLETION_REPORT.md` exists and
  accurately reflects everything `PHASE1`–`PHASE5` actually did.
- Every source/test change plus this report are committed to git on
  `feature/stl-parser-impl` (branch unchanged throughout this entire
  campaign, per this doc's own header).
