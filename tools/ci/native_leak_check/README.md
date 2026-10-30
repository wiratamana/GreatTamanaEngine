# NativeLeakCheck
> Code outside `src/Renderer` must not touch raw engine wrappers or record GPU work by hand.

## What it enforces

| Item | Value |
|---|---|
| ctest name | `NativeLeakCheck` |
| Scanned | `src`, except `Renderer/` |
| Forbidden names | `tokens.txt`: `Native(`, `DescriptorSet`, `ComputePipeline`, `EmitImageBarrier`, `ImmediateSubmit`, `BeginGraphPassRecording`, `RecordingAccess`, `PlatformHandle(`, `WaitForGpuIdle`, `vkDeviceWaitIdle` and more |
| Forbidden includes | `includes.txt`: `Compute/ComputePipeline.h`, `Compute/ComputeDispatch.h`, `/DescriptorSet.h` |
| Comments | skipped |
| String literals | scanned. Editor code templates carry `#include` lines in strings. |

`Native(` covers `.Native()`, `->Native()` and the free function `Native(handle)`.

## Allowlist

`allowlist.txt` has one reason per line.

| Prefix | Meaning | Today |
|---|---|---|
| `permanent:` | a real platform boundary | `Editor/ImGuiEditorLayer.cpp`, `Window/Window.h` |
| `debt:` | work to do | none |

A line whose file is gone or clean fails the check ("stale line: remove it"). Lines are only removed.

## Run

```cmd
ctest --test-dir build -R NativeLeakCheck --output-on-failure
```

## If it fails

Use `rg::OneShot`, `rg::Shader`, `rg::GpuBuffer` and the pass kinds. See [Render graph](../../../docs/architecture/render-graph.md).
