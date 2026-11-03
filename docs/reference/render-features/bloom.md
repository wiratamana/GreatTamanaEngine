# Bloom

> Glow around very bright pixels. For anyone who tunes glow, or sees a halo or a flooded frame.
> Verified against code: 2026-11-03

## TL;DR
- Feature `Bloom` in `src/Features/Bloom/`. Runs in `PostComposite` before `ToneMapping`.
- Input is linear HDR scene color. Output is added on top (`Additive`). Pixels below the threshold add nothing.
- It knows nothing about any model or material. Anything that outputs HDR above the threshold glows.
- Steps: bright pass at half size, down chain (13-tap), up chain (3x3 tent, additive), blit into the private target.
- Input radiance is clamped to 64, so the sun disc cannot flood the frame.

## Where in the frame

| Registration | Stage, priority | Work | Toggle names |
|---|---|---|---|
| `Bloom.FrameStamp` | PreOpaque, 46 | Frame clock for snapshots | none (`Always`) |
| `Bloom` | PostComposite, -100, `Additive` | Compute chain, then a blit into the private target | see below |

Pass names: `Bloom.Prefilter`, `Bloom.Down0`..`Bloom.Down4`, `Bloom.Up0`..`Bloom.Up4`, `Bloom.CopyToPrivateTarget`.
- Mip `k` is `2^(k+1)` times smaller than the view. `Bloom.Down<k>` writes mip `k + 1`. `Bloom.Up<k>` writes up level `k`.
- With `levels = N` the chain uses `Prefilter`, `Down0..Down<N-2>`, `Up0..Up<N-2>` and the copy. Each of them must be on, or the feature declares nothing and reports `PassDisabled`.
- Shaders: `src/Features/Bloom/Shaders/BloomPrefilter.comp`, `BloomDown.comp`, `BloomUp.comp`.
- Active in both views. Each mip is an RGBA16F transient target.

## Settings

| Key | Default | Range | Meaning |
|---|---|---|---|
| `enabled` | true | bool | Off declares nothing |
| `threshold` | 1.0 | 0..16 | Linear radiance where glow starts. 1 is white |
| `knee` | 0.5 | 0..1 | Soft transition width, as a fraction of the threshold |
| `intensity` | 0.30 | 0..4 | Multiplier on the glow that is added. 0 declares nothing |
| `levels` | 5 | 2..6 | Mip chain length. Reduced when the view is small |

- Soft threshold: `kept = max(q, b - t) / b`, with `q = clamp(b - t + k, 0, 2k)^2 / (4k)`. `b` is the max channel, `t` the threshold, `k = t * knee`.
- The bright pass averages 4 pixels with a Karis weight (`1 / (1 + luma)`) to suppress single hot pixels.
- A view whose last mip would be smaller than 2 pixels loses levels. Under 2 levels it reports `ExtentTooSmall`.
- Tests pin the threshold curve and the push block layouts to the shader text.

## Debug keys
- Writable: `enabled`, `threshold`, `knee`, `intensity`, `levels`. Values are clamped. A bad key or value is HTTP 400 and applies nothing. Action: `reset_defaults`.
- Read-only, per view with prefix `game.` or `scene.`: `status`, `frameIndex`, `activeLevels`.
- `status`: `NotRendered`, `Ok`, `Disabled`, `PassDisabled`, `ShadersNotReady`, `TargetFailed`, `ExtentTooSmall`.

```cmd
curl.exe "http://127.0.0.1:8080/feature_debug/state?name=Bloom"
curl.exe "http://127.0.0.1:8080/feature_debug/set?name=Bloom&threshold=1.2&intensity=0.4"
curl.exe "http://127.0.0.1:8080/render_graph/set_feature_enabled?name=Bloom&enabled=false&view=primary"
```

## Gotchas
- Bloom runs in linear space before the exposure of `ToneMapping`. The threshold is not in display units.
- A pass that is toggled off turns the whole effect off. There is no partial chain.
- Measure the glow with Bloom on and off. The mean frame luminance should move by a few percent only.
- Settings are not saved in scene files.

## Related
- [Render features](README.md) · [Tone mapping](tone-mapping.md) · [Volumetric light](volumetric-light.md)
- [HTTP cheat sheet](../../debugging/http-cheat-sheet.md)
