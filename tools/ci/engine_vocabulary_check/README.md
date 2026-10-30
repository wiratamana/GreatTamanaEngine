# EngineVocabularyCheck
> The engine layer uses abstract names only. Feature and gameplay words belong in `src/Features` and game code.

## What it enforces

| Item | Value |
|---|---|
| ctest name | `EngineVocabularyCheck` |
| Scanned | `src/Renderer`, `src/Core`, `src/Profiling`, and the asset files listed in `tests/CMakeLists.txt` (mesh, cluster, `.gta`, import) |
| Case | ignored |
| Comments and strings | both scanned. Feature names leak there. |
| Allowlist | empty. It stays empty. |

## Token forms (`tokens.txt`)

| Form | Meaning | Words |
|---|---|---|
| `*Word*` | hits inside any identifier, comment or string | Atmosphere, Shadow, Cloud, Weather, Terrain, Player, Enemy, Gameplay |
| `Word` | hits a whole identifier only | Sky, Fog, Water, Volumetric |

Bare words are used where the word is part of unrelated names (`SkyBox`, `VolumetricFroxelMath`).

## Run

```cmd
ctest --test-dir build -R EngineVocabularyCheck --output-on-failure
```

## If it fails

- Name the thing abstractly: texture, buffer, pass, view.
- Do not add an allowlist line.
- Exit code 2 means the scan itself broke (0 files scanned, unreadable file). It is never a pass.
