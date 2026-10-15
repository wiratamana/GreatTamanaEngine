# VolkLeakCheck

Only `src/Renderer` may name the graphics backend. The check fails when any other file under `src`
contains `volk.h`, `Vk*`, `vk*` or `VK_*`.

- Scanner: `gte_ci_scan` (`tools/ci/common`). Comments are skipped, string literals are scanned
  (`const char* = "VkFormat"` is still a leak), matching is on whole identifiers.
- `tokens.txt`: the forbidden names. `allowlist.txt`: files that still leak (starting debt).
- Ratchet: an allowlist line needs a one-line reason, and fails the check when its file is gone or
  clean ("stale line: remove it"). Lines are only ever removed; never add one to silence a new leak.
- Starting debt: `gte_ci_scan --root src --skip Renderer/ --tokens tokens.txt --comments skip --strings scan --report`.
