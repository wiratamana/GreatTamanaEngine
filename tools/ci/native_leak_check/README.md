# NativeLeakCheck

Code outside `src/Renderer` must not touch raw engine wrappers or record GPU work by hand. The check
fails on `Native(` (member, arrow and free-function form), `DescriptorSet`, `DescriptorWrite`,
`AllocateDescriptorSet`, `FreeDescriptorSet`, `ComputePipeline`, `CreateComputePipeline`, `EmitImageBarrier`,
`ImmediateSubmit`, `BeginGraphPassRecording`, `RecordingAccess`, `PlatformHandle(`, `ColorPixelFormat(`,
`DepthPixelFormat(`, `WaitForGpuIdle` and `vkDeviceWaitIdle`. It also fails on an `#include` of a
Renderer-internal header (`includes.txt`: `Compute/ComputePipeline.h`, `Compute/ComputeDispatch.h`, `DescriptorSet.h`).

- Scanner: `gte_ci_scan`. Comments are skipped, string literals are scanned on purpose (editor code
  templates carry `#include` lines inside strings).
- `tokens.txt`: the forbidden names. `includes.txt`: the forbidden include paths (substring match).
  `allowlist.txt`: platform files that may use them, one reason per line.
- Ratchet: a line whose file is gone or clean fails ("stale line: remove it"). Lines are only removed.
- A reason starting with `permanent:` is a real platform boundary; `debt:` lines are work to do.
- Starting debt: `gte_ci_scan --root src --skip Renderer/ --tokens tokens.txt --comments skip --strings scan --report`.
