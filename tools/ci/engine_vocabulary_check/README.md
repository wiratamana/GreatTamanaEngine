# EngineVocabularyCheck

The engine layer (`src/Renderer`, `src/Core`) uses abstract names only (texture, buffer, pass, descriptor).
Feature and gameplay words belong in `src/Features` and in game code. The check fails when a file in the
engine layer contains one of the tokens in `tokens.txt`.

- Scanner: `gte_ci_scan`, case-insensitive. Comments and string literals ARE scanned: that is where feature names leak.
- Token forms in `tokens.txt`:
  - `*Atmosphere*` (leading and trailing `*`): matches inside any identifier, comment or string, so
    `AtmosphereLutRenderer` and `ATMOSPHERE_PHASE2_X` both hit. Used for Atmosphere, Shadow, Cloud, Weather,
    Terrain, Player, Enemy, Gameplay.
  - `Sky` (bare word): whole identifier only. Used for Sky, Fog, Water, Volumetric, which are parts of unrelated
    names (`SkyBox`, `VolumetricFroxelMath`).
- `allowlist.txt` is empty. Fix a hit by naming the thing abstractly; do not add a line.
- Exit code 2 means the scan itself is broken (0 files scanned, unreadable file). It never counts as a pass.
- List hits: `gte_ci_scan --root src --only Renderer/ --only Core/ --tokens tokens.txt --comments scan --strings scan --ignore-case --report`.
