# Temporal anti-aliasing

> Smooths stair-stepped edges by mixing frames with a sub-pixel camera shift. For anyone who tunes edge quality, or sees shimmer, ghosts or softness.
> Verified against code: 2026-11-04

## TL;DR
- Feature `TemporalAa` in `src/Features/TemporalAa/`. Game view (Primary) only. The Scene view is not supported.
- Each frame the draw projection is shifted by a tiny jitter (Halton 2,3). A compute pass at `PostComposite` -150 blends the frame with the history.
- It runs on linear HDR, after Atmosphere, Shadow, Gtao, Ssgi and VolumetricLight, and before Bloom and ToneMapping.
- History is moved with depth and the unjittered camera matrices. A neighborhood color clip and a depth check cut bad history.
- Off means off: jitter is exactly 0, no pass is declared, no history is read. The picture is the same as an engine without this feature.
- There are NO per-object motion vectors. Animated or fast objects can ghost a little.

## Where in the frame

| Registration | Stage, priority | Work | Toggle names |
|---|---|---|---|
| `TemporalAa.FrameStamp` | PreOpaque, 47 | Frame clock for snapshots | none (`Always`) |
| `TemporalAa.Jitter` | PreOpaque, 48 | Decides the jitter and gives it to the engine | none (`Always`) |
| `TemporalAa` | PostComposite, -150, `Replace` | Resolve compute pass, then a blit into the private target | `TemporalAa.Resolve.Dispatch`, `TemporalAa.CopyToPrivateTarget` |

- Shader: `src/Features/TemporalAa/Shaders/TemporalAaResolve.comp`. Binding names: `params`, `sceneColor`, `sceneDepth`, `colorHistory`, `depthHistory`, `colorOutput`, `depthOutput`.
- Persistent targets per view: `TemporalAa.Color0/1` (RGBA16F) and `TemporalAa.Depth0/1` (R32F, view depth, 0 = sky). They swap every frame.
- The scene is drawn after the jitter is set. The engine `RenderOpaque` provider reads it and draws the Game view with the shifted matrix.
- The Scene view registration starts off (`DefaultSecondaryViewEnabled(false)`). If you turn it on, it reports `ViewNotSupported` and does nothing.
- No channel, scene slot or view output is published. `ToneMapping` still owns the final picture.

## Jitter and heartbeat
- Jitter is in NDC. `clip.xy += jitter * clip.w`. A zero jitter returns the input matrix bit for bit.
- Sequence: Halton base 2 for x, base 3 for y. `index = (frame % jitterSampleCount) + 1`. Pixels are `(h2 - 0.5, h3 - 0.5) * jitterScale`. `jitterScale = 0` gives exactly `(0, 0)`.
- `fx::View::viewProjection` stays unjittered. The jitter is in `View::jitterNdc`. `View::DrawViewProjection()` is the jittered matrix.
- The jitter is set in PreOpaque, early. The resolve runs in PostComposite, late. So the jitter is applied only when the resolve ran in the PREVIOUS frame (the heartbeat) and every cheap check passes now.
- Check order (first failure wins): not Primary view, `enabled` off, capture armed, a resolve pass off, no valid camera, extent under 8 px, shader not ready, no heartbeat.
- Start up: frame 0 has no heartbeat, so no jitter. The resolve runs anyway and sets the heartbeat. Frame 1 gets jitter.
- The shader is only built after the cheap checks pass. A disabled feature never starts a shader build.

## Settings

| Key | Default | Range | Meaning |
|---|---|---|---|
| `enabled` | true | bool | Off: no jitter, no pass, no history |
| `jitterSampleCount` | 8 | 2..16 | Length of the Halton cycle |
| `jitterScale` | 1.0 | 0..8 | 1 = +/-0.5 pixel. 0 = no jitter. Above 1 is for debugging |
| `feedbackMin` | 0.85 | 0..0.99 | History share when current and history differ a lot |
| `feedbackMax` | 0.96 | `feedbackMin`..0.99 | History share when they agree |
| `clipGamma` | 1.0 | 0.25..4 | Size of the color clip box in sigmas |
| `depthTolerance` | 0.03 | 0.001..0.5 | Relative view depth tolerance of the history depth check |
| `debugView` | `Off` | `Off`, `JitterOnly` | `JitterOnly`: jitter on, history ignored |

- Resolve steps: 3x3 neighborhood in `c / (1 + luma)` space and YCoCg, clip box (mean +/- gamma * sigma, cut by min/max), closest-depth reprojection, Catmull-Rom history (12 taps), 3x3 history depth range check, clip, luma based feedback, blend.
- The depth check uses a range of 3x3 history texels, not one texel. A single texel would kill the AA at silhouettes.
- Non-finite values become the default. Values are clamped. `feedbackMax` is lifted to `feedbackMin` if lower.

## Debug keys
- Writable: `enabled`, `jitterSampleCount`, `jitterScale`, `feedbackMin`, `feedbackMax`, `clipGamma`, `depthTolerance`, `debugView`. Values are clamped. A bad key, bad value, bad enum text or a read-only key is HTTP 400 and applies nothing. Action: `reset_defaults` (no arguments).
- Read-only, per view with prefix `game.` or `scene.`: `status`, `frameIndex`, `jitterDecision`, `jitterFrameIndex`, `jitterApplied`, `jitterPixelX`, `jitterPixelY`, `historyUsed`, `stableFrames`, `extentWidth`, `extentHeight`, `textureBytes`.
- `status`: `NotRendered`, `Ok`, `Disabled`, `PassDisabled`, `ShadersNotReady`, `TargetFailed`, `NoValidCamera`, `ExtentTooSmall`, `ViewNotSupported`, `CaptureArmed`.
- `jitterDecision`: `NotRun`, `Apply`, `NotPrimaryView`, `Disabled`, `CaptureArmed`, `PassDisabled`, `NoValidCamera`, `ExtentTooSmall`, `ShadersNotReady`, `NoHeartbeat`.
- The status and the jitter decision age on their own. A stale half reads `NotRendered` or `NotRun`.

```cmd
curl.exe "http://127.0.0.1:8080/feature_debug/state?name=TemporalAa"
curl.exe "http://127.0.0.1:8080/feature_debug/set?name=TemporalAa&enabled=false"
curl.exe "http://127.0.0.1:8080/feature_debug/set?name=TemporalAa&jitterScale=0"
curl.exe "http://127.0.0.1:8080/feature_debug/set?name=TemporalAa&debugView=JitterOnly&jitterScale=6"
curl.exe "http://127.0.0.1:8080/feature_debug/invoke?name=TemporalAa&action=reset_defaults"
curl.exe http://127.0.0.1:8080/get_game_view -o taa.png
```

## Six ways to switch it off

| Way | Result in the same frame |
|---|---|
| Setting `enabled=false` | Status `Disabled`. Jitter decision `Disabled`. No jitter, no pass |
| Pass `TemporalAa.Resolve.Dispatch` off | Status `PassDisabled`, jitter `PassDisabled`. No jitter, no pass |
| Pass `TemporalAa.CopyToPrivateTarget` off | Same as above |
| Registration `TemporalAa` off (`set_feature_enabled`) | The resolve callback does not run. Jitter stops after the heartbeat goes stale: decision `NoHeartbeat`, status `NotRendered` |
| Registration `TemporalAa.Jitter` off | No jitter. The resolve still runs and only accumulates the same picture |
| Frame Debugger capture armed | Status `CaptureArmed`, jitter `CaptureArmed`. The capture shows the unjittered scene |

- Every early exit of the resolve declares nothing, drops the history and clears the heartbeat.
- History is written only after both passes were declared.
- Switch back on: AA is back within a few frames. `historyUsed` needs `stableFrames >= 2`.

## Known limits
- No per-object motion vectors. The resolve reprojects with depth and camera matrices only. A moving or animated object (the Furina model in an animation) can show slight ghosting or softness. The color clip and the depth check limit it. They do not remove it.
- TAA is always a little softer than a non-AA picture. Thin 1 pixel lines can lose some contrast.
- The Scene view stays aliased. It is never jittered and never resolved.
- One jittered, unresolved frame is possible when the `TemporalAa` registration is disabled while it runs. The heartbeat is from the last frame, so one frame still gets jitter. Then jitter stops. The shift is under 1 pixel.
- If BOTH resolve passes are toggled off, the `PostComposite` gate skips the callback (the gate lets it run when at least one pass is on). The resolve status then ages to `NotRendered` instead of `PassDisabled`. The jitter decision still reads `PassDisabled`. With only one pass off the status is `PassDisabled`.
- Depth is drawn with the jittered matrix. Features that unproject it with the unjittered matrix have an error of up to 0.5 pixel. This is accepted.
- A very large `jitterScale` on a tiny view gives no jitter. The engine accepts at most 0.5 NDC, so the jitter is skipped (no error log, `jitterApplied` stays false).

## Memory
- `width * height * 24` bytes per view: two RGBA16F (8 bytes) and two R32F (4 bytes) targets. `game.textureBytes` shows it.
- The targets are created on the first resolve. They stay after that.

## Gotchas
- Anything that draws into the view's scene color or depth must use `f.View().DrawViewProjection()`. A site that uses `viewProjection` is offset by up to 1 pixel against the others: outline gaps, shimmer.
- Sites today: the `RenderOpaque` CPU draw (Core), the GpuDrivenScene cluster draw (`ClusterDrawCameraOf`), `ToonCharacter` Surface, Outline and Overlay.
- Shadow cascades, the sky background and the Scene view do not use the jitter on purpose.
- Gtao, Ssgi, VolumetricLight and Atmosphere use the unjittered `viewProjection` on purpose. They derive the camera from it and keep their own history with it.
- Do not read a picture of `JitterOnly` as a bug. It shows the jittered frame without history.
- Settings are not saved in scene files.

## Related
- [Render features](README.md) · [Bloom](bloom.md) · [Tone mapping](tone-mapping.md) · [GTAO](gtao.md)
- [Feature kit](../feature-kit.md) · [Render honesty rules](../../conventions/render-honesty.md)
- [HTTP cheat sheet](../../debugging/http-cheat-sheet.md) (recipe R12)
