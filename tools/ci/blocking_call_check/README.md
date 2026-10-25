# BlockingCallCheck

Every CPU wait on the GPU goes through `GpuWaitService` (`src/Renderer/Vulkan/GpuWaitService.cpp`).
It is bounded, named by a `BlockSite`, result-checked and reported to the observer.

The check fails when any other file under `src` names `vkWaitForFences`, `vkDeviceWaitIdle`,
`vkQueueWaitIdle`, `vkWaitSemaphores` or `vkAcquireNextImageKHR` (the acquire has its own bounded wrapper).

- Scanner: `gte_ci_scan` (`tools/ci/common`). Comments are skipped, string literals are scanned.
- The wait service source file is excluded with `--skip`. The allowlist is empty and stays empty.
- Idle waits outside `src/Renderer` go through `Renderer::WaitForGpuIdle(site, detail)` and its two named
  wrappers. They take a mandatory site and return a `[[nodiscard]]` `BlockOutcome`.
