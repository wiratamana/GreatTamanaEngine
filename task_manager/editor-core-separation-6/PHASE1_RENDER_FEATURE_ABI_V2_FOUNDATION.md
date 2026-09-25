# PHASE1 — Render Feature ABI v2 Foundation

**Parent:** `PHASE0_MASTER_STRATEGY.md` (read it FIRST, including every Locked
Design Decision — this phase implements Locked Design Decisions #1, #3, #5's
ABI half only).

## Step 1: The Goal

Add the new, additive `_v2` ABI surface under `plugins/gte_plugin_abi/` —
zero `gte_core`/`gte_editor` change, zero behavior change, zero existing
file's meaning altered. By the end of this phase, the new types compile and
are ready for `Core`/`Editor`-side consumption in later phases, but nothing
in the engine calls or references them yet.

## Step 2: The Situation

`plugins/gte_plugin_abi/` currently has 13 files (confirmed via `browse_dir`):
`CMakeLists.txt`, `GtePluginAbiFingerprint.h`,
`GtePluginAbiFingerprintGenerated.h.in`, `GtePluginModuleInfo.h`,
`IEditorPanelModule.h`, `IPluginModule.h`, `IPluginPanelDrawContext.h`,
`IPluginRenderPassBuilder.h`, `IRenderFeatureModule.h`, `PluginExports.h`,
`PluginExportsMacro.h`, `PublicSurface.md`, `SingleCapabilityPluginModule.h`.

`IRenderFeatureModule.h` currently contains ONLY:

```cpp
namespace gte {
class IPluginRenderPassBuilder;
class IRenderFeatureModule_v1 {
public:
    virtual ~IRenderFeatureModule_v1() = default;
    virtual void AddRenderGraphPasses(IPluginRenderPassBuilder& builder) = 0;
};
inline constexpr const char* kIRenderFeatureModule_v1_Name = "IRenderFeatureModule_v1";
} // namespace gte
```

`plugins/gte_plugin_abi/CMakeLists.txt` defines `gte_plugin_abi` as a plain
`INTERFACE` library (`add_library(gte_plugin_abi INTERFACE)`,
`target_include_directories(... INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}"
"${CMAKE_CURRENT_BINARY_DIR}/generated")`) — there is **no per-file source
list** to maintain. A new header under this folder needs ZERO
`CMakeLists.txt` edit; it is automatically reachable via the existing
include directory.

`plugins/gte_plugin_abi/GtePluginAbiFingerprint.h`'s own doc comment on
`abiContractGeneration` states explicitly: "Adding a brand-new
interface/capability version string is NOT a reason to bump this." This
phase never touches `GtePluginAbiFingerprint.h` or
`GtePluginAbiFingerprintGenerated.h.in`.

## Step 3: The Plan

### Step 3.1 — New file: `plugins/gte_plugin_abi/RenderFeatureDescriptor.h`

```cpp
#pragma once

#include <cstdint>

// editor-core-separation-6 campaign, PHASE1
// (PHASE1_RENDER_FEATURE_ABI_V2_FOUNDATION.md) - the fixed-size POD
// descriptor a plugin implementing IRenderFeatureModule_v2 returns once,
// at load time, so the HOST (never the plugin itself) decides real,
// deterministic ordering and blending - see PHASE0_MASTER_STRATEGY.md's
// Locked Design Decision #1/#2/#9 and RENDER_FEATURE_COMPOSITING_FINDINGS_
// AND_PROPOSAL_2026-09-25.md Section 3.2. Mirrors GtePluginModuleInfo's own
// "fixed-size, trivially-copyable POD, safe to read via GetProcAddress()+
// call" discipline - never std::string/std::vector crossing this boundary
// (plugins/gte_plugin_abi/PublicSurface.md's own rule).

namespace gte {

// PHASE0_MASTER_STRATEGY.md Locked Design Decision #1: as of this campaign,
// ONLY RenderFeatureStage::PostComposite and RenderFeatureStage::PreUI are
// actually wired into the live render graph (RenderFeatureCompositor,
// PHASE4/PHASE5). PreOpaque/PostOpaque/PostTransparent are declared here for
// ABI future-proofing ONLY - a plugin that declares one of them today is
// refused, loudly (GTE_LOG_WARNING naming the plugin and the unwired
// stage), at RenderFeatureCompositor::OnPluginsLoaded() time, and is simply
// never invoked. Numeric values are stable and must never be renumbered
// once shipped (a plugin .dll built against an older layout of this enum
// would otherwise silently misinterpret its own declared stage).
enum class RenderFeatureStage : std::uint32_t {
    PreOpaque       = 0,  // NOT WIRED this campaign - declared, refused if used.
    PostOpaque      = 1,  // NOT WIRED this campaign - declared, refused if used.
    PostTransparent = 2,  // NOT WIRED this campaign - declared, refused if used.
    PostComposite   = 3,  // WIRED - today's existing single hook point.
    PreUI           = 4,  // WIRED - runs immediately AFTER every PostComposite
                          // entry, same hook point, same frame (see
                          // PHASE0_MASTER_STRATEGY.md Locked Design Decision #2
                          // for exactly why this is NOT a separate
                          // RenderPassEvent tier in this engine today).
};

// PHASE0_MASTER_STRATEGY.md Locked Design Decision #10 - Replace is the
// legacy _v1-equivalent hard overwrite (still legal, still available to a
// _v2 plugin that wants it); the other four are real, host-owned GPU blends
// (RenderFeatureBlend.comp, PHASE5).
enum class RenderFeatureBlendMode : std::uint32_t {
    Replace         = 0,
    AlphaOver       = 1,
    Additive        = 2,
    Multiply        = 3,
    ScreenSpaceMask = 4,
};

// A plugin author sets these once, typically returned from a single
// GetRenderFeatureDescriptor() override (IRenderFeatureModule_v2, below).
// The HOST NEVER trusts a plugin to self-order at runtime - RenderFeatureCompositor
// collects every loaded plugin's descriptor exactly ONCE, at
// OnPluginsLoaded() time (right after PluginHost::LoadPlugins() returns),
// groups by stage, sorts by priority ascending WITHIN each stage, and
// reuses that one resolved ordering every subsequent frame (never re-sorted
// per-frame - descriptors do not change while a plugin stays loaded).
struct GtePluginRenderFeatureDescriptor {
    // Display name - shown in the Editor's "Render Graph" panel (PHASE7)
    // and every diagnostic log line (collision warnings, unwired-stage
    // warnings). Must be a short, human-readable, null-terminated string;
    // truncated safely if longer than 63 characters (see
    // MakeRenderFeatureDescriptor() helper, Step 3.2, for the bounded-copy
    // helper every _v2 plugin author should use to fill this field).
    char name[64];

    RenderFeatureStage stage;

    // Lower runs first WITHIN the same stage. Author-declared, NEVER
    // auto-assigned by the host. Two plugins in the SAME stage with the
    // SAME priority is a declared CONFLICT - RenderFeatureCompositor logs
    // a loud GTE_LOG_WARNING naming both plugins by their `name` field,
    // then falls back to a documented, STABLE tie-break (lexical
    // comparison of `name`) purely so the engine never crashes - the
    // ambiguity is a visible, actionable fact at load time, never a pixel
    // mystery (PHASE0_MASTER_STRATEGY.md Locked Design Decision, mirrors
    // the Proposal's own Section 3.4 step 3).
    std::int32_t priority;

    RenderFeatureBlendMode blendMode;
};

} // namespace gte
```

### Step 3.2 — New file: `plugins/gte_plugin_abi/IPluginRenderPassBuilder_v2.h`

```cpp
#pragma once

// editor-core-separation-6 campaign, PHASE1
// (PHASE1_RENDER_FEATURE_ABI_V2_FOUNDATION.md) - the ADDITIVE, v2 sibling of
// IPluginRenderPassBuilder.h's existing IPluginRenderPassBuilder (v1,
// UNTOUCHED by this campaign - see plugins/demo_render_feature/
// RenderFeaturePlugin.cpp, still the exact same file, still compiling,
// still working). A small, fixed, still-curated, still-growable palette of
// real drawing operations (RENDER_FEATURE_COMPOSITING_FINDINGS_AND_PROPOSAL_
// 2026-09-25.md Section 3.3) - deliberately just these 3 for this campaign
// (PHASE0_MASTER_STRATEGY.md Locked Design Decision #3 drops the proposal's
// own scene-color/depth-read methods entirely - no fixed operation below
// needs to read the scene first, they only ever draw ON TOP of whatever a
// prior plugin/stage already produced). Every method uses ONLY plain
// built-in types - no std::string/std::vector/gte_core type crosses this
// boundary, mirroring IPluginRenderPassBuilder (v1)'s own exact discipline.

namespace gte {

class IPluginRenderPassBuilder_v2 {
public:
    virtual ~IPluginRenderPassBuilder_v2() = default;

    // Solid-fill, exactly like v1's AddFullscreenClearPass, but composited
    // via THIS feature's own declared blendMode (GtePluginRenderFeatureDescriptor)
    // instead of always being a hard clear - see RenderFeatureOps.comp
    // (PHASE4), opCode 0.
    virtual void AddSolidFillPass(const char* debugName, float r, float g, float b, float a) = 0;

    // A parameterized radial vignette (screen-space, normalized center/
    // radius/softness/color) - covers "damage vignette"/"low-health pulse"/
    // "night-vision edge falloff" without any shader upload. centerX/centerY
    // and innerRadius/outerRadius are normalized 0.0-1.0 fractions of the
    // view's own width/height (innerRadius/outerRadius as a fraction of the
    // view's diagonal - see RenderFeatureOps.comp's own doc comment for the
    // EXACT formula, PHASE4). Alpha naturally falls to 0 outside
    // outerRadius, so this pass's own private target needs no separate
    // "clear to transparent" step first - see RenderFeatureOps.comp,
    // opCode 1.
    virtual void AddRadialVignettePass(const char* debugName, float centerX, float centerY,
        float innerRadius, float outerRadius, float r, float g, float b, float a) = 0;

    // A parameterized full-screen color-grade (brightness/contrast/
    // saturation/tint) - covers "underwater"/"night vision"/"photo mode
    // grade" without any shader upload. brightness/contrast/saturation are
    // multipliers around their own neutral value (1.0 = unchanged);
    // tintR/tintG/tintB is the tint color; tintStrength (0.0-1.0) is how
    // strongly the tint is mixed in - see RenderFeatureOps.comp, opCode 2.
    virtual void AddColorGradePass(const char* debugName, float brightness, float contrast,
        float saturation, float tintR, float tintG, float tintB, float tintStrength) = 0;
};

} // namespace gte
```

### Step 3.3 — Append (never rewrite) `IRenderFeatureModule_v2` to the EXISTING `plugins/gte_plugin_abi/IRenderFeatureModule.h`

Read the file first (Step 2's listing above is the exact current, full
content). Append, inside the existing `namespace gte { ... }` block, AFTER
`kIRenderFeatureModule_v1_Name`'s declaration (never before it, never
touching a single existing character):

```cpp
class IPluginRenderPassBuilder_v2;

// editor-core-separation-6 campaign, PHASE1
// (PHASE1_RENDER_FEATURE_ABI_V2_FOUNDATION.md) - ADDITIVE new interface,
// _v1 (above) completely untouched. See RenderFeatureDescriptor.h and
// IPluginRenderPassBuilder_v2.h.
class IRenderFeatureModule_v2 {
public:
    virtual ~IRenderFeatureModule_v2() = default;

    // Called exactly once, right after this plugin loads (RenderFeatureCompositor::
    // OnPluginsLoaded(), PHASE4) - the returned descriptor is snapshotted and
    // reused for this plugin's entire loaded lifetime; this method is never
    // called again afterward (mirrors the Proposal's own Section 3.4 step 1).
    virtual GtePluginRenderFeatureDescriptor GetRenderFeatureDescriptor() const = 0;

    // Called once per frame, per active view (Game View and/or Scene View),
    // ONLY while this plugin's own declared stage is one RenderFeatureCompositor
    // actually processes this frame - mirrors IRenderFeatureModule_v1::
    // AddRenderGraphPasses()'s own "a plugin does not need to know Game
    // View/Scene View exist as a distinct concept" contract exactly.
    // `builder` targets THIS PLUGIN'S OWN PRIVATE offscreen target for this
    // call - never a target shared with any other loaded plugin (PHASE0_
    // MASTER_STRATEGY.md Locked Design Decision, the single most important
    // structural difference vs. _v1).
    virtual void AddRenderGraphPasses(IPluginRenderPassBuilder_v2& builder) = 0;
};

inline constexpr const char* kIRenderFeatureModule_v2_Name = "IRenderFeatureModule_v2";
```

Add `#include "RenderFeatureDescriptor.h"` at the top of `IRenderFeatureModule.h`
(needed for `GtePluginRenderFeatureDescriptor` in the new method's return
type).

### Step 3.4 — A small bounded-copy helper for descriptor `name`, mirroring `SingleCapabilityPluginModule.h`'s existing `MakeModuleInfo()`

Add to `plugins/gte_plugin_abi/RenderFeatureDescriptor.h`, right after the
struct definition:

```cpp
// Bounded, ALWAYS-null-terminated copy into GtePluginRenderFeatureDescriptor::name
// - mirrors SingleCapabilityPluginModule.h's own MakeModuleInfo() helper and
// reasoning exactly (plugins/gte_plugin_abi/SingleCapabilityPluginModule.h).
inline GtePluginRenderFeatureDescriptor MakeRenderFeatureDescriptor(const char* name,
    RenderFeatureStage stage, std::int32_t priority, RenderFeatureBlendMode blendMode) noexcept
{
    GtePluginRenderFeatureDescriptor descriptor{};
    std::size_t i = 0;
    for (; name[i] != '\0' && i + 1 < sizeof(descriptor.name); ++i) {
        descriptor.name[i] = name[i];
    }
    descriptor.name[i] = '\0';
    descriptor.stage = stage;
    descriptor.priority = priority;
    descriptor.blendMode = blendMode;
    return descriptor;
}
```

### Step 3.5 — Update `plugins/gte_plugin_abi/PublicSurface.md`

Read the file first (current content confirmed in Step 2's research —
ends with a "PHASE4: `IEditorPanelModule_v1`..." bullet under "Added by
later phases"). Append a new bullet under that same section:

```markdown
- editor-core-separation-6 campaign, PHASE1: `RenderFeatureDescriptor.h`
  (`RenderFeatureStage`, `RenderFeatureBlendMode`,
  `GtePluginRenderFeatureDescriptor`, `MakeRenderFeatureDescriptor()`),
  `IPluginRenderPassBuilder_v2.h`, and `IRenderFeatureModule_v2` (appended to
  the existing `IRenderFeatureModule.h`, `IRenderFeatureModule_v1` completely
  untouched) - the additive `_v2` render-feature ABI. Only `PostComposite`
  and `PreUI` stages are actually wired into the live render graph (see
  `task_manager/editor-core-separation-6/PHASE0_MASTER_STRATEGY.md`'s Locked
  Design Decision #1).
```

### Verification (compile check only — no plugin `.dll` rebuild needed)

This phase adds headers with zero engine call site yet, so there is nothing
real to link. Confirm the 3 new/changed headers are syntactically valid and
self-contained C++20 by compiling a tiny scratch translation unit:

```cpp
// scratch, throwaway, NOT committed:
#include "gte_plugin_abi/RenderFeatureDescriptor.h"   // via the include path below
#include "IPluginRenderPassBuilder_v2.h"
#include "IRenderFeatureModule.h"
int main() { return 0; }
```

Use `gcc` (with `use_gpp=true`) with `-I` pointed at
`plugins/gte_plugin_abi` and `-std=c++20`, exactly mirroring
`editor-core-separation-5/PHASE1_PLUGIN_ABI_AUTHORING_SUGAR_FOUNDATION.md`'s
own precedent of a standalone compile check with no `.dll` rebuild. Delete
the scratch file afterward — confirm via `git_status` that it never shows up
as an untracked leftover before committing (Workflow Rule 10).

### What this phase does NOT do

- Does not touch `Core.cpp`, `Core.h`, any file under `src/Core/Plugins/`,
  any demo plugin, or `CMakeLists.txt`.
- Does not implement `PluginRenderPassBuilderAdapter_v2` (the real GPU-work
  implementation of `IPluginRenderPassBuilder_v2` — that is PHASE4).
- Does not implement `RenderFeatureCompositor` (PHASE4/PHASE5).

### Completion

Write `PHASE1_COMPLETION_REPORT.md` in this folder (compile-check command +
output), then `git_add` + `git_commit` (message referencing PHASE1).
