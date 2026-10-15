# EngineVocabularyCheck

The engine layer (`src/Renderer`, `src/Core`) uses abstract names only (texture, buffer, pass, descriptor).
Feature and gameplay words belong in `src/Features` and in game code. The check fails when a file in the
engine layer contains one of the words in `tokens.txt` (Atmosphere, Shadow, Sky, Cloud, Volumetric, Fog,
Weather, Terrain, Water, Player, Enemy, Gameplay).

- Scanner: `gte_ci_scan`. Whole identifier, case-insensitive. Comments and string literals ARE scanned:
  that is where feature names leak. A longer identifier such as `ShadowMapSlot` is not a match.
- `allowlist.txt` is empty. Fix a hit by naming the thing abstractly; do not add a line.
- Starting debt: `gte_ci_scan --root src --only Renderer/ --only Core/ --tokens tokens.txt --comments scan --strings scan --ignore-case --report`.
