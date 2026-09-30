#pragma once

// editor-core-separation-27 campaign (BIG STEP 3 of 4 -
// BIG_STEP_3_PERSISTENT_RESOURCE_CACHE_HONEST_LAYOUT_HISTORY_REV2_2026-09-30.txt,
// Section 6.2) - every BUILT-IN engine feature's RenderGraphPersistentResourceCache
// `owner` identifier, as a single, flat, always-reviewed list. A built-in
// feature's call to RenderGraphBuilder::GetOrCreatePersistentTexture() MUST
// pass one of these constants, never a locally hand-typed literal -
// enforced by code-review convention only (a `const char*` parameter
// cannot itself forbid an arbitrary literal - see TR9). Adding a new
// built-in owner is a ONE-LINE addition to this ONE file - a collision
// between two built-in features would require the SAME line to be added
// twice to the SAME file, a mistake any reviewer glancing at this small,
// append-only header is positioned to catch immediately.
//
// A `_v2`/`_v3` plugin or Project Assembly render feature NEVER adds an
// entry here - its own `owner` argument is always its own already-unique
// `descriptor.name` (mechanically enforced elsewhere - see
// RenderFeatureCompositor.h/PluginHost) - see Section 6.2 population (a).

namespace gte::rg {

// This campaign's own automated Tier-2 test suite
// (RenderGraphPersistentResourceCacheTests.cpp, PHASE4 onward) - the ONE
// real, built-in "population (b)" consumer this campaign itself ships.
inline constexpr const char* kPersistentOwnerCacheValidation = "PersistentResourceCacheValidation";

} // namespace gte::rg
