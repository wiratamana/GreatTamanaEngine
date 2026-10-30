# Logging
> How to write log lines in engine code and how to read them in the Log panel or over HTTP.
> Verified against code: 2026-10-30

## TL;DR
- Log with `GTE_LOG_DEBUG`, `GTE_LOG_INFO`, `GTE_LOG_WARNING`, `GTE_LOG_ERROR`. Include `Core/Logging.h`.
- Logs go to a ring buffer of 2000 entries in memory. No file. They are lost when the process exits.
- Read them in the editor "Log" panel, or with `GET /get_logs`.
- Use `GTE_LOG_ERROR_BLOCKING` for a failure that an automated caller must see. Read it with `GET /get_blocking_errors`.
- Logging is safe from any thread.

## How it works
```text
GTE_LOG_*  ->  IsLogSinkInstalled()?  ->  LogToActiveSink  ->  ILogSink  ->  LoggerLogSink  ->  Logger (ring buffer)
 (any thread)      no sink: skip, message not built                                              Log panel, /get_logs
```

| Piece | File | Role |
|---|---|---|
| Macros, `LogLevel`, `LogEntry`, `LogQueryFilter` | `src/Core/Logging.h` | Used by `gte_core`. No editor include. |
| `ILogSink`, `InstallLogSink` | `src/Core/LogSink.h` | One global sink. Last install wins. `nullptr` removes it. |
| `Logger`, `LoggerLogSink` | `src/Editor/Logger.h` | The ring buffer. `main` installs `LoggerLogSink` first. |
| `ILogQueryCapability` | `src/Core/EditorCapabilities.h` | How the HTTP routes reach the logger. |

- A host with no sink logs nothing and pays nothing. The message expression is not evaluated.
- `EditorHost::Run()` calls `Logger::SetCurrentFrame` once per frame.

## Write a log line
```cpp
GTE_LOG_WARNING("Renderer", "Pool is full, size=" + std::to_string(size));
```

1. Pass a category and a ready `std::string`. There is no `printf` format.
2. Category is free text, for example `Renderer`, `Network`, `MeshClustering`. Filters match it exactly.
3. Levels in order: `Debug`, `Info`, `Warning`, `Error`.
4. Do not log from a hot loop every frame. The buffer is only 2000 entries.

## Reading the data
Each entry has these fields:

| Field | Meaning |
|---|---|
| `id` | Rises by one per entry. Never reused. `Clear()` does not reset it. |
| `frame` | Engine frame number at record time. |
| `timestamp_seconds` | Seconds since the first log line of the process. |
| `level`, `category`, `message` | As written. |
| `is_blocking` | `true` for `GTE_LOG_ERROR_BLOCKING`. |

`GET /get_logs` parameters (all optional, filters combine with AND):

| Param | Meaning |
|---|---|
| `since_id` | Only entries with `id` greater than this. |
| `min_level` | `debug`, `info`, `warning` or `error`. This level and above. Case does not matter. |
| `category` | Exact match. Case matters. |
| `keyword` | Substring of `message`. Case does not matter. |
| `frame_min`, `frame_max` | Inclusive frame range. |
| `limit` | Default 200. Larger than 2000 is cut to 2000. |

- Result is the newest `limit` matches, listed oldest first.
- Reply: `{"logging_enabled", "count", "latest_id", "entries":[...]}`.
- A bad parameter gives `400`. No log capability gives `503`.
- `GET /get_blocking_errors` takes only `since_id`. It returns blocking entries only, at most the newest 200.
- `POST /clear_logs` empties the buffer. Reply: `{"success":true,"cleared_count":N}`.

## Recipes
```cmd
curl.exe "http://127.0.0.1:8080/get_logs?min_level=warning&limit=50"
curl.exe "http://127.0.0.1:8080/get_logs?category=ImGuiIdConflict"
curl.exe "http://127.0.0.1:8080/get_blocking_errors"
```

| Goal | Steps |
|---|---|
| Watch new lines | Read `latest_id` once. Then poll `/get_logs?since_id=<latest_id>` and keep the new `latest_id`. |
| Test one action | `POST /clear_logs`, run the action, then `GET /get_logs?min_level=warning`. |
| Is anything broken? | `GET /get_blocking_errors`. An empty `entries` list means no blocking error. |

## Log panel
- Panel name: `Log`. Open it with `GET /activate_tab?name=Log`.
- Four level checkboxes. They pick any subset, not only "this level and above".
- Free text `Category` and `Keyword` filters, `Auto-scroll`, and a `Clear` button.

## Gotchas
- A stale `since_id` is safe after a clear, because ids never reset.
- Entries wrap after 2000. A slow poller can miss lines.
- Some messages go to `stderr` only: the fatal error in `main.cpp`, and two in `JobContinuation.cpp` and the render graph compiler. They are not in the buffer.
- If the HTTP port is busy, the server logs a `Network` error and stays off. Read it in the Log panel, not over HTTP.
- Log routes read the logger with no main-thread wait. The logger is built for that. Do not copy this for ECS or Renderer state.

## Related
- [HTTP cheat sheet](http-cheat-sheet.md)
- [HTTP routes](http-routes.md)
- [HTTP server](../architecture/http-server.md)
- [ImGui ID uniqueness](../conventions/imgui-id-uniqueness.md) logs under `ImGuiIdConflict`
