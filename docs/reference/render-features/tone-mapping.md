# Tone mapping

> Last step of the picture: exposure, curve and sRGB encode. For anyone who sees wrong brightness or color.
> Verified against code: 2026-10-30

## TL;DR
- Feature `ToneMapping` in `src/Features/ToneMapping/`. Runs last in `PostComposite`.
- Input is linear HDR scene color. Output is display color 0..1 in an RGBA8 texture.
- Steps: exposure, saturation, curve, sRGB encode, dither.
- After it, the view target holds display values. With the feature off, the view keeps raw linear HDR.
- `/get_game_view` shows the tone-mapped image as it is. It adds no second curve.

## Where in the frame

| Registration | Stage, priority | Work | Toggle names |
|---|---|---|---|
| `ToneMapping.FrameStamp` | PreOpaque, 40 | Frame clock for snapshots | none (`Always`) |
| `ToneMapping` | PostComposite, 1000, `Replace` | Compute `ToneMapping.Dispatch`, then a blit into the private target | `ToneMapping.Dispatch`, `ToneMapping.CopyToPrivateTarget` |

- Both toggles must be on. If one is off, the feature declares nothing and reports `PassDisabled`.
- Output texture: `GameViewToneMapped` / `SceneViewToneMapped` (RGBA8). The last exposure per view wins.
- Shader: `src/Features/ToneMapping/Shaders/ToneMapping.comp`. CPU reference: `src/Math/ToneMapCurves.h`.
- Active in both views.

## Settings

| Key | Default | Range | Meaning |
|---|---|---|---|
| `exposure` | 0.8 | 0.01..16 | Linear multiplier on scene color |
| `mode` | `Aces` | `Clamp`, `Aces`, `PbrNeutral` | Curve. ACES is the Narkowicz fit. PbrNeutral is Khronos |
| `saturation` | 1.1 | 0..2 | 1 = unchanged. Done before the curve |
| `dither` | 1 | 0..2 | Noise in 8-bit steps before quantizing |

- Order inside the shader: exposure, saturation, curve, sRGB encode, dither.
- A non-finite input pixel becomes 0 in the CPU reference.
- Tests pin the CPU curve constants to the shader text.

## Debug keys
- Writable: `exposure`, `saturation`, `dither`, `mode`. A wrong mode name is HTTP 400. Action: `reset_defaults`.
- Read-only, per view with prefix `game.` or `scene.`: `status`, `frameIndex`.
- `status`: `NotRendered`, `Ok`, `ShadersNotReady`, `TargetFailed`, `PassDisabled`.

```cmd
curl.exe "http://127.0.0.1:8080/feature_debug/state?name=ToneMapping"
curl.exe "http://127.0.0.1:8080/feature_debug/set?name=ToneMapping&mode=Clamp&exposure=1&saturation=1&dither=0"
curl.exe -o game.png "http://127.0.0.1:8080/get_game_view"
```

## Gotchas
- Features that must run after all other composites use this one as the model: a high priority and `Replace`.
- Exact-value checks need `mode=Clamp exposure=1 saturation=1 dither=0`.
- Settings are not saved in scene files.

## Related
- [Render features](README.md) · [Scene lighting](scene-lighting.md) · [Atmosphere](atmosphere.md)
- [HTTP cheat sheet](../../debugging/http-cheat-sheet.md)
