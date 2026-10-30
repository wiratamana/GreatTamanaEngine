# BlockingCallCheck
> Every CPU wait on the GPU goes through one file. This check fails when any other file waits.

## What it enforces

| Item | Value |
|---|---|
| ctest name | `BlockingCallCheck` |
| Scanned | `src`, except `Renderer/Vulkan/GpuWaitService.cpp` |
| Forbidden | `vkWaitForFences`, `vkDeviceWaitIdle`, `vkQueueWaitIdle`, `vkWaitSemaphores`, `vkAcquireNextImageKHR`, `vkQueuePresentKHR` |
| Comments | skipped |
| String literals | scanned |
| Allowlist | empty. It stays empty. |

## Rules

- `GpuWaitService` names each wait with a `BlockSite`.
- Code outside `src/Renderer` drains the GPU with `Renderer::DrainGpuForCodeUnload()` or `Renderer::DrainGpuForResourceRelease(detail)`.
- Both return a `[[nodiscard]]` `BlockOutcome`. Check it.

## Run

```cmd
ctest --test-dir build -R BlockingCallCheck --output-on-failure
```

## If it fails

Move the wait into `GpuWaitService`. Do not add an allowlist line. See also [NativeLeakCheck](../native_leak_check/README.md).
