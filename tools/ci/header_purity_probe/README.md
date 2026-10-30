# HeaderPurity
> A compile test. Headers meant to be Vulkan-free must build without any Vulkan header.

Text scans cannot see a header that reaches Vulkan through another include. This probe can.

## How it works

| Item | Value |
|---|---|
| ctest name | `HeaderPurity` |
| Target | `gte_header_purity_probe` (OBJECT, `EXCLUDE_FROM_ALL`) |
| Source | `tests/Tools/HeaderPurityProbe.cpp` |
| Include path | `src` only. No volk, no Vulkan SDK. |
| Failure | "file not found" when a listed header reaches `volk.h` |
| Backup | `#error` if `VOLK_HEADER_VERSION` or `VK_HEADER_VERSION` is defined |

## What is covered

The headers listed in `HeaderPurityProbe.cpp`:

- core value types (`Extent`, `PixelFormat`, `GraphicsHandles`)
- the feature kit umbrella header `Core/FeatureKit/FeatureKit.h`
- feature type headers (Gtao, Ssgi, Shadow, VolumetricLight)
- the `Profiling/` headers
- a set of `Renderer/` and `Renderer/RenderGraph/` headers that are Vulkan-free

## Run

```cmd
ctest --test-dir build -R HeaderPurity --output-on-failure
```

## Add a header

Add its `#include` to `HeaderPurityProbe.cpp`. Headers that include volk cannot be added. Examples: `Renderer.h`, `Core.h`, `CommandBuffer.h`.
