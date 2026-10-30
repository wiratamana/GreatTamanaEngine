# FeaturePurityCheck
> Render feature code stays free of Vulkan and of raw engine wrappers. Features use `gte::fx` only.

## What it enforces

| Item | Value |
|---|---|
| ctest name | `FeaturePurityCheck` |
| Scanned | `src/Features/<Name>/`, except `Editor/` |
| Forbidden names | `tokens.txt`: Vulkan names, `Renderer`, `Core`, `Game`, `Buffer`, `Pipeline`, `RenderGraph`, `Native(` and more |
| Forbidden includes | `includes.txt`: `Renderer/`, `Core/Core.h`, `Game/Game.h`, `Game/SceneQuery.h`, `Game/RenderSystem.h`, `Application/` |
| Allowed | `Core/FeatureKit/` |
| Matching | whole identifier, case sensitive. `Renderer` does not hit `MaskRenderer`. |
| Comments and strings | skipped |

## Run

```cmd
ctest --test-dir build -R FeaturePurityCheck --output-on-failure
```

## If it fails

- Use the kit instead. See [Writing a render feature](../../../docs/guides/writing-a-render-feature.md).
- Do not weaken `tokens.txt`. Rename the feature symbol.
- `allowlist.txt` is empty today. A stale line (file gone or clean) fails the check. Lines are only removed.
