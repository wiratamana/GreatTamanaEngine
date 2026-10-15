# HeaderPurity

Text checks cannot see a header that reaches Vulkan through another include. This probe compiles
`tests/Tools/HeaderPurityProbe.cpp` as an OBJECT target (`gte_header_purity_probe`) whose only include
directory is `src`: no volk, no Vulkan SDK, no PCH, no forced include. A header on the list that pulls in
`volk.h` fails with "file not found", so the check cannot fail open. A `#error` on `VOLK_HEADER_VERSION` /
`VK_HEADER_VERSION` is the second line of defence.

- Covered today: `Core/{Extent,GraphicsHandles,PixelFormat}.h`, the feature kit umbrella header
  `Core/FeatureKit/FeatureKit.h` (the real one, so every kit header it pulls in is checked),
  `Features/Shadow/ShadowTypes.h` and
  `Renderer/RenderGraph/{ComputePass,FullscreenPass,GpuParams,PassChain,PerView,RenderGraphHandles,Shader,StaticName,ViewScopeUtil}.h`.
- To cover another header, add its `#include` to the probe. `RenderGraphBuilder.h`, `Renderer.h`, `Core.h`
  and `CommandBuffer.h` include volk and cannot be added.
- Run: `ctest -R HeaderPurity`.
