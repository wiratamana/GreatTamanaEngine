# VolkLeakCheck
> Only `src/Renderer` may name Vulkan. This check fails when any other file does.

## What it enforces

| Item | Value |
|---|---|
| ctest name | `VolkLeakCheck` |
| Scanned | `src`, except `Renderer/` |
| Forbidden | `volk.h`, `Vk*`, `vk*`, `VK_*` (`tokens.txt`) |
| Comments | skipped |
| String literals | scanned (`"VkFormat"` is still a leak) |
| Matching | whole identifiers |

## Run

```cmd
ctest --test-dir build -R VolkLeakCheck --output-on-failure
```

List current hits:

```cmd
build\tests\gte_ci_scan.exe --root src --skip Renderer/ --tokens tools/ci/volk_leak_check/tokens.txt --comments skip --strings scan --report
```

## If it fails

- Rename the symbol to an engine type (`PixelFormat`, `DescriptorSetHandle`). Do not edit `tokens.txt`.
- Never add a line to `allowlist.txt`. Lines are only removed.
- A stale allowlist line (file gone or clean) also fails. Delete it.
- Allowlist today: KTX2 `vkFormat` fields, the surface provider, the SDL window, the ImGui Vulkan backend setup.
