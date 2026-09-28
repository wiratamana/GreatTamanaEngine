# Project Assembly System

_Part of [GreatTamanaEngine](../../AGENTS.md)'s contributor conventions. See
[docs/README.md](../README.md) for the full documentation index._

## What this is, and why it exists

A per-developer, single-project, `.gitignore`d source tree
(`Projects/<Name>/`) holding REAL, user-authored C++ and GLSL shader source
that compiles into two ordinary Windows `.dll`s (`<Name>_Game.dll`,
`<Name>_Editor.dll`) which `GreatTamanaEditor.exe` loads at its own startup
and which then call **real, live, non-ABI-wrapped engine types** (`gte::Core&`,
real ImGui, real `rg::RenderGraphBuilder`) directly — with **zero
recompilation of the engine itself** for a content change, and **zero new
per-feature ABI surface** to design/maintain (unlike the existing
`plugins/gte_plugin_abi` system, which this system never touches, edits, or
depends on).

Built by the `editor-core-separation-11` campaign
(`task_manager/editor-core-separation-11/PHASE0_MASTER_STRATEGY.md`, 8
phases, `CAMPAIGN_COMPLETION_REPORT.md`). Two concrete, working, end-to-end
proofs were the campaign's own Definition of Done, both built as one
throwaway-but-permanent test project, `Projects/ProjectAssemblyProbe/`:

1. A real, dockable, genuinely interactive Editor panel ("Probe Panel", a
   click counter) contributed entirely from `ProjectAssemblyProbe_Editor.dll`,
   calling real `ImGui::*` functions directly — no ABI wrapper.
2. A real render-graph compute pass
   (`"ProjectAssemblyProbe.FillTexture"`) contributed entirely from
   `ProjectAssemblyProbe_Game.dll`, using genuine `rg::RenderGraphBuilder`/
   `rg::RenderPipeline` types directly, with its own freshly-compiled `.spv`
   shader — visible in the Editor's real "Render Graph" panel and
   `GET /render_graph`.

## The toolchain prerequisite this whole system depends on

This system is only safe because this repo's ACTIVE toolchain is a
shared-CRT-capable GCC 16.2.0 `mingw-builds-binaries` build
(`scoop/apps/mingw/current`), not the older `scoop/apps/gcc/current` build
(`--disable-shared`) every campaign through `editor-core-separation-9`/
`editor-enchancements-1` ran against. **The switch already happened, quietly,
sometime between `editor-core-separation-9` and 2026-09-28 — nobody
documented a deliberate decision to perform it anywhere under
`task_manager/`.** `editor-core-separation-11`'s own PHASE1 mechanically
re-confirmed (not assumed) that this is still true: `build/CMakeCache.txt`'s
`CMAKE_CXX_COMPILER` points at `scoop/apps/mingw/current/bin/c++.exe`,
`GTE_PLUGIN_SHARED_CRT_TOOLCHAIN_SUPPORTED` reads `TRUE`, and the generated
plugin ABI fingerprint's `sharedRuntimeLinkage` field reads `1`. **A Project
Assembly `.dll` links against `GreatTamanaEditor.exe`'s own import library
and freely passes real `std::string`/`std::vector` values across that
boundary — this is only safe on a shared-CRT toolchain.** No separate
`build-shared-crt` tree exists or is needed; the existing default `build/`
tree already IS the shared-CRT tree.

## Locked Design Decisions (do not silently re-litigate these)

- **A Project Assembly links `GreatTamanaEditor` (the executable's own import
  library) ONLY, never `gte_core`/`gte_editor` directly.** This is what keeps
  exactly one physical copy of every `gte_core`/`gte_editor`/`imgui` global in
  the process.
- **`Projects/` is `.gitignore`d, single-developer, same-toolchain,
  same-build-run only.** Never design anything to survive being zipped up and
  handed to a different machine/toolchain version.
- **No hot reload, anywhere, ever.** A Project Assembly `.dll` is scanned/
  loaded exactly once, at `GreatTamanaEditor.exe` startup. A changed/
  recompiled `.dll` requires a full close+relaunch. No file-watcher, no
  reload button, no `OnBeforeUnload` hook.
- **This is a NEW, ADDITIVE, PARALLEL system.** It never modifies
  `plugins/gte_plugin_abi/`, `src/Core/Plugins/PluginHost.h/.cpp`, or any
  existing `IRenderFeatureModule_*`/`IEditorPanelModule_v1` ABI type.

## Folder layout and the CMake mechanism

```
Projects/<Name>/
  Assets/
    *.cpp, *.vert, *.frag, *.comp   (compiled into <Name>_Game.dll)
    Editor/
      *.cpp                        (compiled into <Name>_Editor.dll, only if this folder has any source)
  Libraries/
    CMakeLists.txt                 (one line: gte_add_project(<Name>))
    ProjectAssemblyExports.h        (copy of cmake/templates/ProjectAssemblyExports.h)
```

`gte_add_project(<Name>)` (`cmake/GteProject.cmake`):
- Globs every `.cpp` under `Assets/` (`CONFIGURE_DEPENDS`, `Editor/` handled
  as its own separate glob) — never a hand-maintained source list.
- Builds `<Name>_Game` unconditionally; builds `<Name>_Editor` only if
  `Assets/Editor/` has any source at all.
- Calls `gte_add_project_shaders()` for each target's own `Assets/*.vert/.frag/.comp`
  (a thin wrapper around the engine's own, completely unmodified
  `gte_add_shader()`, `cmake/CompileShaders.cmake`).
- Propagates every header search path `gte_core`/`gte_editor` themselves
  declare `PUBLIC`, via explicit
  `$<TARGET_PROPERTY:<dep>,INTERFACE_INCLUDE_DIRECTORIES>` generator
  expressions — needed because `GreatTamanaEditor` links `gte_editor`
  `PRIVATE` (root `CMakeLists.txt`), so ordinary CMake usage-requirement
  propagation does not flow through to a Project Assembly target linking
  only `GreatTamanaEditor`.
- For an `_Editor` target only, also calls
  `gte_project_assembly_apply_editor_header_paths()` — a headers-only
  `$<TARGET_PROPERTY:imgui,INTERFACE_INCLUDE_DIRECTORIES>`/`imguizmo`
  propagation, `PRIVATE`, so `_Editor.dll` gets real ImGui headers WITHOUT
  re-linking the compiled `imgui`/`imguizmo` static libraries a second time
  (which would create a second, independent `GImGui` context pointer in the
  process — the exact hazard this whole system exists to avoid).
- Applies `gte_apply_project_assembly_shared_crt_linkage(target)`
  (`cmake/MingwRuntime.cmake`) — a genuinely SEPARATE function from
  `gte_apply_plugin_*_shared_crt_linkage()` (the OTHER, unrelated
  `gte_plugin_abi` system's own CRT-linkage helper), gated ONLY by
  `GTE_ENABLE_PROJECT_ASSEMBLIES` + `GTE_PLUGIN_SHARED_CRT_TOOLCHAIN_SUPPORTED`
  — never by `GTE_ENABLE_PLUGINS`. This exists because the two systems are
  logically independent: a developer who sets `GTE_ENABLE_PLUGINS=OFF` (to
  disable the OTHER, ABI-based plugin system) must still get correct,
  shared-CRT-linked Project Assembly `.dll`s.

**One new CMake option, `GTE_ENABLE_PROJECT_ASSEMBLIES` (default `ON`)**,
gates the entire system end to end: both the root `CMakeLists.txt`
auto-discovery loop (`add_subdirectory()` over every `Projects/*/Libraries/`)
and the runtime `Core::LoadProjectAssemblies()` call site
(`src/Editor/EditorHost.cpp`'s own constructor).

**Known CMake gap, not yet fixed**: an incremental, shader-only, target-scoped
rebuild (`cmake --build build --target <Name>_Game`, editing only a `.comp`/
`.vert`/`.frag` file) does not reliably re-stage the compiled `.spv` next to
the `.dll` — the missing link is `gte_add_shader()`'s own `target_sources()`
call wiring the compiled shader in only as an ORDER-ONLY prerequisite, never a
real staleness-tracked link input, for a `SHARED` library target whose
runtime output directory differs from `CMAKE_BINARY_DIR` (true for a Project
Assembly, never true for `GreatTamanaEditor` itself, which is why this gap
was invisible until this campaign). Work around it by also touching/rebuilding
the target's own `.cpp` (forces a real relink, which reliably re-runs the
`POST_BUILD` staging copy), or by doing a full rebuild after any shader edit.

## The runtime loader

`src/Core/Plugins/ProjectAssemblyHost.h/.cpp` — modeled directly on
`src/Core/Plugins/PluginHost.h/.cpp`'s own shape:

- `LoadProjectAssemblies(outputDirectory, core, editorHost)` enumerates every
  regular file directly inside `<exe dir>/project_assemblies/` (no
  recursion), safe on a non-existent directory.
- Filters by filename suffix (`_Game.dll`/`_Editor.dll`, everything else
  silently ignored — never introspects the export itself).
- `LoadLibraryW()`s each match, resolves the ONE fixed export
  `GTE_RegisterProject` through one of TWO different function-pointer
  signatures depending on which suffix matched
  (`cmake/templates/ProjectAssemblyExports.h`'s
  `GTE_DEFINE_PROJECT_EXPORTS_GAME(fn)` → `void(gte::Core&)`;
  `GTE_DEFINE_PROJECT_EXPORTS_EDITOR(fn)` → `void(gte::Core&, gte::EditorHost&)`),
  calls it exactly once.
- An `_Editor.dll` found while `editorHost == nullptr` (a Player-shaped host
  that never constructs one) is skipped with a loud `GTE_LOG_WARNING`, never
  crashed on.
- Keeps every `HMODULE` alive forever — never `FreeLibrary()`'d except on the
  "declined/invalid" early-return paths where the export was never called
  (LDD4 — no hot reload, ever).

`Core::LoadProjectAssemblies()` is a thin public pass-through into
`m_projectAssemblyHost`, mirroring `Core::LoadPlugins()`'s own identical
shape (a private member, a public one-line forwarding method).
`EditorHost.cpp`'s constructor calls it once, gated by
`#if GTE_ENABLE_PROJECT_ASSEMBLIES` at that call site only (the method itself
always compiles).

## The "Compile" build trigger

`src/Core/Plugins/ProjectAssemblyBuildRunner.h/.cpp` —
`TriggerProjectAssemblyCompile(projectName, buildDirectory)` runs a genuine
`cmake --build` child process (`CreateProcessW()`) on its own dedicated
`std::thread`, registered via `JobSystem::RegisterBackgroundThread()` —
**never** `Schedule()`, which would tie up a fixed worker-pool thread for the
build's entire, potentially multi-minute duration. Builds `<Name>_Game`
first; only attempts `<Name>_Editor` if that succeeded, and a missing
`_Editor` target (a project with no `Editor/` sources) is detected via a
best-effort keyword heuristic on Ninja's own error text and treated as
success, not failure. Streams the child's combined stdout/stderr into
`GTE_LOG_INFO`/`WARNING`/`ERROR` (category `"ProjectAssemblyBuild"`) as it
runs. A simple per-project in-flight guard turns a second concurrent trigger
for the SAME project into a harmless, logged no-op. The final log line
(`"Build finished with exit code N — relaunch GreatTamanaEditor.exe to use
the result..."`) is emitted on every run, success or failure — no permanent
UI trigger point (menu item/button) is defined by the campaign that built
this; a future UI surface calls `TriggerProjectAssemblyCompile()` directly.

**Known, permanent limitation, not a bug**: a Project Assembly `.dll` already
`LoadLibraryW()`'d by the CURRENTLY RUNNING instance can never be
successfully recompiled by that same instance — Windows locks a mapped DLL
image against being overwritten by the linker (`ld.exe: cannot open output
file ...: Permission denied`). The realistic workflow is: edit source, close
the Editor (releases the lock), rebuild (externally, or via a future
"Compile" UI), relaunch.

## Capability #1 — a custom Editor panel

An `_Editor.dll`'s own registration function implements
`gte::IEditorPanelModule_v1` directly (the SAME ABI interface
`gte_plugin_abi` panels implement — deliberately reused, since this system
has no reason to invent a parallel one), **ignoring** the `ctx`
(`IPluginPanelDrawContext&`) parameter entirely and calling real `ImGui::*`
functions directly instead, then registers itself through the existing,
unmodified `gte::EditorPanelRegistry::Instance().RegisterPluginPanel(name, module)`.
No new registry, no new panel-hosting mechanism.

**Finding G, resolved**: `src/Editor/ImGuiEditorLayer.cpp`'s per-frame loop
that calls `entry.module->BuildPanel(drawContext)` for every
`EditorPanelRegistry::PluginPanels()` entry used to sit inside
`#if GTE_ENABLE_PLUGINS` only — the OTHER system's flag. Widened to
`#if GTE_ENABLE_PLUGINS || GTE_ENABLE_PROJECT_ASSEMBLIES` so a Project
Assembly panel actually draws even with `GTE_ENABLE_PLUGINS=OFF` (it still
gets a dock slot either way — `src/Editor/DockLayout.cpp`'s own separate loop
over the same registry was never gated by either flag — but without this fix
it would be a permanently blank tab, which looks like a bug rather than a
configuration choice).

## Capability #2 — a custom render-graph pass

**Finding B, resolved**: `Core::RegisterOffscreenRenderPipelineProviders()`
is `private`, and `Core` exposed no public method to register a new provider
from outside `Core` at all. Fixed with a new, public thin pass-through:

```cpp
// Core.h (public section)
void RegisterProjectRenderPassProvider(const char* debugName, rg::ProviderScope scope, rg::RenderPassProvider provider);

// Core.cpp
void Core::RegisterProjectRenderPassProvider(const char* debugName, rg::ProviderScope scope, rg::RenderPassProvider provider)
{
    m_offscreenRenderPipeline.Register(debugName, scope, std::move(provider));
}
```

`m_offscreenRenderPipeline` is the correct target — the SAME pipeline every
production Game-View/Scene-View pass (`"AtmosphereSharedLut"`,
`"GpuSkinning"`, `"RenderOpaque"`, ...) registers onto;
`m_presentRenderPipeline` is a separate, narrower pipeline used only for the
one `"Present"` swapchain-blit provider. Safe to call any time after `Core`'s
own constructor has run (a Project Assembly's `GTE_RegisterProject` runs from
`EditorHost.cpp`'s constructor body, strictly after `Core`'s own
construction) — `RenderPipeline::Register()` merely appends to an internal
`std::vector` read fresh, in full, every frame by `DeclareInto()`.

**Finding E, resolved**: `RenderGraphBuilder::PassBuilder` (the type a
deferred `RenderPassDesc::setup` receives) has NO `CreateTexture()`/
`ImportTexture()` method — only `ReadTexture()`/`WriteColorAttachment()`/
`WriteTexture()`/`ReadBuffer()`/`WriteBuffer()`/etc., all operating on an
ALREADY-MINTED handle. `CreateTexture()`/`CreateBuffer()`/`ImportTexture()`
are methods of `RenderGraphBuilder` ITSELF. The correct pattern: call
`frame.builder.CreateTexture(name, desc)` directly inside the provider's own
lambda body (available because `RenderPassFrameContext::builder` is a plain
reference member, freely callable through a `const RenderPassFrameContext&`),
obtain the resulting `TextureHandle` BEFORE constructing the
`RenderPassDesc`, then reference that already-minted handle from
`desc.setup`/`desc.execute` via closure capture. Append any transient handle
nobody else reads to `frame.finalTextureOutputs` so
`RenderGraphCompiler`'s backward-reachability culling scan does not silently
drop the pass that writes it.

**Finding F, resolved**: a Project Assembly's own compiled shader is
referenced as a plain, bare relative path string,
`"project_assemblies/shaders/<Name>.spv"` — the exact same convention every
existing internal `ComputePipeline` construction call site already uses
(`Renderer::CreateComputePipeline()`, never a raw `ComputePipeline`
constructor call, since `Renderer` exposes no public `VkDevice` getter —
`Core::GetRenderer().GetVulkanContextInfo().device` is the one legitimate way
to obtain one).

`ProjectAssemblyProbe_Game.dll`'s own `HelloGame.cpp` is the full, working,
concrete example of all three Findings combined: it mints a transient
256x256 texture, lazily builds a `ComputePipeline`/`ComputeDescriptorSet` on
first use (a process-lifetime `std::optional<gte::ComputePipeline>` — never
`std::unique_ptr`, matching every other real call site in this codebase), and
dispatches `ProbeCompute.comp` (a solid-color fill shader,
`local_size_x/y = 16`, matching `ComputeGroupCount(256, 16) == 16` on both
axes) against it every frame — confirmed, live, visible in both the Editor's
real "Render Graph" panel and `GET /render_graph`'s JSON.

### On-screen Game View compositing — investigated, confirmed NOT safe today

A Project Assembly has real, direct access to
`Core::GetGameViewTargetThisFrame()` (public, confirmed already resolved by
the time any provider runs, same frame). One might expect
`ImportTexture()`-ing that same `RenderTexture*` a second time, from a
Project Assembly's own pass, would let it write directly into the on-screen
Game View with no compositor needed. **This is NOT safe as this engine's
`RenderGraph` exists today, and is a confirmed, structural gap, not merely an
untried idea**: `RenderGraph::EnsureTextureResolved()` tracks resource state
PER `TextureHandle`, never per underlying physical resource. Two INDEPENDENT
`ImportTexture()` calls against the exact same physical `RenderTarget`/
`VkImage` mint TWO completely separate `PhysicalTexture` tracking slots, each
independently seeded — `RenderGraphCompiler`'s dependency-graph construction
(the RAW/WAW edges the barrier planner relies on) is built purely from
`TextureHandle` IDENTITY, and no mechanism anywhere in this codebase today
tells the compiler "this newly-imported handle is really the same physical
resource as that OTHER already-tracked handle." Concretely: a second
import/write against an already-imported `RenderTarget` gets NO automatic
`VkImageMemoryBarrier` against the engine's own internal Game-View-compositing
chain's reads/writes of the identical physical image. **Do not attempt this**
until a future campaign adds real handle-aliasing support to
`RenderGraphBuilder`. The honest Definition of Done for this system's
render-graph capability is "the pass is real and visible in the Render Graph
panel/HTTP endpoint" — never on-screen compositing.

## What this system does NOT do (explicit Non-Goals)

- No gameplay/"MonoBehaviour"-style scripting bridge (per-entity
  `Update()`/`Start()`, ECS component authoring, Input access).
- No hot reload, ever.
- No cross-machine/cross-checkout portability — `Projects/` is
  `.gitignore`d, single-developer, same-build-run only.
- No separate Player executable — both `_Game.dll`/`_Editor.dll` load into
  the one existing `GreatTamanaEditor.exe` process.
- No change of any kind to `plugins/gte_plugin_abi`, `PluginHost`, or any
  existing ABI-versioned interface.
- No scaffolding/"New Project" wizard tool — a human creates
  `Projects/<Name>/{Assets,Libraries}` by hand today.
- No UI-design decision for where a permanent "Compile" button/menu item
  lives — `TriggerProjectAssemblyCompile()` is the mechanism; a future
  campaign decides the UI.
- No resolution of every possible future render-graph capability (reading
  `SceneDepth`, multiple render targets, cross-Project-Assembly-pass
  chaining, a safe way to alias an imported handle onto an already-tracked
  physical resource, on-screen Game View compositing — see above).

Full campaign writeup: `task_manager/editor-core-separation-11/PHASE0_MASTER_STRATEGY.md`,
each `PHASEn_COMPLETION_REPORT.md` in that same folder, and
`CAMPAIGN_COMPLETION_REPORT.md`.
