# Render feature authoring (`gte::fx`, the feature kit)

A render feature is written against `src/Core/FeatureKit/FeatureKit.h`: Vulkan-free, handle based. Feature code
(everything under `src/Features/<Name>/` except `Editor/`) should include only the kit, `Math/`, `ECS/`,
`Core/Extent.h`, `Core/PixelFormat.h` and `Core/Logging.h`. FeaturePurityCheck lists every file that still breaks
this; the list only shrinks.

## What exists

| Piece | Purpose |
|---|---|
| `fx::Feature`, `fx::FeatureHost` | Base class and narrow engine facade (`Scene()`). |
| `fx::FeatureSetup` | `PreOpaque`, `PostOpaque`, `PostTransparent`, `PostComposite`, `PreUI`, `PreOpaqueShared`. Verbs only collect. |
| `fx::Gate` | Pass names a registration answers to in the toggle panel (default: the registered name). |
| `fx::View`, `fx::Frame` and typed frames | `PreOpaqueFrame`, `PostOpaqueFrame` (scene color and depth), `CompositeFrame` (private target, extent), `SharedFrame`. |
| `fx::Texture` | Graph handle stamped with the frame serial; valid for one callback. |
| `GTE_REGISTER_RENDER_FEATURE("Name", Class)` | Registers a built-in feature module. `fx::FeatureOf<T>(module)` reaches it from editor code. |

Setup is atomic: `Setup()` only collects. If it throws, or any registration is invalid (empty name, name over
63 bytes, duplicate name, missing callback), nothing is registered. Callbacks of a destroyed module do nothing
(`WhileAlive`), because Core cannot unregister a built-in pass.

The gate runs before the callback, so a disabled pass runs no side effect. Per-view callbacks are skipped for a view
the engine has no data for; `PreOpaqueShared` runs once per frame.

## Not built yet

The verbs that let a feature declare passes (`Compute`, `Fullscreen`, `SceneDraw`, `Blit`, `Keep`,
`PublishToScene`, `SetSceneUniforms`), the resource types (`PerView`, `PersistentTarget`, `GpuParams`,
`OffscreenTarget`, `Channel<T>`, `SceneSlot`) and the shader wrappers (`ComputeShader`, `FullscreenShader`,
`SceneShaderSet`). Until they exist a kit feature can read its frame but cannot yet draw; the built-in features
still use the engine pass API directly (see `render-pass-api.md`).

## Adding a verb

If a feature needs something the kit lacks, add a verb to the kit. Never hand a feature a raw handle or a
`Native()` escape.

For the human: add a summary link to this page and to `render-pass-api.md` in `AGENTS.md` (AI agents must not edit
that file).
