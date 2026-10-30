# Profiler A/B run
> `run_ab.ps1` drives a running editor over HTTP and writes one table, `profiler_ab.md`. JSON only: no image, no capture.

```powershell
powershell -File tools\profiler_ab\run_ab.ps1 -BaseUrl http://127.0.0.1:8080 -RecordFrames 300
```

## Before you run

- Start the editor with `--no-vsync`. Fix the camera pose and window size.
- The script stops when `GET /profiler` `device_state.present_policy` is `capped`. `-AllowCapped` overrides it.

## Parameters
Defaults: `-BaseUrl http://127.0.0.1:8080`, `-Feature GpuDrivenScene`, `-WarmupFrames 120`, `-RecordFrames 300`, `-TimeoutSeconds 120`, `-OutFile profiler_ab.md`.

## What it does

1. Runs twice: timing (`collectStats` off), then counts (`collectStats` on).
2. Waits for warm-up, then reads frames from `GET /profiler/frames`.
3. Rows A1 to A10 name their HTTP source. Frame time and GPU span show median and p95.
4. `NO HTTP SOURCE` means no route can answer the row. That is a profiler bug.
5. The GPU stamps on/off comparison is not measured. No route switches the stamps.
