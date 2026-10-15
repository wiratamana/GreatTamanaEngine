# NativeLeakCheck

Code outside `src/Renderer` must not touch raw engine wrappers or record GPU work by hand. The check
fails on `Native(` (member, arrow and free-function form), `ComputeDescriptorSet`, `ComputeDescriptorWrite`,
`AllocateComputeDescriptorSet`, `ComputePipeline`, `EmitImageBarrier`, `ImmediateSubmit`,
`BeginGraphPassRecording`, `WrapRaw`, `ctx.cmd`, `PassContext::cmd`, `ColorPixelFormat(`,
`DepthPixelFormat(`, `WaitForGpuIdle` and `vkDeviceWaitIdle`.

- Scanner: `gte_ci_scan`. Comments are skipped, string literals are scanned on purpose (editor code
  templates carry `#include` lines inside strings).
- `tokens.txt`: the forbidden names. `allowlist.txt`: starting debt, one reason per line.
- Ratchet: a line whose file is gone or clean fails ("stale line: remove it"). Lines are only removed.
- A reason starting with `permanent:` is a real platform boundary; `debt:` lines are work to do.
- Starting debt: `gte_ci_scan --root src --skip Renderer/ --tokens tokens.txt --comments skip --strings scan --report`.
