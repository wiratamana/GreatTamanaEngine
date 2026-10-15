# FeaturePurityCheck

Render feature code (`src/Features/<Name>/`, except `Editor/`) must stay free of Vulkan and of the
engine's raw wrappers. The check fails on any name in `tokens.txt` and on any `#include` whose path
contains an entry of `includes.txt` (`Renderer/`, `Core/Core.h`, `Game/Game.h`, `Game/SceneQuery.h`,
`Game/RenderSystem.h`, `Application/`). `Core/FeatureKit/` is allowed.

- Scanner: `gte_ci_scan`. Comments and string literals are skipped; matching is whole identifier and
  case-sensitive, so `Renderer` does not match `ShadowMaskRenderer`. Do not weaken the token list: rename
  the feature symbol instead.
- `allowlist.txt`: starting debt, one reason per line, relative to `src/Features`.
- Ratchet: a line whose file is gone or clean fails ("stale line: remove it"). Lines are only removed.
- Starting debt: `gte_ci_scan --root src/Features --skip-dir Editor --tokens tokens.txt --includes includes.txt --comments skip --strings skip --report`.
