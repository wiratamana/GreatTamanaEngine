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
| `bench_culling.ps1` | Tables T1 (geometry only), T2 (full pipeline), T3 (Hi-Z wall), T4 (empty floor), T5 (`-IncludeHeavy`: 100000-instance grid, with and without a wall). Alternating A/B rounds (`-Repeats`, default 3, `-Samples` default 20). Prints per-view and per-feature pass sums, then PASS/FAIL for D1, V7, D3, D1b, D5, D6, D7, D2, D2b. |
| `soak_equality.ps1` | Hi-Z never removes a visible cluster: replays rest poses, a 2 degree/frame pan, 40 degree one-frame turns and moving entities in Hi-Z mode and in NoCull mode, `ClusterId` debug view, and compares the game view images. |
| `lib.ps1` | Shared HTTP helpers. Dot-source it. |

## What PASS means

- `bench_culling.ps1` exit codes: 0 all checks pass, 1 a check failed, 2 precondition failed, 3 a check could not be evaluated.
- A row is valid when at least 80% of its samples have `Present` status for the span and the primary view and no folded GPU record (`-MinValidPercent`). A check on an invalid row is `INVALID`, never `PASS`.
- Checks use same-run ratios. A check in ms (D1b, D3, D5) also fails when one round misses its limit by more than 2 ms. Absolute ms are orientation only.
- D3 uses `-HizBudgetMs` (default 0.4). D7 compares against a run with every feature on in both views. That emulates the per-view switch only; pass `-BaselineSpanMs` from an old build for the full check.
- `soak_equality.ps1` exit codes: 0 every frame equal (0 differing pixels), 1 some pixel differs, 2 precondition failed, Hi-Z was not `Applied`, or a capture failed. The number of differing pixels is always printed. Images are written under `-OutDir` (default `%TEMP%\culling_soak`); equal frames are deleted unless `-KeepAllImages`.
- The mesh must have no coincident triangles for byte equality. That check is not exposed over HTTP; do it offline when a pixel differs.

## Notes

- The scripts restore the feature states (global and per view), the `GpuDrivenScene` settings and the camera pose. They delete the entities they created (sun, walls, terrain).
- The instance grid cannot be bulk-deleted over HTTP. Close the editor without saving after `-IncludeHeavy` or `-Scene grid`. The soak test moves grid instances 0..2.
- The camera far plane cannot be set over HTTP. The default is assumed.
- `-Terrain` defaults to `build\Project\terrain_clustered.gta` under the repo. `-SkipLoad` uses the scene as it is (T4 is skipped).
