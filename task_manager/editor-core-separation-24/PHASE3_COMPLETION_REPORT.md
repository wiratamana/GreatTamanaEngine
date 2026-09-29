# PHASE3 — Completion Report: The Generated Screen Post-Process Pass Template

Parent: `PHASE0_MASTER_STRATEGY.md`. Phase file:
`PHASE3_GENERATED_SCREEN_POST_PROCESS_PASS_TEMPLATE.md`.

## What changed

Exactly one new, pure, free function was added to
**`src/Editor/EditorProjectLifecycleCapability.cpp`**, inside the file's
existing anonymous namespace, alongside its three precedent
`Build*CppContent()` siblings — placed right after `BuildFragmentShaderContent()`
and before the `ScaffoldFileSpec` struct (now lines 254-327):

```cpp
std::string BuildScreenPostProcessPassCppContent(const std::string& name, std::int32_t priority)
```

- Byte-for-byte the template text specified in the phase file's own Step 3.2
  code block, including the full header comment, the `#include` list, the
  `Register__NAME__ScreenPass(gte::Core&)` function, the
  `core.RegisterProjectRenderFeature(...)` call
  (`RenderFeatureStage::PostComposite` / `RenderFeatureBlendMode::AlphaOver`),
  and the lambda that clears the private target to a translucent red
  (`std::array<float, 4>{ 1.0f, 0.0f, 0.0f, 0.15f }`) via
  `builder.AddRenderPass(..., RenderPassDrawKind::DrawQuad, RenderPassEvent::AfterEverything)`.
- Uses the collision-safe two-token substitution scheme mandated by the phase
  file's own Step 3.1: `ReplaceAll(kTemplate, "__NAME__", name)` FIRST, then
  `ReplaceAll(content, "@@PRIORITY@@", std::to_string(priority))` SECOND — NOT
  the source design document's own `__NAME__`/`__PRIORITY__` sketch. `@@PRIORITY@@`
  contains `@`, a character `IsValidProjectAssemblyIdentifierName()` can never
  legally accept in any project/pass name, so the second substitution pass is
  structurally guaranteed to touch only the one, original
  `/*priority=*/@@PRIORITY@@,` template spot — never a fragment that came from
  the user's own name (see "Adversarial name check" below for the concrete,
  mechanical proof this closes the exact bug Step 3.1 describes).
- Added one new include, `<cstdint>`, to the file's existing include block
  (needed for `std::int32_t`, the function's second parameter — not present in
  the file before this phase; every other file across `src/Editor/` that uses
  `std::int32_t`/similar already includes it directly, so this matches
  existing convention rather than relying on a transitive include).

**Nothing else changed.** `BuildScaffoldFileSpecs()`/`ReminderMessageForKind()`/
`CreateAssetScaffold()` were NOT touched — the new function is currently
uncalled from anywhere real (PHASE6's own job). No test file was added or
modified (see "Why no new Tier-1 test" below).

## Why no new Tier-1 test

Per the phase file's own Step 2/Step 4 item 3: `BuildScreenPostProcessPassCppContent()`
is placed in the same anonymous namespace as its three precedent siblings,
giving it INTERNAL linkage — exactly like `BuildRenderPassCppContent()`/
`BuildComputePassCppContent()`/etc., none of which has its own direct,
isolated Tier-1 test today. `tests/Editor/AssetScaffoldTemplateTests.cpp`
already exists and its own header comment documents this convention: every
template builder in this file is exercised ONLY through the one real,
externally-linked, public entry point, `CreateAssetScaffold()` — never by
calling a builder function directly from a separate translation unit (which
is impossible anyway with internal linkage). PHASE6's own dedicated
dispatch-branch test is the correct, and only, place that will exercise this
function's real output end-to-end, once `CreateAssetScaffold()` actually calls
it. This phase's own verification story is the isolated-compile proof and the
adversarial-name check below, exactly as the phase file itself prescribes.

## Step 4: Verification

### 1. Incremental build

`cmake --build build` — succeeded, 7/7 steps, **zero errors, zero warnings**
(including no "unused static function" warning for the new,
currently-uncalled `BuildScreenPostProcessPassCppContent()` — this codebase's
own toolchain evidently does not warn on an unused function inside an
anonymous namespace that is otherwise a valid, well-formed declaration used by
nothing yet, at least at this project's current warning level):

```
[0/2] Re-checking globbed directories...
[1/7] Building CXX object CMakeFiles/gte_editor.dir/src/Editor/EditorProjectLifecycleCapability.cpp.obj
[2/7] Linking CXX static library libgte_editor.a
[3/7] Linking CXX executable GreatTamanaEditor.exe; ...
[4/7] Linking CXX executable tests\GreatTamanaEngineTests.exe; ...
[5/7] Linking CXX shared library project_assemblies\ProjectAssemblyProbe_Editor.dll; ...
[6/7] Linking CXX shared library project_assemblies\ProjectAssemblyProbe_Game.dll; ...
```

### 2. Isolated-compile proof

A throwaway, scratch driver (`scratch_phase3/driver.cpp`, deleted after this
verification — never part of the real source tree) contained a VERBATIM copy
of `ReplaceAll()` and the new `BuildScreenPostProcessPassCppContent()` exactly
as they exist in the real source file, plus a `main()` that called the
function twice (see "Adversarial name check" below for the second call) and
wrote each result to a `.cpp` file.

Compiled and ran with:

```
g++ -std=c++17 -o driver.exe driver.cpp
driver.exe
```

— zero errors, zero warnings. The "ordinary name" case
(`BuildScreenPostProcessPassCppContent("ScreenTintDemo", 3)`) produced
`normal_output.cpp` (confirmed via `read_file`, matching the phase file's
template exactly, with `__NAME__` -> `ScreenTintDemo` and `@@PRIORITY@@` -> `3`
substituted correctly everywhere, including inside the function name
`RegisterScreenTintDemoScreenPass` and the two string literals
`"ScreenTintDemo.ScreenTint"`/`"ScreenTintDemo.ScreenTint.Clear"`).

To prove this generated text is not merely syntactically plausible but
**genuinely, actually compileable** inside this engine's real toolchain and
real include-path setup, `normal_output.cpp` was copied byte-for-byte to
`Projects/ProjectAssemblyProbe/Assets/ScreenTintDemoScreenPass.cpp` (the exact
real location a live `CreateAssetScaffold()` call would write it to — same
directory depth, so the generated file's own `"../../../src/Core/Core.h"`-style
relative includes resolve correctly), then:

```
cmake -S . -B build            # required: CONFIGURE_DEPENDS re-globs Assets/ at reconfigure time
cmake --build build --target ProjectAssemblyProbe_Game
```

Result:

```
[0/2] Re-checking globbed directories...
[1/3] Building CXX object Projects/ProjectAssemblyProbe/Libraries/CMakeFiles/ProjectAssemblyProbe_Game.dir/__/Assets/ScreenTintDemoScreenPass.cpp.obj
[2/3] Linking CXX shared library project_assemblies\ProjectAssemblyProbe_Game.dll; ...
```

Zero errors — the generated function compiles and links cleanly as a real
`.dll` translation unit, using the exact real `gte::Core`/`gte::rg::RenderGraphBuilder`/
`gte::rg::TextureHandle`/`VkExtent2D`/`gte::RenderFeatureStage`/
`gte::RenderFeatureBlendMode`/`gte::rg::PassKind`/`gte::rg::RenderPassDrawKind`/
`gte::rg::RenderPassEvent` types this campaign's own Step 2 confirmed. The
`(void)`-free unused `registered` local (Step 3's own note) did **not** trigger
any warning severe enough to break this build either, confirming the phase
file's own prediction.

**Cleanup**: `Projects/ProjectAssemblyProbe/Assets/ScreenTintDemoScreenPass.cpp`
was deleted, then `cmake -S . -B build` + `cmake --build build` were re-run —
`[1/2] Linking CXX shared library project_assemblies\ProjectAssemblyProbe_Game.dll`
confirmed a clean re-link with no dangling reference to the removed file. The
throwaway `scratch_phase3/` folder (driver source + both generated `.cpp`
outputs + `driver.exe`) was deleted entirely afterward. `git status` confirms
only `src/Editor/EditorProjectLifecycleCapability.cpp` is modified —
`Projects/` is `.gitignore`d, so this whole temporary excursion left zero
git-visible trace either way.

### 3. Adversarial name check (the exact hazard Step 3.1 describes)

The SAME scratch driver's second call,
`BuildScreenPostProcessPassCppContent("Foo__PRIORITY__Bar", 7)` — a name that
`IsValidProjectAssemblyIdentifierName()` legally accepts (matches
`^[A-Za-z_][A-Za-z0-9_]*$`) and that contains the literal substring
`"__PRIORITY__"` inside it — produced this real, actual output (captured via
both `std::cout` in the driver and `adversarial_output.cpp`):

```cpp
// Foo__PRIORITY__BarScreenPass.cpp - generated by the Editor's "Create -> Screen
// Post-Process Pass" action. ...
// the one required call, RegisterFoo__PRIORITY__BarScreenPass(core);, into your
// project's own RegisterProject() function (Assets/<ProjectName>Game.cpp)
...
#include "../../../src/Core/Core.h"
#include "../../../src/Renderer/RenderGraph/RenderGraphBuilder.h"
#include "../../../src/Renderer/RenderGraph/RenderGraphTypes.h"

#include <array>

void RegisterFoo__PRIORITY__BarScreenPass(gte::Core& core)
{
    const bool registered = core.RegisterProjectRenderFeature(
        "Foo__PRIORITY__Bar.ScreenTint",
        gte::RenderFeatureStage::PostComposite,
        gte::RenderFeatureBlendMode::AlphaOver,
        /*priority=*/7, // auto-assigned - ...
        [](gte::rg::RenderGraphBuilder& builder, gte::rg::TextureHandle privateTarget, VkExtent2D /*extent*/) {
            ...
            builder.AddRenderPass("Foo__PRIORITY__Bar.ScreenTint.Clear", gte::rg::PassKind::Graphics,
                ...
                gte::rg::RenderPassDrawKind::DrawQuad, gte::rg::RenderPassEvent::AfterEverything);
        });
    ...
}
```

**Confirmed uncorrupted**: the generated function name reads
`RegisterFoo__PRIORITY__BarScreenPass` — the literal substring `"__PRIORITY__"`
that came FROM the user's own name survived the second `ReplaceAll()` pass
completely untouched, and the priority VALUE (`7`) was correctly substituted
at the one, single, original `/*priority=*/@@PRIORITY@@,` template spot
(reading `/*priority=*/7,`), nowhere else. This is the exact, mechanical proof
that the `@@PRIORITY@@` token choice (Step 3.1) closes the real, confirmed
bug the source design document's own naive `__NAME__`/`__PRIORITY__`
sequential-substitution sketch would have hit: had this file instead used
`__PRIORITY__` as the second token (the source document's own sketch), this
exact adversarial name would have produced a corrupted
`RegisterFoo0BarScreenPass` (priority `7`, not `0` — but same corruption
shape) instead, and PHASE6's own separately-computed
`"Register" + name + "ScreenPass"` auto-wire string would then read
`"RegisterFoo__PRIORITY__BarScreenPass"` — a literal mismatch producing a real
linker error the first time such a project compiled. That bug class cannot
occur with this phase's actual implementation, confirmed here empirically,
not merely by argument.

## No full build / full regression run

Per Locked Decision 2 (`PHASE0_MASTER_STRATEGY.md`) — that is PHASE8's own
job. This phase used only the incremental `cmake --build build` gate plus the
targeted isolated-compile/adversarial checks above. No new/changed Tier-1
test exists for this phase (see "Why no new Tier-1 test" above), so no
targeted `ctest -R` run was applicable either.

## Files touched

- `src/Editor/EditorProjectLifecycleCapability.cpp` (new function +
  `<cstdint>` include)
- `task_manager/editor-core-separation-24/PHASE3_COMPLETION_REPORT.md` (this
  file)

No file under `Projects/` or elsewhere was left modified — every temporary
verification artifact (`scratch_phase3/`, the throwaway
`Projects/ProjectAssemblyProbe/Assets/ScreenTintDemoScreenPass.cpp`) was
deleted, and the build tree was reconfigured/rebuilt afterward to confirm a
clean, fully-caught-up state with zero dangling references.
