# Writing a render feature
> Step-by-step guide to add a built-in render feature to the engine. For engine developers.
> Verified against code: 2026-10-30

## TL;DR
- A feature is a folder `src/Features/<Name>/`, a class derived from `fx::Feature`, and one macro line.
- No edit in CMake, `Core` or `Editor`. The root `CMakeLists.txt` finds the folder by itself.
- Feature code uses only the kit (`gte::fx`). No Vulkan, no `Renderer`, no raw handles.
- Eight features ship today. `ToneMapping` is the smallest. Copy it.
- A feature is compiled into the engine. There is no hot reload. Rebuild after each change.

## Before you start

| Way to add code | Lives in | Loaded how | Use it for |
|---|---|---|---|
| Built-in feature (this guide) | `src/Features/<Name>/` | Compiled into `gte_core` | A permanent engine feature |
| Project assembly | `Projects/<Name>/` | Separate DLL | Your own game code. See [Project assembly](project-assembly.md) |

Primary view means the Game view. Secondary view means the Scene view. Code says `Primary` and `Secondary`.

| Feature code may include | Feature code must not use |
|---|---|
| `Core/FeatureKit/`, `Math/`, `ECS/`, `Core/Extent.h`, `Core/PixelFormat.h`, `Core/Logging.h`, `Game/Lighting/` | `Renderer/`, `Core/Core.h`, `Game/Game.h`, `Game/SceneQuery.h`, `Game/RenderSystem.h`, `Application/`, any `Vk*` name |

- The `Editor/` subfolder is not scanned by `FeaturePurityCheck`. It may use ImGui, but never Vulkan. See [AGENTS.md](../../AGENTS.md#vulkan-boundary-strict).
- A feature never includes another feature's header. Shared plain records live in `src/Game/Lighting/`.

## How a feature runs

```text
FooFeature.cpp: GTE_REGISTER_RENDER_FEATURE("Foo", FooFeature)
      | static init: a factory is added to BuiltinFeatureModuleRegistry
      v
EditorHost start: CreateAll(core)   (a factory that throws is logged and skipped)
      v
module: Setup() collects registrations -> committed only if Setup() had no error
      v   Core::RegisterRenderPass(stage, name, priority, callback)
RenderFeatureCompositor: per stage, sorted by priority (lower runs first)
      v   every frame, for each active view
gate check -> view check -> feature scope opened -> callback(frame)
      v
callback declares passes with verbs: f.Compute, f.Fullscreen, f.SceneDraw ...
      v
render graph: order, cull, barriers, record, submit
```

| Stage | Frame type | Runs | Typical use |
|---|---|---|---|
| `PreOpaque` | `PreOpaqueFrame` | Per view, before geometry | Shadow maps, LUTs, uniforms |
| `PreOpaqueShared` | `SharedFrame` | Once per frame, no view | Data every view needs |
| `Opaque` | `OpaqueFrame` | Per view, after `RenderOpaque`. Writes color and depth | GPU-driven draw |
| `PostOpaque` | `PostOpaqueFrame` | Per view, after opaque | Sky, AO, volumes |
| `PostTransparent` | `PostOpaqueFrame` | Per view, after transparent | Available, no feature uses it |
| `PostComposite` | `CompositeFrame` | Per view, private target blended in | Final screen effects |
| `PreUI` | `CompositeFrame` | After `PostComposite` | Available, no feature uses it |

## Steps

### 1. Make the folder
```text
src/Features/Foo/
  FooTypes.h                settings struct, status enum, GPU blocks
  FooMath.h / .cpp          pure CPU code: sanitizer, packing
  FooFeature.h / .cpp       the fx::Feature class and the macro
  FooDebugEndpoint.h / .cpp optional, HTTP debug
  Shaders/                  Foo.comp ... (the folder must be named Shaders)
  Editor/                   optional panel
```
- `*.h` and `*.cpp` directly in the folder go to `gte_core`. `Editor/*` goes to `gte_editor`.
- `Shaders/*.vert|frag|comp` are compiled by `glslc`. Other subfolders are not picked up.
- Only `FooFeature.h/.cpp` is required.

### 2. Write the types and the CPU math first
Put settings, status enum and snapshot in `FooTypes.h`. GPU blocks need `static_assert(sizeof(...))` and `is_trivially_copyable`.
Write `SanitizeFooSettings()` and the packing code in `FooMath`. The shader is copied from the CPU code. If they differ, the CPU code is right.

### 3. Write the class
```cpp
class FooFeature final : public fx::Feature {
public:
    explicit FooFeature(fx::FeatureHost& host);
    const char* Name() const override { return kFooFeatureName; }
    void Setup(fx::FeatureSetup& setup) override;
    fx::IFeatureDebugEndpoint* DebugEndpoint() noexcept override { return &m_debugEndpoint; }
    void ApplySettings(const FooSettings& candidate); // sanitizes, main thread only
private:
    void CompositePass(fx::CompositeFrame& f);
    FooSettings m_settings;
    fx::PerView<FooViewState> m_views;
    fx::ComputeShader m_shader{ "Foo" };  // loads shaders/Foo.comp.spv
    fx::ViewOutput m_output{ "Foo" };
};
```
- Features live in namespace `gte`. The constructor takes `fx::FeatureHost&`. `host.Scene()` gives the ECS registry.
- Kit resources (`PersistentTarget`, `ViewOutput`, `ComputeShader`) are members. They cannot be copied or moved.
- Change settings only through `ApplySettings()`. It must call the sanitizer.

### 4. Register the passes in `Setup()`
```cpp
void FooFeature::Setup(fx::FeatureSetup& s)
{
    s.PreOpaque("Foo.FrameStamp", 40, [this](fx::PreOpaqueFrame& f) { m_newestFrame = f.FrameIndex(); },
        fx::Gate::Always());
    s.PostComposite("Foo", 500, fx::Blend::Replace, [this](fx::CompositeFrame& f) { CompositePass(f); },
        fx::Gate{ "Foo.Dispatch", "Foo.CopyToPrivateTarget" });
}
```
- A registration name has at most 63 bytes. It must be unique across all features and all stages.
- An empty name or a missing callback fails the whole `Setup()`. Then nothing is registered.
- Inside one stage, lower priority runs first.
- A `Gate` lists the real pass names the callback declares. The callback runs if any of them is enabled.
- Use `Gate::Always()` for work that is not a render pass (a frame stamp, a uniform write). A toggle never silences it.
- Call `s.DefaultSecondaryViewEnabled(false)` before a registration to start it off in the Scene view.

### 5. Write the callback
```cpp
void FooFeature::CompositePass(fx::CompositeFrame& f)
{
    if (!f.PassEnabled("Foo.Dispatch") || !f.PassEnabled("Foo.CopyToPrivateTarget")) { return; }
    if (!f.ShaderReady(m_shader)) { return; }
    const fx::Texture out = m_output.Acquire(f, f.Extent(), PixelFormat::RGBA8_UNorm);
    if (!out.IsValid()) { return; }
    const bool ok = f.Compute(m_shader, "Foo.Dispatch")
                        .Push(MakeFooPush(m_settings, f.FrameIndex()))
                        .Bind("sourceColor", f.SceneColor())
                        .Bind("destinationImage", out)
                        .Dispatch(f.Extent());
    if (!ok) { f.ReportOnce("Foo.Dispatch was refused"); return; }
    f.Blit("Foo.CopyToPrivateTarget", out, f.PrivateTarget(), fx::BlitFilter::Linear);
    f.ExposeAsViewOutput(m_output, out);
}
```
1. Ask `f.PassEnabled()` before any `Acquire()`, resize or other side effect.
2. Every early return declares nothing. If you drop history, reset it.
3. Never throw. Use `f.ReportOnce()`. A throw is caught, logged once, and the callback's work is dropped.
4. Publish a channel or expose a view output only after the pass was declared.
5. Pass names are string literals. A runtime string does not compile.
6. End every chain with its terminal call (`Dispatch`, `Draw`, `Record`).

### 6. Add the macro
At the bottom of `FooFeature.cpp`, inside `namespace gte`:
```cpp
GTE_REGISTER_RENDER_FEATURE("Foo", FooFeature);
```
- The name must be unique. A duplicate is refused at start-up: an error log, and `assert` in debug.
- Use the same string in the macro and in `Name()`. The macro name finds the editor panel. `Name()` is the owner of passes, channels and debug keys.

### 7. Write the shaders
- Binding names in GLSL are the contract with C++. `Bind("sourceColor", ...)` needs a variable `sourceColor`.
- Mark storage blocks `readonly` or `writeonly`. No qualifier means read-write and costs one more barrier.
- A push-constant block or a `params` block must match the C++ struct size. Use `static_assert(sizeof(...))`.
- `ComputeShader{"Foo"}` loads `Foo.comp`. `FullscreenShader{"Foo"}` loads `Foo.vert` and `Foo.frag`.
- `#include` finds files in the shader's own folder first, then in `src/Shaders/`.

### 8. Add a debug endpoint (optional)
Implement `fx::IFeatureDebugEndpoint`: `GetState()`, `Set()` and `Invoke()`. All run on the main thread.
- `Set()` applies all arguments or none. A bad key goes in `badKey` and the route answers 400.
- Read-only per-view keys start with `game.` or `scene.`. Use `reset_defaults` as the reset action.
- Use `ParseDebugBool`, `ParseDebugUnsigned`, `ParseDebugFloat` from `FeatureDebug.h` to parse values.
- Routes: `/feature_debug/{list,state,set,invoke}?name=Foo`. See [HTTP cheat sheet](../debugging/http-cheat-sheet.md).

### 9. Add an editor panel (optional)
In `Editor/`, write a class derived from `IEditorPanelModule_v1` (`GetPanelName`, `BuildPanel`). Register it with the same name as the feature:
```cpp
GTE_REGISTER_BUILTIN_FEATURE_EDITOR_PANEL("Foo", &CreateFooPanelModule);
```
- The factory gets `(IEngineFeatureModule&, EditorContext&, Renderer&, const rg::RenderGraph&)`.
- Reach your feature with `fx::FeatureOf<FooFeature>(module)`. It throws `std::logic_error` for a wrong type.
- Built-in panels ignore `ctx` and call their own ImGui code. The editor docks every registered panel.
- A panel that throws in its constructor is not caught. Fix the panel.

### 10. Guard the side channels
If a channel or scene slot of yours is read by another pass that can be toggled alone, add a rule in `KnownRiskBlackboardKeyRules()` (`src/Editor/FrameDebuggerSideChannelChecker.cpp`). Use `AddChannelRules()` per view, or `AddSharedChannelRule()` for a Shared publish. See [Render honesty](../conventions/render-honesty.md).

## Common changes

| I want to... | Do this |
|---|---|
| Add a pass to my feature | New literal name. Add it to the registration's `Gate`. New verb chain, with `PassEnabled` first. If it publishes data, add a keep list and a risk rule |
| Share data with another feature | Put a plain record in `src/Game/Lighting/`. Publish with `fx::Channel<T>`. Read with `fx::ChannelReader<T>` |
| Let material shaders read my texture | `s.DeclareSceneSlot(name, kind, index)` in `Setup()`, then `f.PublishToScene(slot, tex)` each frame |
| Add a kit verb | New small `.cpp` in `src/Core/FeatureKit/`. Never hand a feature a raw handle |
| Need a GPU feature the kit lacks | Add it inside `src/Renderer/` behind a Vulkan-free interface |
| Use a 3D texture | See [Volumetric resources](../conventions/volumetric-resources.md) |

## Check it worked

```cmd
cmake --build build --target GreatTamanaEditor
ctest --test-dir build -R "FeaturePurityCheck|VolkLeakCheck|NativeLeakCheck|HeaderPurity|EngineVocabularyCheck|AtmosphereCoreDecouplingCheck" --output-on-failure
curl.exe "http://127.0.0.1:8080/feature_debug/list"
curl.exe "http://127.0.0.1:8080/get_logs?category=FeatureKit"
```
- `/feature_debug/list` shows your feature if it has a debug endpoint.
- Logs of categories `RenderFeatureCompositor` and `FeatureKit` must stay empty, in Debug and Release.
- Add Tier-1 tests for `FooMath`. Pin shader constants to the GLSL text.
- Check your passes in the Render Graph panel. Toggle each one and look at the Frame Debugger. See [Render honesty](../conventions/render-honesty.md).

## Troubleshooting

| Symptom | Likely cause | Fix |
|---|---|---|
| Feature does not appear | `.cpp` not in the folder root, or no macro | Move the file. Add `GTE_REGISTER_RENDER_FEATURE` |
| Error log `BuiltinFeatureModuleRegistry` | Duplicate name, or the factory threw | Rename. Read the message. It names the feature |
| Feature registered but no passes | `Setup()` recorded an error | Check name length, duplicates, empty callback |
| Pass never runs | Name not in the `Gate`, or the pass is toggled off | Fix the `Gate`. Check `/render_graph` |
| Pass silently disappears | Its output is read by nobody, so the graph culled it | Acquire through the kit (it keeps the handle) or call `f.Keep(tex)` |
| Shader not found | File not in `Shaders/`, or wrong name | Folder must be `Shaders`. Rebuild. `glslc` must be on PATH |
| Second `Compute` with same name is dropped | Same literal pass name twice in one view and frame | Give each call its own name |
| `FeaturePurityCheck` fails | Banned include or name | Use the kit. Do not edit `tokens.txt` or the allowlist |

## Related
- [Feature kit reference](../reference/feature-kit.md) — every verb and resource type
- [Render features](../reference/render-features/README.md) — the eight shipped features
- [Render graph](../architecture/render-graph.md) — pass kinds, toggles, pools
- [Render honesty](../conventions/render-honesty.md) — toggle and Frame Debugger rules
- [Volumetric resources](../conventions/volumetric-resources.md) — 3D textures
- [HTTP cheat sheet](../debugging/http-cheat-sheet.md) — debug your feature live
- [FeaturePurityCheck](../../tools/ci/feature_purity_check/README.md) — the purity rules
