# Physics debug scenario runner
> Runs the physics scenarios on a live editor and checks the numeric gates. For anyone who changes chain physics (skirt, hair, clothes).

Route reference and metric words: [docs/debugging/physics-debug.md](../../docs/debugging/physics-debug.md).

## Run
1. Start the editor: `build\GreatTamanaEditor.exe --no-vsync`. Only one editor can hold port 8080.
2. Run the script from the repo root:

```powershell
powershell -NoProfile -File tools\physics_debug\run_scenario.ps1 `
  -Model "C:\...\build\Project\Furina.gta" `
  -Clip  "C:\...\build\Project\ChatanyaraKuushanku_bassui260717a.gta" `
  -OutDir "C:\...\GreatTamanaEngin-Ideas\fix-7\evidence\2026-11-02\final" `
  -Preset default `
  -Baseline "C:\...\GreatTamanaEngin-Ideas\fix-7\evidence\2026-11-02\baseline"
```

| Parameter | Meaning |
|---|---|
| `-Model` | Absolute path of the model `.gta`. |
| `-Clip` | Absolute path of the animation `.gta`. |
| `-OutDir` | Folder for the replies and pictures. Keep it outside the repo. |
| `-Preset` | `default` (finished behavior) or `legacy` (old behavior). |
| `-Thresholds` | Gate file. Default is `thresholds.json` next to the script. |
| `-BaseUrl` | Default `http://127.0.0.1:8080`. |
| `-Baseline` | Folder with baseline replies (see below). Without it the relative gates FAIL. |
| `-GateIds` | Only these gate ids, for example `E1,E2`. |

The script spawns the model, runs the scenarios, writes `<scenario>_<preset>.json` and the pictures `<scenario>_<preset>_negz.png` and `<scenario>_<preset>_posx.png` into `-OutDir`, prints one line per gate, then restores the `default` preset and deletes the entity. Exit code 0 means every gate passed. Exit code 1 means a gate failed. Exit code 2 means a setup error.

Needs `curl.exe` (ships with Windows 10 and later) and Windows PowerShell 5.1 or PowerShell 7.

## What it runs
| Id | What |
|---|---|
| S4 | Idle bind pose, 600 frames at 60 Hz, 120 warmup. Runs before the clip is played. |
| S1 | Dance, 204 frames at 60 Hz. Runs 3 times when a gate asks for it (E7) or 2 times (E9). |
| S2, S3 | Dance at 144 Hz (489 frames) and 240 Hz (816 frames). |
| S5 | S1 with the `legacy` preset. |
| S6 | S1 with `collision_enabled=false`. |
| S7 | S1 with `collision_fit_enabled=false`. |

S5, S6, S7 run only when a gate names them. The pictures are live frames of the editor (clip playing) taken right after the run. The exact frames of a run are in `snapshots` when you ask for `snapshot_frames`.

## Gates
Gates are in `thresholds.json`. A gate whose JSON field does not exist counts as FAIL. It is never skipped.

| Id | Meaning |
|---|---|
| E1 | Limit adherence. `limit_violation_deg` p95 <= 3, p99 <= 6, max <= 12 for Q, HB1, HB2, HB3, HB4 and the two hair groups. |
| E2 | Weld. At least 10 welded root children. Their `dist_from_animated` mean <= 0.05 and p95 <= 0.12. |
| E3 | Honest switches. The `legacy` run (S5) stays within 2 percent of the baseline for `seg_dir_err_deg` p50 and p95 and `dist_from_animated` mean, for every group of the baseline file. |
| E4 | Render rate independence. `accel_local` mean ratio S2/S1 <= 1.30 and S3/S1 <= 1.50 for HB1, Q, HB2. |
| E5 | Collision fit. `bind_pose_pushed_joints` is 0 (fit on) and 23 (fit off, S7). Raw `bind_pose_penetrating_joints` stays 23. `pushed_particles_per_frame` <= 0.5 x baseline. The `seg_dir_err_deg` p50 gap between S6 and S1 is <= 3.0 (Q) and <= 1.5 (HB1). The 23 is a Furina number. |
| E6 | Stability. `nan_count` and `reseed_count` are 0 in S1 and S4. `abs_stretch` p99 per group <= max(1.1 x baseline p99, 0.10). S4 `dist_from_animated` max <= 6.0. |
| E7 | Performance. S1 `physics_ms.median` <= 2.5 x baseline. The script uses the median of the medians of 3 runs. Run the editor with `--no-vsync`. |
| E9 | Determinism. Two identical S1 runs are equal except `physics_ms`. |

E8 (damping and goal pull at 60 Hz and 120 Hz) is a unit test, not a gate here. The live `GET /profiler/scope?name=PhysicsSystem::Update` median of E7 is checked by hand.

### Gate fields
| Field | Meaning |
|---|---|
| `id`, `note` | Label and a short remark. |
| `scenario` or `scenarios` | One scenario, or `[A, B]` for the ops `ratio<=` (A / B) and `absdiff<=` (abs(A - B)). |
| `groups` | Group keys. `"*"` means every group of the baseline file (or of the result when there is no baseline). |
| `aggregate` | `all` or `welded_root_children` instead of a group. |
| `field` | Dotted JSON path, such as `collision.bind_pose_pushed_joints`. |
| `metric`, `stat` | Metric name and `n`, `mean`, `p50`, `p95`, `p99` or `max`. |
| `op` | `<=`, `>=`, `==`, `ratio<=`, `absdiff<=`, `within_rel`. |
| `value` | The limit. With `baseline` it is a factor. With `within_rel` it is a fraction (0.02 = 2 percent). |
| `baseline` | File stem inside `-Baseline`. The limit is `value x baseline value`. |
| `floor` | Lower bound of the limit. With `baseline` the limit is `max(value x baseline, floor)`. |
| `repeat` | Number of runs of the scenario. |
| `check` | `determinism` for E9. |

Group names are read from `thresholds.json` as UTF-8. The two hair groups are the PMX group keys `左F` and `右F` (the character for left and for right, then `F`). The script file itself is ASCII only. Keep `thresholds.json` saved as UTF-8 when you edit it.

## Baseline workflow
Relative gates (E3, E5, E6, E7) compare with replies taken before the fix.
1. If you still have a build of the old code, run the script there with `-Preset default` and keep the folder. That is the true baseline.
2. Otherwise run once with `-Preset legacy` on the current code. The `legacy` preset restores the old behavior, so those files are the closest baseline: S1 and S5 are written as `S1_legacy.json` and `S5_legacy.json`.
3. Pass that folder as `-Baseline`. File names must be `<scenario>_<preset>.json`.

A legacy baseline makes E3 almost trivial: it only proves that the `legacy` preset is deterministic. It does not prove that nothing else changed. Use the old-code baseline when you can.

Running with `-Preset legacy` is meant to produce baseline files. The fix gates (E1, E2, E4, E5) are expected to FAIL there. If a fix gate passes on the baseline, the gate is wrong.

## Evidence
Keep outputs outside the repo, for example `GreatTamanaEngin-Ideas\fix-7\evidence\<yyyy-mm-dd>\<step>\`. Never commit them.
