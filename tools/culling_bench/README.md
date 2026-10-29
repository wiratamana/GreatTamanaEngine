# Culling bench

PowerShell 5.1 scripts that drive a running editor over HTTP (default `http://127.0.0.1:8080`).
They only send requests. Nothing is saved by the editor. Close the editor without saving afterwards.

## Before you run

1. Start the editor with `--no-vsync`. With vsync every frame snaps to 16.7 ms steps. Compare milliseconds, never FPS.
2. Make the Game tab visible (the scripts call `GET /split_views?enabled=true`). Close the Frame Debugger window.
3. Do not resize the window during a run.

## Scripts

| Script | What it checks |
| --- | --- |
| `precheck.ps1` | Preconditions: `GpuDrivenScene` supported, `game.rendered`, `game.inertReason = none`, `game.claimedEntities >= 1`, `gpu_folded_passes = 0`, Frame Debugger closed. Warns when `present_policy` is `capped`. Exit 0 = ok, 1 = failed, 2 = editor not reachable. |
| `bench_culling.ps1` | Tables T1 (geometry only), T2 (full pipeline), T3 (Hi-Z wall), T7 (camera looks away), T4 (empty floor), T5 (`-IncludeHeavy`: 100000-instance grid, with and without a wall; also T7h). Alternating A/B rounds (`-Repeats`, default 3, `-Samples` default 20). Prints per-view and per-feature pass sums, then PASS/FAIL for D1, V7, D3, D1b, D5, D6, D7, D2, D2b, A1-A4 (T7), Ah1-Ah4 (T7h). |
| `hiz_demo.ps1` | Table T6: opens the Hi-Z demo scene (`POST /open_preset_scene`) and measures profile (`geometry_only`, `full`, `both`) x wall (off, on) x mode (NoCull, Frustum, HiZ). Columns: span, primary, secondary, GpuDrivenScene primary and secondary ms, clusters tested and drawn, FPS (only when `present_policy` is `uncapped` and the verdict is not present limited, else `n/a: ...`), valid samples. Prints computed verdict lines and two checks per profile. Parameters: `-Count` (50000), `-Spacing`, `-Profile`, `-Samples`, `-Repeats`, `-WallRatio` (0.25), `-HizBudgetMs` (3.0), `-OutFile` (writes `<name>.md` and `<name>.json`). It waits until the fixed-cost features read off. Cleanup closes the demo through the engine (`POST /close_preset_scene`), which restores the features and settings. |
| `soak_equality.ps1` | Hi-Z never removes a visible cluster: replays rest poses, a 2 degree/frame pan, 40 degree one-frame turns, an away-and-back turn (`-AwayFrames` frames with nothing in view, then one frame back, twice) and moving entities in Hi-Z mode and in NoCull mode, `ClusterId` debug view, and compares the game view images. |
| `lib.ps1` | Shared HTTP helpers and the table and verdict builders of `hiz_demo.ps1` (`Format-HizDemoTable`, `Get-HizDemoVerdicts`). Dot-source it. |

## What PASS means

- `bench_culling.ps1` exit codes: 0 all checks pass, 1 a check failed, 2 precondition failed, 3 a check could not be evaluated.
- A row is valid when at least 80% of its samples have `Present` status for the span and the primary view and no folded GPU record (`-MinValidPercent`). A check on an invalid row is `INVALID`, never `PASS`.
- Checks use same-run ratios. A check in ms (D1b, D3, D5) also fails when one round misses its limit by more than 2 ms. Absolute ms are orientation only.
- D3 (`bench_culling.ps1`) uses `-HizBudgetMs` (default 0.4). D7 compares against a run with every feature on in both views. That emulates the per-view switch only, so without `-BaselineSpanMs` (from an old build) it prints `PARTIAL` and the run exits 3. It never prints FAIL.
- `soak_equality.ps1` captures NoCull twice per pose; the difference of those two is the noise floor (cluster emit order is not deterministic). A frame FAILS when the Hi-Z difference is above `max(8, 4 x noise)` pixels, or when the differing pixels form a connected block of 16 or more pixels. Hi-Z diff, noise and largest block are always printed.
- `soak_equality.ps1` exit codes: 0 every scenario ran and no frame failed, 1 a frame failed, 2 precondition failed, no frame of a scenario read occlusion `Applied` in Hi-Z mode, a capture failed or a scenario (for example the movers) was skipped. Poses with nothing in view are fine. The away pose is per scene (the terrain camera moves to Y 4000 and pitches down, because Y 900 is inside the mesh box). Images of failing frames are written under `-OutDir` (default `%TEMP%\culling_soak`); the others are deleted unless `-KeepAllImages`.
- The mesh must have no coincident triangles for byte equality. That check is not exposed over HTTP; do it offline when a pixel differs.
- `hiz_demo.ps1` exit codes: 0 all checks pass, 1 a check failed, 2 precondition failed or editor unreachable, 3 a check could not be evaluated. Checks: wall on, Hi-Z GpuDrivenScene primary ms <= `-WallRatio` (0.25) x Frustum; wall off, Hi-Z <= Frustum + `-HizBudgetMs` (3.0). A row below `-MinValidPercent` valid samples, or Hi-Z not `Applied`, makes the check `INVALID`.
- The demo limits are looser than D3 (0.4 ms) on purpose. Hi-Z runs a second full cull pass over all work items (two-phase cull), so it adds ms when nothing is hidden. Only an indirect-dispatch cull ("Level 2", not built) would remove that second pass. Measured with `geometry_only`, 3 rounds x 20 samples, GpuDrivenScene primary GPU time:

  | Instances | Wall | Hi-Z | Frustum | Result |
  | --- | --- | --- | --- | --- |
  | 50000 | on | 14.18 ms | 125.0 ms | 11.3 percent of Frustum |
  | 50000 | off | 88.84 ms | 87.06 ms | +1.78 ms (stats on, 1.1M work items, two cull phases) |
  | 20000 | on | 6.39 ms | 34.23 ms | 18.7 percent of Frustum |
  | 20000 | off | 35.52 ms | 33.28 ms | +2.24 ms |

- The demo run keeps stats on (3 extra passes per part, about 0.01 ms) so that tested and drawn clusters are read in the same run. Compare the `geometry_only` and `full` rows: fixed-cost features dominate the `full` frame.
- T7 (and T7h with the grid): the camera looks away from the scene. A1 `game.skippedParts >= 1`, A2 tested clusters = 0, A3 no GpuDrivenScene pass in the primary view (sum 0 ms), A4 the facing view skips nothing. The script finds the away pose by measurement: zero visible clusters and `skippedParts >= 1`, trying pitch +80, pitch -80, yaw 180 at the facing position, then at raised positions (Y 4000, Y 20000). A camera inside the part box cannot skip it (the terrain at Y 900), so the raised position is needed there. No pose found makes the check `INVALID`.
- The fixed-cost feature list (name prefixes) is owned by the engine: `GET /hiz_demo/geometry_only_features`. `bench_culling.ps1`, `soak_equality.ps1` and `hiz_demo.ps1` read it from there; there is no copy in `tools`.

## Notes

- The scripts restore the feature states (global and per view) and the `GpuDrivenScene` settings. `bench_culling.ps1` and `soak_equality.ps1` also restore the camera pose and delete the entities they created (sun, walls, terrain). `hiz_demo.ps1` replaces the scene, so the old camera is gone; it closes the demo through the engine, which restores the settings it changed.
- The instance grid cannot be bulk-deleted over HTTP. Close the editor without saving after `-IncludeHeavy` or `-Scene grid`. The soak test moves grid instances 0..2.
- Buffers of a feature turned off are not released until it is enabled again (see `docs\conventions\gpu-driven-clusters.md`, "Buffer memory"). After a `full` profile run, restart the editor before `-IncludeHeavy`, or T5w D2b can read `INVALID` (the 100000 instance Hi-Z chain is refused over the process budget).
- The camera far plane cannot be set over HTTP. The default is assumed.
- `-Terrain` defaults to `build\Project\terrain_clustered.gta` under the repo. `-SkipLoad` uses the scene as it is (T4 is skipped).
