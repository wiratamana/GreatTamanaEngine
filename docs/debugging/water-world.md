# Water world (bodies, carve, panel, HTTP)
> Lakes, rivers and the ocean as data, the terrain carve they cause, the Water window and the `/water/*` routes.
> The render side (waves, shading) is in [Water feature](../reference/render-features/water.md).
> Verified against code: 2026-11-16

## What it is
- `src/Water/` is pure CPU: `WaterBody` (ocean, lake polygon up to 32 corners, river path up to 64 points), `WaterSettings`, spline and polygon math, `WaterBodySet` (immutable checked snapshot with hashes), `WaterCarve`, JSON, map picture.
- `src/Editor/Water/WaterWorld` owns the bodies on the main thread. It hands the carve to the terrain and the bodies and look to the Water render feature.
- Bodies are **not** ECS entities and are **not** saved in the `.gtscene`. Save them with `/water/save` or the panel (JSON). Reason: the terrain scene wipes every root entity on reopen, and the scene file keeps no terrain state anyway.
- One ocean at most, 16 bodies at most. The ocean level is `seaLevel` (default 0, the same height the terrain colours use).

## Terrain carve
- `terrain::ITerrainCarve` (src/Terrain/TerrainCarve.h) is a pure function of world X/Z and the base height. `WaterCarve` implements it.
- Hook: `BuildGridMesh` applies it after erosion on the whole tile (margin included), so neighbouring nodes stay bit-equal. `TerrainHeightField::Height()` applies it too. `FillGrid` and `BaseHeight` stay raw.
- Lake: bed slopes to `depth` over `shoreWidth`; land outside is pulled to the water over `bankWidth`; optional rim (dam) keeps low ground from spilling.
- River: parabolic channel along the spline, same bank; the surface never rises downstream (`EnforceDescent`).
- Coarse LODs fade rivers narrower than about 3 vertex spacings. Expect far rivers to vanish (no geomorph).
- Edits go through a debounce (default 0.3 s, or on release / `/water/flush`). Only nodes that the old or new carve can change are rebuilt. The old mesh keeps drawing until the new one is uploaded into a fresh slot (`StreamerUpdate::rebuilt`).
- A baked `.gta` node is skipped (generated instead) when the carve touches it.
- The old flat "Terrain Water" quad is hidden while the Water feature is connected (`hideOceanQuad`). `/terrain/ocean_quad?visible=true` brings it back.

## Measured (Iris Xe, 128 m nodes, 2 m spacing, debounce 0.3 s)
| Edit | Nodes rebuilt | Wall time to all new meshes |
|---|---|---|
| Add lake 240x160 m | 6 | 30 ms |
| Add river 700 m | 16 | 65 ms |
| Drag release (lake corner) | 9 to 15 | 43 to 55 ms |
| Carve cost per node (tile 85x85) | | 0.13 to 0.33 ms (lake), 0.9 to 2.7 ms (river + lake, loaded machine) |
Frame time with a lake and a river: 9.4 ms vs 9.0 ms without (719x360, noise level). The carve costs nothing per frame.

## HTTP (all GET; changing routes refuse browsers with 403)
Read-only: `state`, `list`, `settings`, `probe`, `map`, `leaks`.
| Route | Use |
|---|---|
| `/water/state` | revision, hashes, carvePending, carveApplies, featureConnected, issues, bodies, settings |
| `/water/set?waveHeight=2&ssrEnabled=true&quality=low&carveDebounce=0.3` | settings by name, all or nothing |
| `/water/add_lake?cx=&cz=&sx=&sz=&level=&depth=&name=` or `points=x,z;x,z;...` | add lake; `auto_level=true` picks the level from the ground |
| `/water/add_river?points=x,y,z,w;...&depth=&auto_level=true&freeboard=1` | add river; 3-number points take `width=` |
| `/water/add_ocean`, `/water/remove?id=`, `/water/clear` | ocean, remove |
| `/water/set_body?id=&level=&depth=&shoreWidth=&bankWidth=&rimHeight=&carve=&seal=&waveScale=&name=` | body parameters |
| `/water/move_point?id=&index=&x=&z=[&y=&w=]`, `/water/add_point?id=&index=&x=&z=`, `/water/remove_point?id=&index=` | the same code path as the view gizmos |
| `/water/flush` | hand the pending carve to the terrain now |
| `/water/probe?x=&z=` | base and carved height, body, surface, depth, shore distance |
| `/water/map?size=512&range=1000&cx=&cz=` | top-down PNG of the carved ground, water, outlines and control points (about 250 ms at 512) |
| `/water/leaks?id=` | how many outline samples of a lake lie below its level, and a suggested level |
| `/water/save?path=`, `/water/load?path=` | JSON |
| `/terrain/height?x=&z=` | noise height and carved height |
| `/terrain/ocean_quad?visible=` | the old flat quad |
Water look is also readable through `/feature_debug/state?name=Water` and `debugView=ShoreMask` etc. (see the feature page).

## Water window and gizmos
- `Window > Water`. Status (feature connected, carve pending, gizmo debug line), body list, per-body carve and look, global look and performance (quality preset, SSR, wave count), file save/load.
- Game and Scene views draw the outlines and handles of every lake and river. Drag a handle to move it. Drag a white square to add a point. Ctrl+click an outline to add a point. Delete removes the selected point. The selected body is yellow, the selected point orange.
- Terrain beyond the Game frustum is not drawn in the Scene view, so author in the Game view first.
- While a handle is under the cursor or dragged, the terrain camera does not react.

## Injected mouse (for AI tests)
`/ui_mouse` takes **desktop** coordinates, not window pixels. Add the client origin of the editor window (for example read it with `ClientToScreen`; it was (320,156) on this machine). Hide floating windows (Water, Render Graph) first: a floating window is its own OS window and eats the mouse. Finish with `action=release`.

## Known limits
- Rivers and lakes are carved with the raw noise height; erosion on LOD 0/1 can differ by metres, so use scene depth (the shader does) for the exact shoreline.
- River surface in the shader uses the same Catmull-Rom as the CPU but a 64-point cap, so very long rivers are resampled.
- Lakes sit on one flat level; a lake on a steep slope needs the rim or it will look like a raised pool.
