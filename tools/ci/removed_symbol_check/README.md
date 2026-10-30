# RemovedSymbolCheck
> Deleted symbols must stay deleted. This check fails when one comes back.

## What it enforces

| Item | Value |
|---|---|
| ctest names | `RemovedSymbolCheckSrc`, `RemovedSymbolCheckTests` |
| Scanned | `src`, then `tests` |
| Forbidden | every name in `tokens.txt` |
| Comments and strings | both scanned |
| Allowlist | empty. It stays empty. |

A hit means dead code came back, or a comment still talks about code that is gone.

## Rule

When you delete a symbol, add its name to `tokens.txt` in the same change.

## Run

```cmd
ctest --test-dir build -R RemovedSymbolCheck --output-on-failure
```
