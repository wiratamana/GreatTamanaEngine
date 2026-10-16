# Scene-Resource Publishing (`SceneResourceKeyOf()` and `fx::Channel`)

A pass hands a resource to a later pass through the frame's `RenderPassBlackboard`
(`Publish()` / `Fetch()` under an `rg::RenderPassId` key). Two helpers build the key.

| Helper | Where | Use it for |
|---|---|---|
| `fx::Channel<T>` | `src/Core/FeatureKit/Channel.h` | Feature code. A typed hand-off between passes of one feature. |
| `rg::SceneResourceKeyOf(name, view)` | `src/Renderer/RenderGraph/RenderPipeline.h` | Engine code only. A pure hash of `(name, view)`; no registration. |

Feature code does not call `SceneResourceKeyOf()` or `fx::ChannelKey()` directly.
Use `fx::Channel<T>`.

## Channels

`fx::Channel<T>` builds its key with `fx::ChannelKey(feature, name, view)`
(`src/Core/FeatureKit/ChannelKey.h`), which hashes `"<Feature>.<name>"` plus the view
through `SceneResourceKeyOf()`.

```cpp
fx::Channel<ShadowMapInfo> shadowMap{"Map"};

// Producer pass callback:
shadowMap.Publish(frame, info, {depthTexture}); // textures in the list stay alive

// Consumer pass callback, same frame:
if (const std::optional<ShadowMapInfo> map = shadowMap.Fetch(frame)) { /* use *map */ }
```

- The key uses the frame's own view. A value published for the Game view is not seen by the Scene view.
- A value published by a `Shared` callback is visible to every view (`Fetch()` falls back to the Shared key).
- `Publish()` and `Fetch()` outside a render callback do nothing and log once.
- Fetching with another `T` than was published returns nothing and logs once.
- Two channel names of one feature with the same key hash assert in debug and log once in release.
- A disabled pass never runs its callback, so a disabled producer publishes nothing.

## Engine code without the kit

Publish and fetch under one key, built with the view of the current callback:

```cpp
blackboard.Publish<rg::TextureHandle>(rg::SceneResourceKeyOf("Owner.Name", view), handle);
const auto value = blackboard.Fetch<rg::TextureHandle>(rg::SceneResourceKeyOf("Owner.Name", view));
```

Never reuse a view from an earlier call: two views publishing under one key overwrite each other silently.

## Channel or scene-service slot?

`SceneServiceBlackboardKey(slot, view)` (`src/Renderer/SceneServicesDescriptorSet.h`) serves a
different need: at most `kSceneServiceSlotCount` (8) fixed slots, each sampled by shaders at a
durable `layout(set = 1, binding = N)`. Use a slot when a shader must read the value.
Use a channel for everything else. Do not grow a channel into a per-material or high-volume table.

## Side-channel honesty

`Publish()` is a real side effect, so [`render-pass-side-channel-honesty.md`](render-pass-side-channel-honesty.md)
applies. When a toggleable pass publishes a channel that another toggleable pass reads, add a rule for
it to `KnownRiskBlackboardKeyRules()` in `src/Editor/FrameDebuggerSideChannelChecker.cpp`
(`AddChannelRules()` per view, `AddSharedChannelRule()` for a Shared publish). The checker only knows keys
that are listed there.
