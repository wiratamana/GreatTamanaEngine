# RemovedSymbolCheck

Fails when `src` or `tests` name a symbol listed in `tokens.txt`. Those symbols were deleted on purpose.
A hit means dead code came back, or a comment still talks about code that no longer exists.

- Scanner: `gte_ci_scan` (`tools/ci/common`), run once for `src` and once for `tests`. Comments and string literals are scanned.
- The allowlist is empty and stays empty.
- When a symbol is deleted, add its name to `tokens.txt` in the same change.
