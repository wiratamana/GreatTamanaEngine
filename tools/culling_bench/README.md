# Culling bench
> PowerShell 5.1 scripts that benchmark GPU-driven culling on a running editor, over HTTP. They send requests only. The editor saves nothing.

Default URL: `http://127.0.0.1:8080`. Close the editor without saving when you finish.

## Before you run

1. Start the editor with `--no-vsync`. Compare milliseconds, never FPS.
2. Make the Game tab visible. The scripts call `GET /split_views?enabled=true`.
3. Close the Frame Debugger. Do not resize the window during a run.

## Scripts

| Script | What it does |
|---|---|
| `precheck.ps1` | Checks preconditions. `GpuDrivenScene` supported, `game.rendered`, `game.inertReason = none`, `game.claimedEntities >= 1`, `gpu_folded_passes = 0`, Frame Debugger closed. Warns when `present_policy` is not `uncapped`. |
| `bench_culling.ps1` | Tables T1 geometry only, T2 full pipeline, T3 Hi-Z wall, T7 camera looks away, T4 empty floor. `-IncludeHeavy` adds T5 (100000-instance grid) and T7h. Alternating A/B rounds. Prints PASS or FAIL per check. |
| `hiz_demo.ps1` | Table T6. Opens the Hi-Z demo scene (`POST /open_preset_scene`). Measures profile x wall x mode (NoCull, Frustum, HiZ). Writes `<OutFile>.md` and `.json`. Closes the demo with `POST /close_preset_scene`. |
| `soak_equality.ps1` | Checks that Hi-Z never removes a visible cluster. Replays rest poses, pans, one-frame turns, away-and-back turns and moving entities. Compares Hi-Z against NoCull game-view images. |
| `lib.ps1` | Shared HTTP helpers and table builders. Dot-source it. |

## Main parameters

| Script | Parameters (default) |
|---|---|
| `bench_culling.ps1` | `-Samples` (20), `-Repeats` (3), `-MinValidPercent` (80), `-HizBudgetMs` (0.4), `-BaselineSpanMs`, `-GridCount` (100000), `-Terrain`, `-SkipLoad`, `-OutFile` |
| `hiz_demo.ps1` | `-Count` (50000), `-Spacing`, `-Profile` (`geometry_only`, `full`, `both`), `-Samples`, `-Repeats`, `-WallRatio` (0.25), `-HizBudgetMs` (3.0) |
| `soak_equality.ps1` | `-Seed`, `-Scene` (`terrain`, `grid`, `both`), `-AwayFrames` (30), `-OutDir` (`%TEMP%\culling_soak`), `-KeepAllImages`, `-MoverEntities` |

`-Terrain` defaults to `build\Project\terrain_clustered.gta`. `-SkipLoad` uses the scene as it is, and T4 is skipped.

## Exit codes

| Code | `bench_culling`, `hiz_demo` | `soak_equality` |
|---|---|---|
| 0 | all checks pass | every scenario ran, no frame failed |
| 1 | a check failed | a frame failed |
| 2 | precondition failed or editor not reachable | precondition failed, no `Applied` Hi-Z frame, capture failed, or a scenario was skipped |
| 3 | a check could not be evaluated | - |

`precheck.ps1`: 0 ok, 1 failed, 2 editor not reachable.

## How to read the result

- A row is valid when 80% of samples have `Present` status and no folded GPU record. A check on an invalid row prints `INVALID`, never `PASS`.
- Checks use same-run ratios. Absolute ms are for orientation.
- Hi-Z runs a second cull pass over all work items. So it can add ms when nothing is hidden. The demo limit (3.0 ms) is looser than D3 (0.4 ms) for this reason.
- Without `-BaselineSpanMs`, D7 prints `PARTIAL` and the run exits 3. It never prints FAIL.
- `soak_equality` captures NoCull twice per pose. The difference is the noise floor. A frame fails when the Hi-Z difference is above `max(8, 4 x noise)` pixels, or a block of 16 or more connected pixels differs.
- T7 finds the "look away" pose by measurement: zero visible clusters and `skippedParts >= 1`. No pose found means `INVALID`.
- The list of fixed-cost features comes from the engine: `GET /hiz_demo/geometry_only_features`.

## Cleanup and limits

- The scripts restore feature states and `GpuDrivenScene` settings. `bench_culling` and `soak_equality` also restore the camera and delete the entities they made.
- `hiz_demo` replaces the scene. The old camera is gone.
- No route bulk-deletes the instance grid. Restart the editor after `-IncludeHeavy` or `-Scene grid`.
- A disabled feature keeps its buffers until it is enabled again. After a `full` run, restart the editor before `-IncludeHeavy`. See [GPU-driven rendering](../../docs/architecture/gpu-driven-rendering.md).
- The camera far plane cannot be set over HTTP.
- Byte equality needs a mesh with no coincident triangles. That check is not exposed over HTTP.
