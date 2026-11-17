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
- Grass on all dry land (preset from the quality tier), clouds, sun and sky.
- The camera is in free-fly mode. Right mouse drag looks, WASD moves.
- The blue circles over the lake are the Water edit handles. They show whenever terrain is open.

## Quality tiers
| Tier | Grass preset | Cloud march steps | River | Frame ms at 1264x629 (Iris Xe) |
|---|---|---|---|---|
| `lean` | Lean | 48 | no | 28 to 30 |
| `medium` | Medium | 64 | yes | 31 |
| `big` (menu default) | Big | 96 (feature default) | yes | 35 |

Numbers are medians from one live session. This notebook varies 2 to 4 ms between sessions. Clouds cost 13 to 20 ms of every tier.

## Open and close over HTTP
| Route | What it does |
|---|---|
| `GET /bigworld/open` | Starts the demo. Options: `quality=lean\|medium\|big`, `x`, `z`, `alt` (metres above ground), `heading`, `lookDown` (degrees), `lake=true\|false`, `river=true\|false`, `free=true\|false`, `oceanQuad=true\|false`, `lakeDistance`, `lakeSize` |
| `GET /bigworld/close` | Restores everything the demo changed and closes the terrain |
| `GET /bigworld/state` | Phase, status, warnings and a health block for each system |

`open` returns at once. The terrain opens in that frame; the rest runs in the next frames. Poll `/bigworld/state` until `phase` is `Ready` (about 1.5 s on this machine). `open` and `close` are refused for browser requests. `state` is allowed.

The state has these blocks:
- `terrain`: `open`, `nodesResident`, `nodesPending`, `uploadQueue`, `nodesRebuilding`.
- `water`: `featureConnected`, `bodyCount`, `hasOcean`, `oceanLevel`, `carvePending`, `carveApplies`.
- `grass`: render `status`, `tilePresent`, `seaLevel`, `tileRevision`, `blades` (LOD0, LOD1, LOD2).
- `clouds`: `enabled`.
- `warnings`: things that work but look wrong, for example "ocean level differs from the grass sea level".

## Phases
```text
Idle -> OpeningTerrain -> WaitingForNodes -> AddingWater -> WaitingForWater
     -> EnablingFeatures -> WaitingForGrass -> Ready        (any phase -> Failed)
```
- `WaitingForNodes`: waits until the terrain nodes around the camera are resident. It fails after 90 s.
- `AddingWater`: adds the lake and the river, with levels taken from the ground under them.
- `WaitingForWater`: waits until the carve reached the terrain and the Water feature is connected. A timeout is a warning.
- `EnablingFeatures`: saves the current settings, then turns the clouds on and applies the grass preset.
- `WaitingForGrass`: waits for the first grass frame (`status` `Ok` and a height tile). A timeout is a warning.
- `Ready`: checks the three systems every half second. If the terrain scene was replaced, the demo restores everything and goes to `Idle`.
- `Failed`: something refused. The demo restores everything and closes the terrain it opened. `error` says why.

## Layers
```text
src/Game/BigWorld/                  gte_core, no feature header, no ImGui, no Vulkan
  BigWorldPlan      tiers, camera spot, lake outline, river path (pure)
  BigWorldPorts     ITerrainPort, IWaterPort, IFeaturePort (plain data)
  BigWorldDemo      the state machine and the cross-checks
src/Features/BigWorld/Editor/       gte_editor
  BigWorldAdapters  the ports on top of the live terrain world, water world and the feature debug endpoints
  BigWorldModule    File menu items, /bigworld routes, per-frame tick (order 30, after terrain, water, grass tiles)
```
- The Game layer names terrain, water and grass in words only. It includes no feature header.
- The adapters find the other modules on every call. Nothing is cached across frames.
- The clouds and the grass are driven through their debug endpoints (`enabled`, `preset`, the cloud cost keys of the tier). The demo saves the keys first and puts them back on close.

## Quality tiers
| Tier | Grass preset | Cloud settings | River |
|---|---|---|---|
| `lean` | Lean | `maxSteps` 48, `resolutionDivisor` 6, `shadowSteps` 6, `shadowUpdate` 5 | no |
| `medium` | Medium | `maxSteps` 64, `resolutionDivisor` 6 | yes |
| `big` (menu default) | Big | feature defaults | yes |

The clouds are the biggest cost of the scene (about 13 to 15 ms of 35 at ground level on an Iris Xe). A coarser cloud grid (`resolutionDivisor` 6 instead of the automatic 4) took 10 to 20 percent off the frame in an interleaved A/B on that GPU. The clouds get a little grainier. The far horizon clouds only thin out when `maxDistanceM` is also cut, so the tiers do not touch it.

## What the demo checks across systems
- The terrain must be resident before water is added (the carve needs the terrain).
- The ocean level of the Water world must equal the sea level of the grass height tile.
- The Water feature must be connected to the Water world.
- Grass must draw and hold a height tile.
- The terrain must still be open. Another scene (Open Scene, Hi-Z demo, `/terrain/open`) ends the demo cleanly.

## Tests
- `tests/Game/BigWorld/BigWorldPlanTests.cpp` and `BigWorldDemoTests.cpp`: pure tests with fake ports. They cover the success path, a failure at each phase, a scene replaced in the middle, close while opening, restore of settings, and the cross-checks.
- No test draws anything. Headless rendering is not possible here. The live checks use `/get_game_view` and `/get_swapchain`.

## Known limits
- Only one demo spot. Other spots work through the `x`, `z`, `heading` options, but the lake and river are placed relative to it and may land in a hill.
- The Big World replaces the current scene. It does not ask first.
- Frame time is dominated by the clouds, not by the landscape systems.
