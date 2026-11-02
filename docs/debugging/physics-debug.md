# Physics debug
> Measure, change and look at the dynamic bone chains (skirt, hair, clothes) of a model over HTTP. For AI agents and developers who tune or debug chain physics.
> Verified against code: 2026-11-02

## TL;DR
- Routes live under `/physics_debug/*`. They use one bridge (`PhysicsDebugCommandBridge`). A busy bridge gives `503`.
- `POST /physics_debug/run` runs a whole scenario headless in one call and returns statistics per joint group. It is deterministic.
- `GET /physics_debug/set` changes one or more settings while the editor runs. `GET /physics_debug/preset?name=legacy` switches every new behavior off again.
- `GET /physics_debug/image` draws the chains as a PNG: animated pose green, simulated pose red, colliders blue. No screenshot, no Game or Scene view needed.
- The live recorder (`/physics_debug/capture`) costs one bool test per frame when it is off.
- Settings are in memory. A restart resets them.
- Route shapes here come from the code and its design. They were not run live when this page was written.

## Words
| Word | Meaning |
|---|---|
| Chain | One tree of simulated bones hanging from a pinned anchor bone. |
| Joint | One simulated bone inside a chain. Not a PMX joint. |
| Animated pose | Where plain animation (no physics) puts a bone this frame. |
| Display position | What `ApplyDynamicChainPhysicsToPose` gets, so what the user sees. |
| Root child | A joint whose tree parent is the anchor bone. |
| Group key | Bone name with every trailing `_<digits>` run removed. `HB1_3_5` is `HB1`, `Q_5_2` is `Q`, `SZY_J1_2` is `SZY_J1`. If nothing is left, the full name. |
| Step fraction | Fraction removed per 1/60 s step. See [glossary](../glossary.md). |

## Routes
All answers are JSON `{"success":false,"error":"..."}` on failure. `/physics_debug/image` answers a PNG on success.
Query keys are strict: an unknown key gives `400` and names the key. `12abc` is rejected. Bools are `true` or `false`.

| Status | Meaning |
|---|---|
| `400` | Bad value, unknown key, out of range. |
| `404` | Unknown entity. |
| `409` | State conflict: entity has no physics chains, no animator, capture off, no pose yet. |
| `503` | Bridge missing or busy ("already in progress"). |
| `504` | Main thread too slow. The change may still apply. Read the state before you retry. |

Timeouts: 3 s for every route, 60 s for `POST /physics_debug/run`.

### `GET /physics_debug/list`
Every entity with a physics rig.
```json
{"entities":[{"name":"Furina","entity":{"index":3,"generation":1},"mesh_gta_path":"...","chains":3,"joints":260,
 "rig_enabled":true,"rig_frozen":false,
 "animator":{"present":true,"playing":true,"loop":true,"speed":1,"frame":12.5,"last_frame":105,"animation_gta_path":"..."}}]}
```
`last_frame` is `-1` when the clip is not cached.

### `GET /physics_debug/state?entity=N`
```json
{"success":true,"entity":"Furina","settings":{"gravity_y":-98},"time":{"paused":false,"frozen_this_frame":false,"frame_count":1234},
 "chains":[{"index":0,"anchor_bone":"...","anchor_bone_index":10,"joints":204,"root_children":25,"extra_constraints":144,
 "constraint_iterations":4,"collision_enabled":true,"max_plausible_root_delta":33.3,"initialized":true,"reseed_count":0}],
 "groups":[{"key":"HB1","chain":0,"joints":72}],"colliders":40,
 "detection":{"orphans":0,"cross_chain_joints_dropped":0,"duplicate_bodies_dropped":23,"bend_limit_missing":0},
 "capture":{"enabled":false,"window_frames":120,"frames_recorded":0}}
```

### `GET /physics_debug/set?<key>=<value>&...`
Keys of the settings table below. At least one key. All or nothing: one bad pair changes nothing. `rig_enabled` and `rig_frozen` also need `entity=N`.
Reply: `{"success":true,"settings":{...}}` (all settings after the change).

### `GET /physics_debug/preset?name=default|legacy`
Reply as `set`. Any other name: `400`.

### `POST /physics_debug/play`
Body `{"entity":"N","animation_gta_path":"<absolute path>","frame":0,"speed":1,"loop":true}`. `frame`, `speed`, `loop` are optional.
`frame` 0..100000. `speed` 0..100 (`0` holds a frame); a value outside the range gives `400`.
Reply `{"success":true,"entity":"N","animator":{...}}`. Refused with `400` for a dead entity, a model without skin data or a bad clip.

### `GET /physics_debug/seek?entity=N&frame=F[&reset_physics=true|false]`
`frame` 0..100000. Needs an animator (`409` otherwise). `reset_physics` default `true`. Reply `{"success":true,"animator":{...}}`.

### `GET /physics_debug/reset?entity=N`
Drops the simulation state of the rig. The next step seeds from the animated pose. Reply `{"success":true}`.

### `POST /physics_debug/run`
Body (only `entity`, `frames`, `render_hz` are required):
```json
{"entity":"N","frames":204,"render_hz":60,"start_frame":0,"warmup_frames":0,"restore":true,
 "settings":{"gravity_y":-98,"bend_limits_enabled":false},
 "trace":{"bones":["HB1_7_4"],"every":1},"snapshot_frames":[100]}
```
| Field | Range |
|---|---|
| `frames` | 1..3600 |
| `render_hz` | 10..480 |
| `start_frame` | 0..100000 |
| `warmup_frames` | less than `frames` |
| `frames` x joints of the entity | at most 2,000,000, else `400` |
| `settings` values | JSON numbers, bools or strings. Same parser as `set`. Temporary. |

Reply:
```json
{"success":true,"entity":"N","frames":204,"render_hz":60,"settings_used":{},"wrapped":false,"steps_histogram":[0,204,0,0],
 "groups":[{"key":"Q","chain":0,"joints":78,"dist_from_animated":STATS,"seg_dir_err_deg":STATS,"bend_delta_deg":STATS,
 "limit_violation_deg":STATS,"abs_stretch":STATS,"writeback_err":STATS,"accel_local":STATS}],
 "all":{"...same fields..."},
 "welded_root_children":{"joints":15,"dist_from_animated":STATS,"seg_dir_err_deg":STATS},
 "collision":{"pushed_particles_per_frame":0,"raw_pushed_particles_per_frame":0,"mean_push_distance":0,"bind_pose_penetrating_joints":23,"bind_pose_pushed_joints":0},
 "bend":{"clamped_joints_per_frame":0},"reseed_count":0,"nan_count":0,"physics_ms":{"median":0.4,"p95":0.6},
 "trace":[],"snapshots":[]}
```
`STATS` is `{"n","mean","p50","p95","p99","max"}`. A number that is not finite is written as `null`.
`welded_root_children` is `null` when the model has none. Two identical calls give the same JSON except the `physics_ms` values.

### `GET /physics_debug/capture?enabled=true|false[&window=120]`
Turns the live recorder on or off. `window` 1..600 frames. The window is clamped so that `window x joints <= 600000`.
Reply `{"success":true,"enabled":true,"window_frames":120,"frames_recorded":0}`.

### `GET /physics_debug/metrics?entity=N[&window=120][&group=KEY]`
Same body as `run` without `trace` and `snapshots`, from the recorded live frames. `409` when capture is off or fewer than 3 frames exist.

### `GET /physics_debug/joints?entity=N[&chain=I][&group=KEY][&first=0][&count=200]`
`count` 1..2000. One row per joint:
```json
{"success":true,"total":260,"first":0,"rows":[{"i":0,"chain":0,"bone":"HB1_0_1","bone_index":120,"group":"HB1","parent_joint":-1,"parent_bone":"...",
 "rest_length":0.37,"rigid_body":"...","mass":40,"damping":0.0165,"pmx_linear_damping":1,"stiffness":0.02,"collision_radius":0.2,
 "collision_fit_radius":0.2,"ignored_colliders":[],"group_bit":1,"collision_mask":"0xffbf","bend_limit_deg":0,
 "cumulative_bend_limit_deg":0,"effective_bend_limit_deg":0,"pmx_joint":"...","pmx_rot_lower_deg":[0,0,0],"pmx_rot_upper_deg":[0,0,0],
 "pmx_joint_found":true,"live":{"valid":true,"animated":[0,0,0],"display":[0,0,0],"particle":[0,0,0],"fk":[0,0,0],
 "dist_from_animated":0,"seg_dir_err_deg":0}}]}
```
`bend_limit_deg` 180 means free.

### `GET /physics_debug/colliders?entity=N`
```json
{"success":true,"colliders":[{"i":0,"rigid_body":"...","bone":"...","shape":"sphere","size":[1,0,0],"world_center":[0,0,0],
 "world_rotation":[0,0,0,1],"group":0,"mask":"0x86e0"}],
 "bind_pose_penetrations":[{"chain":0,"joint":5,"bone":"Q_0_5","collider":"...","push":0.1}]}
```
`push` uses the raw collision radius, not the fitted one.

### `GET /physics_debug/image`
`entity=N&view=negz&mode=both&width=900&height=900&colliders=true&joints=true&limits=false&highlight=Q&format=png`

| Param | Values | Default |
|---|---|---|
| `entity` | name | required |
| `view` | `negz`, `posz`, `posx`, `negx`, `top` | `negz` |
| `mode` | `both`, `sim`, `anim` | `both` |
| `width`, `height` | 200..2000 | 900 |
| `colliders` | `true`, `false` | `true` |
| `joints` | `true`, `false` (small crosses) | `true` |
| `limits` | `true`, `false` (bend cones as yellow lines) | `false` |
| `highlight` | group key or exact bone name | none |
| `format` | `png` or `json` | `png` |

`409 "no pose yet, wait one frame"` when the entity has no pose. With `format=json` the reply is
`{"width","height","format":"png","data_base64","view":{"name","right","up","forward","bounds_min","bounds_max","pixels_per_unit"},"legend":{...},"highlighted_bones":[...]}`.
Use `gte_send_request` to get the PNG as an image. `http_request` does not show images.

Views (left-handed, orthographic). `right = Cross(up, forward)`. Screen x grows along `right`, screen y grows down along `-up`.

| View | forward (camera to model) | right | up |
|---|---|---|---|
| `negz` | (0, 0, +1) | (+1, 0, 0) | (0, 1, 0) |
| `posz` | (0, 0, -1) | (-1, 0, 0) | (0, 1, 0) |
| `posx` | (-1, 0, 0) | (0, 0, +1) | (0, 1, 0) |
| `negx` | (+1, 0, 0) | (0, 0, -1) | (0, 1, 0) |
| `top` | (0, -1, 0) | (+1, 0, 0) | (0, 0, +1) |

PMX models normally face -Z, so `negz` shows the front. Look at the first picture and note which side is the front of your model.

Legend (no text in the picture, colors carry the meaning):

| Color | RGBA | Meaning |
|---|---|---|
| green | 60, 200, 90, 255 | animated pose |
| red | 230, 60, 60, 255 | simulated pose |
| blue | 80, 140, 255, 255 | colliders |
| yellow | 255, 220, 60, 200 | bend limit cone (`limits=true`) |
| magenta | 255, 0, 255, 255 | highlighted group or bone (wins over red) |
| white cross | 255, 255, 255, 255 | anchor |
| background | 20, 22, 28, 255 | |
| triad, lower left | X red, Y green, Z blue | axes, 30 px, same projection |

## Settings
One table, read by the solver every call. A change shows on the next frame. `default` is the preset of the finished behavior. `legacy` restores the old behavior.

| Key | Type | Range | Default | Legacy |
|---|---|---|---|---|
| `gravity_x` | float | -1000..1000 | 0 | 0 |
| `gravity_y` | float | -1000..1000 | -98 | -9.8 |
| `gravity_z` | float | -1000..1000 | 0 | 0 |
| `fixed_step_hz` | float | 30..480 | 120 | 60 |
| `max_steps_per_frame` | int | 1..32 | 10 | 4 |
| `collision_enabled` | bool | | true | true |
| `damping_multiplier` | float | 0..50 | 1 | 1 |
| `stiffness_multiplier` | float | 0..50 | 1 | 1 |
| `damping_mode` | enum | `mapped`, `raw_pmx` | `mapped` | `raw_pmx` |
| `collision_fit_enabled` | bool | | true | false |
| `bend_limits_enabled` | bool | | true | false |
| `bend_limit_scale` | float | 0..20 | 1 | 1 |
| `bend_limit_min_degrees` | float | 0..180 | 0 | 0 |
| `interpolate_output` | bool | | true | false |
| `rig_enabled` | bool | needs `entity=N` | true | per entity, not in presets |
| `rig_frozen` | bool | needs `entity=N` | false | per entity, not in presets |

- Bool: exactly `true` or `false` (also `1` and `0`). Numbers must be finite, whole text, no leading `+`. Enum: exact case.
- `collision_enabled` is ANDed with the `collisionEnabled` flag of each chain.
- `effective bend limit = clamp(cumulative * bend_limit_scale + radians(bend_limit_min_degrees), 0, pi)`.

## Metrics in plain English
Samples are taken after the physics update wrote the pose. Units are model units (PMX: about 8 cm).

| Metric | Meaning |
|---|---|
| `dist_from_animated` | Distance between the display position and the animated position. |
| `seg_dir_err_deg` | Angle between the simulated segment (parent to joint) and the animated segment. |
| `bend_delta_deg` | How much the bend between two segments differs from the animated bend. Joints with a parent joint only. |
| `limit_violation_deg` | `max(0, seg_dir_err_deg - effective limit)`. How far the segment leaves its cone. |
| `abs_stretch` | `abs(segment length / rest length - 1)`. How much a rod is stretched or squashed. |
| `writeback_err` | Distance between the final bone position (forward kinematics) and the display position. Loss of the position-to-rotation step. |
| `accel_local` | Second difference of the joint position in anchor space, times `render_hz` squared. Body motion removed, so it shows cloth jitter only. Welded groups show the float noise of the position-to-rotation step (`acos` near 0 in `ApplyDynamicChainPhysicsToPose`) times `render_hz` squared, so their ratio between render rates is not a solver quality number. |
| `collision.pushed_particles_per_frame` | Joints moved by collision that are still outside every collider after the final bend cone, summed over all fixed steps of one render frame, then averaged per frame. Not per step. Welded joints skip collision. A push the cone undoes (no place outside the collider inside the cone) is not counted. |
| `collision.raw_pushed_particles_per_frame` | Same sum, but every joint collision moved, whether or not the cone undid it. Equal to the counted value when bend limits are off. |
| `collision.bind_pose_penetrating_joints` | Joints that sit inside a collider in the bind pose (raw radius). |
| `collision.bind_pose_pushed_joints` | Same, with the radius fit and ignore list that are active now. |
| `collision_fit_radius` (per joint) | Half of the largest radius that no collider pushes at the bind pose. The slack keeps a joint that rests on a collider face from being pushed every step. |
| `bend.clamped_joints_per_frame` | Joints the bend cone had to move. |
| `steps_histogram` | Render frames with 0, 1, 2 and 3 or more fixed steps. |
| `reseed_count` | Re-seeds after the first seed: teleport guard or divergence guard. Should be 0. |
| `nan_count` | Particles the divergence guard caught (not finite, or farther than 10 times the longest root path, at least 100 units, from the animated pose). Must be 0. |
| `welded_root_children` | Root children with an effective limit of 1 degree or less. |

Statistics: `p50`, `p95`, `p99` are nearest rank (`index = ceil(p * n) - 1` in the sorted list). `max` is the largest sample.

## Agent playbook
Setup (once per session):
1. `run_app_background build\GreatTamanaEditor.exe --no-vsync`. Poll `GET /http_hello_world`.
2. `POST /instantiate_asset {"gta_path":"<FURINA>"}`. Remember `name`.
3. `POST /physics_debug/play {"entity":"<name>","animation_gta_path":"<CLIP>","frame":0,"speed":1,"loop":true}`.
4. `GET /physics_debug/state?entity=<name>`. Check chains and settings. `GET /physics_debug/preset?name=default` if unsure.

Measure (headless, deterministic, preferred):

5. `POST /physics_debug/run {"entity":"<name>","frames":204,"render_hz":60}`. Read `groups[].seg_dir_err_deg`, `limit_violation_deg`, `dist_from_animated`, `accel_local` and `all`.
6. Change ONE thing: `GET /physics_debug/set?...` (stays) or the `settings` object of the run (temporary). Run again. Compare numbers, not feelings.
7. `GET /physics_debug/image?entity=<name>&view=negz` with `gte_send_request`. Pose it first: `POST /physics_debug/play` with `"speed":0`, then `GET /physics_debug/seek?entity=<name>&frame=60`.

Watch the live editor (what the user sees):

8. `GET /physics_debug/capture?enabled=true&window=120`. Wait 2 s. `GET /physics_debug/metrics?entity=<name>`. `GET /physics_debug/capture?enabled=false`.

Symptom to cause:

| Symptom | Cause |
|---|---|
| `seg_dir_err_deg` p95 far above the cumulative limit, `limit_violation_deg` p95 > 3 | Bend limits off or not applied. Check `settings.bend_limits_enabled` and `joints[].effective_bend_limit_deg`. |
| Root children `dist_from_animated` mean > 0.05 (limit 0 joints) | Weld not working. `effective_bend_limit_deg` of the root children must be 0. |
| `accel_local` at 144 fps much larger than at 60 fps | Interpolation off or broken. Check `settings.interpolate_output`. |
| `bind_pose_pushed_joints` > 0 or high `pushed_particles_per_frame` | Collision fit. `colliders.bind_pose_penetrations` names the pairs. |
| `writeback_err` high on one group | Hub with several children or collision pushes (known limit). Look at that group in `joints`. |
| `abs_stretch` p99 > 0.1 | Too few constraint iterations or a cone fighting the rods. Raise iterations in a run copy to prove it. |
| `physics_ms.median` jumped | `fixed_step_hz` or `max_steps_per_frame`. `GET /profiler/scope?name=PhysicsSystem::Update` gives the live number. |

Rules for the agent:
- Change one switch at a time. Restore with `GET /physics_debug/preset?name=default` when done.
- Never leave the editor with `rig_frozen` or `collision_enabled` changed.
- `run` freezes the editor for about 0.2 to 2 s.
- Read the state before you repeat a call that returned `504`.

## Scenarios
Asset paths are the files in the `build\Project` folder. Use double backslashes in JSON. `FURINA` is `...\build\Project\Furina.gta`, `CLIP` is `...\build\Project\ChatanyaraKuushanku_bassui260717a.gta` (105 VMD frames, 3.5 s).
A run advances the VMD frame by `30 / render_hz` per render frame. One pass is `floor(3.4 * render_hz)` frames, which stays below frame 105.

| Id | Name | Request body of `POST /physics_debug/run` |
|---|---|---|
| S1 | dance60 | `{"entity":"N","frames":204,"render_hz":60}` |
| S2 | dance144 | `{"entity":"N","frames":489,"render_hz":144}` |
| S3 | dance240 | `{"entity":"N","frames":816,"render_hz":240}` |
| S4 | idle60 (no animation, bind pose) | `{"entity":"N","frames":600,"render_hz":60,"warmup_frames":120}` on an entity without a clip |
| S5 | dance60, legacy | after `GET /physics_debug/preset?name=legacy`: same body as S1 |
| S6 | dance60, collision off | S1 body plus `"settings":{"collision_enabled":false}` |
| S7 | dance60, fit off | S1 body plus `"settings":{"collision_fit_enabled":false}` |

Each run starts from a fresh physics state and restores everything it touched. Use `tools/physics_debug/run_scenario.ps1` to run them and check the gates ([README](../../tools/physics_debug/README.md)).

## Gotchas
- A headless `run` steps ALL entities, but records only the target. It freezes the editor for its duration (about 0.2 to 2 s). Time and pause state are ignored.
- With `restore` (default `true`) the run restores the settings and, for every entity, the animator (frame, speed, playing, loop) and the rig (enabled, frozen, chain states, accumulated time). It does this even when a step fails. The live recorder is off during a run.
- The run forces `loop` on for the target animator. A clip that ends without loop would fall back to the bind pose.
- `wrapped: true` means the clip looped during the run. Use fewer frames or a lower `start_frame` for a clean pass.
- `/physics_debug/image`, `/physics_debug/joints` and `/physics_debug/colliders` read the current frame through a zero-time probe step. The probe does not advance the animation or the simulation. The Scene and Game views do not need to be visible.
- The live recorder is zero cost when off: one bool test per frame, no allocation, no extra matrix work.
- Collision is a point plus a radius per joint. Cloth panels between joints do not collide.
- Chains made of fully locked PMX joints (limit 0) follow the body exactly and look rigid. That is what the model data says. Knob: `bend_limit_min_degrees`.
- After `504` the change may still apply. Read `state` before you retry.

## Related
- [HTTP cheat sheet](http-cheat-sheet.md) · [HTTP routes (debug)](http-routes-debug.md) · [HTTP server](../architecture/http-server.md)
- [Animation and physics](../architecture/animation-physics.md) · [Profiling](profiling.md)
