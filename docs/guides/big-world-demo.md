# Big World demo
> One click opens a scene with terrain, sea, lake, river, grass, atmosphere and volumetric clouds. For developers who test or extend the landscape features.
> Verified against code: 2026-11-17

## What it is
- A menu item: **File > Open Big World**. **File > Close Big World** undoes it.
- A runtime scene, not a `.gtscene`. The terrain GPU pool cannot be saved in a scene file.
- Three parts: an orchestrator in the Game layer, three small port interfaces, and one editor module that implements them.

## What you see
- The terrain around the west coast spot (x -2000, z -800), camera 6 m above the ground, looking at the sea.
- The sea from the Water feature, an uneven lake in front of you, and a river that flows to the sea.
- Grass on all dry land (preset from the quality tier), cloud and sun shadows, screen-space sun shafts through cloud gaps, sun and sky.
- The camera is in free-fly mode. Right mouse drag looks, WASD moves.
- The blue circles over the lake are the Water edit handles; in the Game view they show only while the Water window is open.

## Quality tiers
| Tier | Terrain profile | Grass preset | Cloud coverage | Cloud cost settings | River |
|---|---|---|---|---|---|
| `lean` | `lean` (no VL, SSGI, GTAO) | Lean | `0.45` (`kLeanCloudCoverage`) | `maxSteps` 48, `resolutionDivisor` 6, `shadowSteps` 6, `shadowUpdate` 4 | no |
| `medium` | `lean` (no VL, SSGI, GTAO) | Medium | `0.5` (`kMediumCloudCoverage`) | `maxSteps` 64, `resolutionDivisor` 6 | yes |
| `big` (menu default) | `lean` (no VL, SSGI, GTAO) | Big | `0.5` (`kBigCloudCoverage`) | feature defaults (`maxSteps` 96) | yes |

Every tier snapshots and restores the render settings it touches:
- `Bloom`: `enabled` `true`, `threshold` `2.0` (`kBloomThreshold`), `intensity` `0.2` (`kBloomIntensity`) so sunlit clouds do not veil the sky.
- `VolumetricCloud`: `coverage` (`0.45`–`0.5`), `shadowRangeKm` `8` (`kCloudShadowRangeKm`, ~62.5 m per 128x128 texel), `shadowSoftness` `0.6` (`kCloudShadowSoftness`), `sunRays` `1.2` (`kCloudSunRays`), `sunRaysContrast` `2.5` (`kCloudSunRaysContrast`), `sunRaysDecay` `1.0` (`kCloudSunRaysDecay`), `sunRaysLength` `1.5` (`kCloudSunRaysLength`), plus the tier march levers above.
- `Grass`: `enabled` `true` and the tier preset.

## Open and close over HTTP
| Route | What it does |
|---|---|
| `GET /bigworld/open` | Starts the demo. Options: `quality=lean\|medium\|big`, `coverage` (`0..1` cloud coverage override), `x`, `z`, `alt` (metres above ground), `heading`, `lookDown` (degrees), `lake=true\|false`, `river=true\|false`, `free=true\|false`, `oceanQuad=true\|false`, `lakeDistance`, `lakeSize` |
| `GET /bigworld/close` | Restores everything the demo changed and closes the terrain |
| `GET /bigworld/state` | Phase, status, info, warnings and a health block for each system |

`open` returns at once. The terrain opens in that frame; the rest runs in the next frames. Poll `/bigworld/state` until `phase` is `Ready` (about 1.5 s on this machine). `open` and `close` are refused for browser requests. `state` is allowed.

The state has these blocks:
- `plan`: camera placement, `terrainProfile`, `lake`, `river`, `cloudCoverage`, `cloudShadowRangeKm`, `cloudShadowSoftness`, `cloudSunRays`, `cloudSunRaysContrast`, `cloudSunRaysDecay`, `cloudSunRaysLength`, `bloomThreshold`, `bloomIntensity`.
- `terrain`: `open`, `nodesResident`, `nodesPending`, `uploadQueue`, `nodesRebuilding`.
- `water`: `featureConnected`, `bodyCount`, `hasOcean`, `oceanLevel`, `carvePending`, `carveApplies`.
- `grass`: render `status`, `tilePresent`, `seaLevel`, `tileRevision`, `blades` (LOD0, LOD1, LOD2), `receivesSunShadow`, `receivesCloudShadow`, `castCascades`, `castBlades`.
- `clouds`: `available`, `enabled`.
- `info`: benign note when a check is inactive by design (for example grass shadow casting when the camera is 15 m or higher above ground).
- `warnings`: things that work but look wrong, for example `"grass gets no sun shadow"` or `"ocean level differs from the grass sea level"`.

## Phases
```text
Idle -> OpeningTerrain -> WaitingForNodes -> AddingWater -> WaitingForWater
     -> EnablingFeatures -> WaitingForGrass -> Ready        (any phase -> Failed)
```
- `WaitingForNodes`: waits until the terrain nodes around the camera are resident. It fails after 90 s.
- `AddingWater`: adds the lake and the river, with levels taken from the ground under them.
- `WaitingForWater`: waits until the carve reached the terrain and the Water feature is connected. A timeout is a warning.
- `EnablingFeatures`: saves the current settings, then applies the cloud, bloom and grass tweaks.
- `WaitingForGrass`: waits for the first grass frame (`status` `Ok` and a height tile). A timeout is a warning.
- `Ready`: checks the systems every half second without delaying entry to `Ready`. If the terrain scene was replaced, the demo restores everything and goes to `Idle`.
- `Failed`: something refused. The demo restores everything and closes the terrain it opened. `error` says why.

## Layers
```text
src/Game/BigWorld/                  gte_core, no feature header, no ImGui, no Vulkan
  BigWorldPlan      tiers, cloud and bloom constants, camera spot, lake outline, river path (pure)
  BigWorldPorts     ITerrainPort, IWaterPort, IFeaturePort and their state structs (plain data)
  BigWorldDemo      the state machine and the cross-checks
src/Features/BigWorld/Editor/       gte_editor
  BigWorldAdapters  the ports on top of the live terrain world, water world, active sun and feature endpoints
  BigWorldModule    File menu items, /bigworld routes, per-frame tick (order 30, after terrain, water, grass tiles)
```
- The Game layer names terrain, water, grass, shadow, bloom and clouds in words only. It includes no feature header.
- The adapters find the other modules on every call. Nothing is cached across frames.
- Render features are driven through their debug endpoints. The demo saves the touched keys first and puts them back on close.

## What the demo checks across systems
- The terrain must be resident before water is added (the carve needs the terrain).
- The ocean level of the Water world must equal the sea level of the grass height tile.
- The Water feature must be connected to the Water world.
- The spot must be above the sea. A spot under the sea only raises a warning.
- The sun must be above the horizon.
- Grass must draw and hold a height tile.
- Sun shadow cascades must reach grass (`game.shadow.cascades > 0`, else warns `"grass gets no sun shadow"`).
- Grass must cast into the cascades (`game.shadow.castCascades > 0`) while the camera is below 15 m above ground; at 15 m or higher, SDSM cascade texels exceed `shadowMaxTexelM` by design and `info` reports it without warning.
- When volumetric clouds are enabled, the cloud shadow map must be bound on grass (`game.shadow.cloud`).
- The terrain must still be open. Another scene (Open Scene, Hi-Z demo, `/terrain/open`) ends the demo cleanly.

## Tests
- `tests/Game/BigWorld/BigWorldPlanTests.cpp` and `BigWorldDemoTests.cpp`: pure tests with fake ports covering tier defaults and overrides, settings snapshot and restore, the state machine, each cross-check warning/info condition, and the state JSON keys.
- `tests/Features/BigWorld/Editor/BigWorldModuleTests.cpp`: adapter and `/bigworld` route tests against fake feature debug endpoints.
- No test draws anything. Headless rendering is not possible here. The live checks use `/get_game_view` and `/get_swapchain`.

## Known limits
- Only one tuned demo spot (the west coast). Other spots work through the `x`, `z`, `heading` options, but the lake and river are placed relative to the spot with fixed sizes. The river is a fixed-length strip: it only reaches the sea when the coast is close. Live checks at 5 other spots showed: a spot in the sea opens under water with no grass and raises the warning `"the spot is under the sea"`; a high alpine spot gets a perched lake and almost no grass (grass fades out above `alpineHeight`).
- The Big World replaces the current scene. It does not ask first.
- Frame time is dominated by the clouds, not by the landscape systems.
