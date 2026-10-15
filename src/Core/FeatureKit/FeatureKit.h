#pragma once

// Umbrella header of the feature kit: everything a render feature includes. Vulkan-free by contract
// (compiled by the header purity probe with `src` as its only include directory).

#include "ComputeChain.h"
#include "ComputeShader.h"
#include "Feature.h"
#include "FeatureLife.h"
#include "FeatureRegistration.h"
#include "FeatureSetup.h"
#include "Frame.h"
#include "FullscreenChain.h"
#include "FullscreenShader.h"
#include "Gate.h"
#include "GpuParams.h"
#include "PerView.h"
#include "PersistentTarget.h"
#include "SharedOncePerFrame.h"
#include "Texture.h"
#include "View.h"
