# Profiler A/B run

`run_ab.ps1` drives a running editor over HTTP and writes one markdown table (`profiler_ab.md`).
It needs no module and sends only JSON requests. It never asks for an image or a capture.

```powershell
powershell -File tools\profiler_ab\run_ab.ps1 -BaseUrl http://127.0.0.1:8080 -RecordFrames 300
```

- Two runs: a timing run (cluster stats off) and a count run (cluster stats on).
- Each run waits for the warm-up frames, then reads the frames from `GET /profiler/frames`.
- Rows A1..A10 name their HTTP source. A row that no route can answer prints `NO HTTP SOURCE`. That is a profiler bug.
- Frame time and GPU span are reported as median and p95 over the measured frames.
- Fix the camera pose and window size before the run, and run with vsync off.
- No route switches the GPU stamps on and off, so the stamps comparison is reported as not measured.
