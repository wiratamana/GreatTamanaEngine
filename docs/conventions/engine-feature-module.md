# Engine Feature Modules — How to Write a Render Feature

This is the convention for adding a **first-party, built-into-the-engine
render feature** — compiled directly into `gte_core.a`/`gte_editor.a`
(same binary as the engine itself), not an out-of-tree `.dll`. **Atmosphere
Scattering** (`src/Features/Atmosphere/`) is the one real, shipped feature
built this way today, and is the worked reference example throughout this
page. This pattern was introduced when Atmosphere was refactored out of
`Core.cpp`/`EditorHost.cpp` into a standalone, self-contained module, and
had ZERO documentation anywhere in this repo until this page was written —
if you are reading `AGENTS.md`/`docs/conventions/` to learn how a feature is
supposed to be structured, this is that page.

**Where this fits among this engine's three ways to add a feature:**

| Mechanism | Lives in | Compiled into the .exe? | Hot reload? | When to use it |
|---|---|---|---|---|
| **Engine Feature Module** (this page) | `src/Features/<Name>/` | Yes, statically, same binary | No (requires a full engine rebuild) | A real, permanent, shipped engine feature everyone gets, e.g. Atmosphere, a future Shadow Map, a future GI system. |
| **Project Assembly System** | `Projects/<Name>/` | No — a separately compiled `.dll`, loaded at runtime | Partially (see its own doc's caveats) | A single developer's own project-specific content/gameplay code, kept out of the engine's own git history. |
| ~~Plugin Architecture (ABI)~~ | ~~`plugins/`~~ | ~~No~~ | ~~No~~ | **REMOVED** by the `better-render-pass-2` campaign. See `docs/conventions/plugin-architecture.md` for the historical record only. |

See `docs/conventions/project-assembly-system.md` for the second row. This
page covers only the first.

## The Core Interface: `IEngineFeatureModule`

`src/Core/Plugins/IEngineFeatureModule.h`:

```cpp
class IEngineFeatureModule {
public:
    virtual ~IEngineFeatureModule() = default;

    // Short, stable, human-readable name - used for logging and as the
    // lookup key BuiltinFeatureEditorPanelRegistry matches an optional panel
    // factory against.
    virtual const char* ModuleName() const = 0;
};

using EngineFeatureModuleFactory = std::unique_ptr<IEngineFeatureModule> (*)(Core&);
```

Every feature implements exactly one class derived from this. It is the
**single object that owns everything feature-specific** — settings, any
GPU-resident state, and every one of the feature's own render-graph pass
registrations. It is constructed exactly once (at `EditorHost` startup,
after `Core` already exists) and destroyed exactly once (at shutdown),
never directly by `Core` itself — `Core` does not even know this mechanism
exists (see "What `Core.cpp`/`EditorHost.cpp` Need to Know" below).

## Self-Registration: `GTE_REGISTER_BUILTIN_FEATURE_MODULE`

A feature never gets constructed by name anywhere in `Core.cpp`/
`EditorHost.cpp`. Instead, it registers a **factory function** for itself,
at static-init time (before `main()` runs), via one macro call at the bottom
of its own `.cpp` file:

```cpp
// AtmosphereFeature.cpp, bottom of file
namespace {
std::unique_ptr<IEngineFeatureModule> CreateAtmosphereFeatureModule(Core& core)
{
    return std::make_unique<AtmosphereFeature>(core);
}
} // namespace

GTE_REGISTER_BUILTIN_FEATURE_MODULE("Atmosphere", &CreateAtmosphereFeatureModule);
```

This works through `src/Core/Plugins/BuiltinFeatureModuleRegistry.h/.cpp`, a
Meyers-singleton (`BuiltinFeatureModuleRegistry::Instance()`) holding a list
of `(debugName, factory)` pairs:

- **`RegisterFactory(debugName, factory)`** — called by the macro above, at
  static-init time. Only appends to the factory list; never touches `Core`,
  never constructs anything (safe to call before `Core` exists, since
  nothing is actually invoked yet). A duplicate `debugName` is refused:
  logged via `GTE_LOG_ERROR` unconditionally, and `assert(false)`'d in debug
  builds — a release build keeps the first registration and silently drops
  the second, rather than crashing in a shipped build.
- **`CreateAll(Core& core)`** — called exactly once, by `EditorHost`, after
  `Core` already exists. Constructs one instance per registered factory, in
  registration order, and returns
  `std::vector<std::unique_ptr<IEngineFeatureModule>>` — the caller
  (`EditorHost`) owns the result; the registry itself only ever keeps the
  factory list. **Every factory call is wrapped in `try`/`catch`** — a
  single misbehaving feature's constructor throwing is logged by name
  (`GTE_LOG_ERROR_BLOCKING`) and skipped; it does **not** take every other
  already-registered feature down with it.

The macro itself (`src/Core/Plugins/BuiltinFeatureModuleRegistry.h`):

```cpp
#define GTE_REGISTER_BUILTIN_FEATURE_MODULE(DebugNameStringLiteral, FactoryFunctionPointer) \
    static ::gte::EngineFeatureModuleAutoRegister \
        GTE_BUILTIN_FEATURE_MODULE_CONCAT(g_gteBuiltinFeatureModuleAutoRegister_, __LINE__)( \
            DebugNameStringLiteral, FactoryFunctionPointer)
```

A static global of type `EngineFeatureModuleAutoRegister`, whose constructor
just calls `RegisterFactory()` — the classic "self-registering factory"
C++ idiom. **The one thing this requires**: the `.cpp` file containing this
macro call must actually be *compiled and linked in*, or the registration
never happens (this is exactly what the CMake auto-discovery section below
guarantees, generically, for every feature folder).

## What `Core.cpp`/`EditorHost.cpp` Need to Know

**Nothing, by name.** Confirmed by reading the actual code:

- `Core.cpp` has **zero** mentions of `BuiltinFeatureModuleRegistry` at all
  — `Core` does not even know this registry exists. Module construction is
  owned entirely by `EditorHost` (or, in a future `PlayerHost`), never by
  `Core` itself.
- `EditorHost.cpp`'s *only* involvement is two fully generic lines, present
  permanently, mentioning no feature by name:
  ```cpp
  m_builtinFeatureModules = BuiltinFeatureModuleRegistry::Instance().CreateAll(m_core);
  m_editorLayer->AttachBuiltinFeatureModules(m_builtinFeatureModules, m_renderer, m_renderGraph);
  ```
- `ImGuiEditorLayer.cpp`'s `AttachBuiltinFeatureModules()` loops over every
  constructed module generically, asks `BuiltinFeatureEditorPanelRegistry`
  if a panel factory exists for that exact `ModuleName()`, and registers it
  into `EditorPanelRegistry` generically if so — zero per-feature code.
- `DockLayout.cpp` docks every entry in
  `EditorPanelRegistry::Instance().PluginPanels()` generically, in a loop —
  the word "Atmosphere" appears there only inside a code *comment* as an
  example; the real code has no per-panel-name branch at all.

**Adding a brand-new feature therefore needs zero lines changed in
`src/Core/*.cpp/.h` or `src/Editor/*.cpp/.h`**, outside the feature's own
`src/Features/<Name>/` folder.

## The Optional Editor-Tier Half: `IEditorPanelModule_v1` + `GTE_REGISTER_BUILTIN_FEATURE_EDITOR_PANEL`

A feature that wants its own dockable Inspector panel (e.g. Atmosphere's
"Atmosphere" panel with its sliders) implements `IEditorPanelModule_v1`
(`src/Core/EditorPanelModule.h` — the same interface the Project Assembly
system and the now-removed Plugin ABI both also implement):

```cpp
class IEditorPanelModule_v1 {
public:
    virtual ~IEditorPanelModule_v1() = default;
    virtual const char* GetPanelName() const = 0;
    virtual void BuildPanel(IPluginPanelDrawContext& ctx) = 0;
};
```

In practice, every real built-in feature panel **ignores the restricted
`ctx` parameter** and calls its own real ImGui-drawing function directly —
this is an established, deliberate convention (see
`AtmospherePluginPanelModule::BuildPanel()`), since a built-in feature's
panel code is compiled into `gte_editor.a` itself and shares the one real
`GImGui` context; `IPluginPanelDrawContext`'s restricted surface exists for
the genuinely cross-DLL-boundary case (Project Assembly), not this one.

Registration mirrors the core half exactly, via
`src/Editor/BuiltinFeatureEditorPanelRegistry.h/.cpp` — a second
Meyers-singleton, keyed by the **same `ModuleName()` string** instead of a
compile-time type:

```cpp
// AtmospherePluginPanelModule.cpp, bottom of file
namespace {
std::unique_ptr<IEditorPanelModule_v1> CreateAtmospherePanelModule(
    IEngineFeatureModule& module, EditorContext& ctx, Renderer& renderer, const rg::RenderGraph& renderGraph)
{
    // Safe: the ONLY IEngineFeatureModule ever registered under the exact
    // name "Atmosphere" is AtmosphereFeature itself.
    return std::make_unique<AtmospherePluginPanelModule>(
        ctx, renderer, renderGraph, static_cast<AtmosphereFeature&>(module));
}
} // namespace

GTE_REGISTER_BUILTIN_FEATURE_EDITOR_PANEL("Atmosphere", &CreateAtmospherePanelModule);
```

`BuiltinFeatureEditorPanelRegistry::TryCreatePanel(module, ctx, renderer,
renderGraph)` looks up `module.ModuleName()` and returns `nullptr` if
nothing was registered under that exact name — **having no Editor panel at
all is the expected, common case**, not an error; a Game-only feature
(e.g. one with no tunable settings) can skip this half entirely and still
work as a full engine feature.

Unlike the core registry's `CreateAll()`, panel construction is **not**
wrapped in `try`/`catch` — a panel failing to construct is treated as a bug
in that panel's own constructor to fix directly, not a runtime condition to
degrade gracefully from.

## Folder Shape Convention

Mirror `src/Features/Atmosphere/` exactly:

```
src/Features/<Name>/
    <Name>Feature.h / .cpp        <- implements IEngineFeatureModule, registers
                                      the GTE_REGISTER_BUILTIN_FEATURE_MODULE line
    <Name>Types.h                 <- settings struct, plain data
    <Name>Math.h / .cpp           <- pure CPU helpers, NO Vulkan types at all -
                                      Tier-1-testable (see AtmosphereMath.h's own
                                      "permanent CPU oracle" precedent)
    Shaders/                      <- THIS feature's OWN .vert/.frag/.comp files
                                      (see "Shader Auto-Discovery" below)
    Editor/
        <Name>PluginPanelModule.h/.cpp  <- OPTIONAL, implements IEditorPanelModule_v1,
                                             registered via
                                             GTE_REGISTER_BUILTIN_FEATURE_EDITOR_PANEL
```

Only `<Name>Feature.h/.cpp` is mandatory. `Editor/` and `Shaders/` are both
entirely optional — a Game-only feature with no shaders of its own (e.g.
one that only reads existing ECS data and republishes it) can omit both.

## CMake Auto-Discovery — Zero Touch, For Real, Verified

**As of 2026-10-06, a brand-new `src/Features/<Name>/` folder needs
LITERALLY ZERO lines added or edited in the root `CMakeLists.txt`, ever —
confirmed by an actual clean build and an actual incremental build, both
succeeding.** This used to not be fully true (each feature needed its own
hand-copied `file(GLOB ...)` block and one `gte_add_shader(...)` line per
shader) — that gap is now closed, generically, for every current and future
feature at once:

1. **Core + Editor sources.** Root `CMakeLists.txt` has one generic loop:
   ```cmake
   file(GLOB GTE_FEATURE_DIRS CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/src/Features/*")
   foreach(GTE_FEATURE_DIR ${GTE_FEATURE_DIRS})
       if(IS_DIRECTORY "${GTE_FEATURE_DIR}")
           file(GLOB GTE_FEATURE_CORE_SOURCES CONFIGURE_DEPENDS
                "${GTE_FEATURE_DIR}/*.h" "${GTE_FEATURE_DIR}/*.cpp")
           target_sources(gte_core PRIVATE ${GTE_FEATURE_CORE_SOURCES})

           if(IS_DIRECTORY "${GTE_FEATURE_DIR}/Editor")
               file(GLOB GTE_FEATURE_EDITOR_SOURCES CONFIGURE_DEPENDS
                    "${GTE_FEATURE_DIR}/Editor/*.h" "${GTE_FEATURE_DIR}/Editor/*.cpp")
               target_sources(gte_editor PRIVATE ${GTE_FEATURE_EDITOR_SOURCES})
           endif()
       endif()
   endforeach()
   ```
   Every `src/Features/*/` directory found gets its `*.h/.cpp` wired into
   `gte_core`, and — only if an `Editor/` subfolder actually exists — its
   `Editor/*.h/.cpp` wired into `gte_editor` too. `CONFIGURE_DEPENDS` means a
   brand-new or deleted feature folder re-runs CMake's own configure step
   automatically before the next build; no manual "re-run cmake" step
   needed.

2. **Shaders.** A second generic loop looks for a subfolder **literally
   named `Shaders`** (locked to this one exact name on purpose, for now —
   not an arbitrary-subfolder wildcard) inside each feature directory:
   ```cmake
   file(GLOB GTE_FEATURE_DIRS_FOR_SHADERS CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/src/Features/*")
   foreach(GTE_FEATURE_DIR_FOR_SHADERS ${GTE_FEATURE_DIRS_FOR_SHADERS})
       if(IS_DIRECTORY "${GTE_FEATURE_DIR_FOR_SHADERS}/Shaders")
           file(RELATIVE_PATH GTE_FEATURE_SHADERS_DIR_RELATIVE
                "${CMAKE_SOURCE_DIR}" "${GTE_FEATURE_DIR_FOR_SHADERS}/Shaders")
           gte_add_shaders_in_dir(GreatTamanaEditor "${GTE_FEATURE_SHADERS_DIR_RELATIVE}")
       endif()
   endforeach()
   ```
   `gte_add_shaders_in_dir(TARGET DIR)` (new helper function,
   `cmake/CompileShaders.cmake`) globs every `*.vert`/`*.frag`/`*.comp`
   directly inside `DIR` and registers each one via the existing
   `gte_add_shader()` — **including auto-detecting `EXTRA_DEPENDS`**, by
   scanning the shader source's own text for `#include "Foo.glsl"` lines,
   **recursively** (so a transitive include — e.g. Atmosphere's own
   `AtmosphereCommon.glsl` itself `#include`-ing the shared
   `VolumetricFroxelMath.glsl` — is tracked correctly, matching what a human
   used to have to type by hand into a manual `EXTRA_DEPENDS` list).
3. **Shared GLSL pool fallback.** `gte_add_shader()`'s own `glslc`
   invocation now also passes `-I "${CMAKE_SOURCE_DIR}/src/Shaders"`, so a
   shader living inside its own feature's `Shaders/` folder can still
   `#include` a genuinely cross-feature, shared `.glsl` file that lives in
   the common `src/Shaders/` pool (checked only *after* the including
   file's own directory, same two-step resolution `glslc` already used).

**Proof this actually works, not just in theory**: Atmosphere's own 8
shader files plus `AtmosphereCommon.glsl` were physically moved from
`src/Shaders/` into `src/Features/Atmosphere/Shaders/` as part of
introducing this mechanism, and a from-scratch `cmake --build` succeeded,
compiling every one of them from their new location with zero manual
`gte_add_shader()` lines left anywhere for Atmosphere.
`VolumetricFroxelMath.glsl` deliberately stayed in the shared `src/Shaders/`
pool, since it is genuinely reused by a second, non-Atmosphere example
shader — not everything a feature's shader touches necessarily belongs
inside that feature's own folder; only files truly exclusive to it do.

## Step-By-Step: Adding a New Feature (worked example: "Shadow")

1. `mkdir src/Features/Shadow` (and `src/Features/Shadow/Editor/`,
   `src/Features/Shadow/Shaders/` if needed).
2. Write `ShadowTypes.h` (settings struct, plain data, no Vulkan types).
3. Write `ShadowMath.h/.cpp` — pure CPU helper functions FIRST, before any
   engine/Vulkan code, mirroring `AtmosphereMath.h`'s own "CPU oracle first"
   precedent. Add a matching `tests/Features/ShadowMathTests.cpp` (Tier 1 —
   see `TESTING.md`).
4. Write `ShadowFeature.h`:
   ```cpp
   class ShadowFeature final : public IEngineFeatureModule {
   public:
       explicit ShadowFeature(Core& core);
       const char* ModuleName() const override { return "Shadow"; }
   private:
       void RegisterPasses();
       Core& m_core;
       ShadowSettings m_settings;
   };
   ```
5. Write `ShadowFeature.cpp` — constructor calls `RegisterPasses()`, which
   registers the feature's own render-graph passes via the engine's already
   public `Core::AddPreOpaquePass()`/`Core::AddPostOpaquePass()`/
   `Core::RegisterProjectRenderFeature()` (see
   `docs/conventions/atmosphere-scattering.md` for the full worked example
   of a real feature's own pass-registration shape — this page intentionally
   does not duplicate that Render Graph API). At the bottom:
   ```cpp
   namespace {
   std::unique_ptr<IEngineFeatureModule> CreateShadowFeatureModule(Core& core)
   {
       return std::make_unique<ShadowFeature>(core);
   }
   } // namespace
   GTE_REGISTER_BUILTIN_FEATURE_MODULE("Shadow", &CreateShadowFeatureModule);
   ```
6. (Optional) Write `Editor/ShadowPluginPanelModule.h/.cpp` the same way,
   ending in `GTE_REGISTER_BUILTIN_FEATURE_EDITOR_PANEL("Shadow", &CreateShadowPanelModule);`.
7. Drop any `.vert`/`.frag`/`.comp` files into `src/Features/Shadow/Shaders/`.
8. Reconfigure (`cmake -S . -B build`) and build
   (`cmake --build build --target GreatTamanaEditor`). **No CMakeLists.txt
   edit needed at any point in this recipe.**

## Known Limitations (stated honestly, not hidden)

- **No hot reload.** A feature module is compiled directly into the same
  binary as the engine — changing its code requires a full engine rebuild,
  unlike the Project Assembly system's separately-compiled `.dll`s.
- **One `ModuleName()` string, globally unique.** A duplicate name between
  two features is refused (logged + asserted in debug, silently keeps the
  first in release) — pick a name as unique and specific as `"Atmosphere"`
  or `"Shadow"`, never something generic like `"Feature"`.
- **Construction-time exception safety only.** `BuiltinFeatureModuleRegistry::CreateAll()`
  wraps each factory call in `try`/`catch`, so a broken constructor cannot
  take down every other feature — but nothing wraps a feature's own
  per-frame render-pass callbacks once registered; a feature's own
  `RegisterPasses()` lambdas must be as careful about not throwing as any
  other render-graph pass callback in this engine.
- **The two CMakeLists.txt loops themselves still exist and must not be
  deleted** — "zero touch" means a *new feature folder* needs no edit, not
  that the generic discovery mechanism maintains itself. If a future change
  needs a THIRD subfolder convention beyond `Editor/`/`Shaders/` (e.g. a
  feature-owned `Tests/` folder), that is a new, deliberate CMake change,
  exactly like this one was — not something "free."

## See Also

- `docs/conventions/atmosphere-scattering.md` — the one real, shipped
  feature built this way; its own worked example for actually registering
  render-graph passes, not just the module-bootstrap mechanism this page
  covers.
- `docs/conventions/project-assembly-system.md` — the other, out-of-tree
  way to add a feature (a separately-compiled `.dll`), for project-specific
  content rather than a permanent engine feature.
- `docs/conventions/volumetric-resources.md` — the generic `VolumeTexture`
  recipe, useful if a new feature module needs a 3D GPU-resident grid.
- `cmake/CompileShaders.cmake` — `gte_add_shader()`/`gte_add_shaders_in_dir()`'s
  own implementation and doc comments.
