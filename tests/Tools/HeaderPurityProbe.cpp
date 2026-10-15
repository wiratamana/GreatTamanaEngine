// Compiled with `src` as the only include directory: a header that pulls in volk.h or a Vulkan header
// fails to compile here (file not found). Target: gte_header_purity_probe; ctest: HeaderPurity.

#include "Core/Extent.h"
#include "Core/FeatureKit/FeatureKit.h"
#include "Core/GraphicsHandles.h"
#include "Core/PixelFormat.h"
#include "Features/Shadow/ShadowTypes.h"
#include "Renderer/RenderGraph/ComputePass.h"
#include "Renderer/RenderGraph/FullscreenPass.h"
#include "Renderer/RenderGraph/GpuBuffer.h"
#include "Renderer/RenderGraph/GpuParams.h"
#include "Renderer/RenderGraph/PassChain.h"
#include "Renderer/RenderGraph/PerView.h"
#include "Renderer/RenderGraph/RenderGraphHandles.h"
#include "Renderer/RenderGraph/Shader.h"
#include "Renderer/RenderGraph/StaticName.h"
#include "Renderer/RenderGraph/ViewScopeUtil.h"

#if defined(VOLK_HEADER_VERSION) || defined(VK_HEADER_VERSION)
#error "A header meant to be Vulkan-free pulls in Vulkan"
#endif
