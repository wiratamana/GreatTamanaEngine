# PHASE5 — `GET /get_texture` + `GET /list_textures` HTTP Endpoints

> **Second-iteration audit note (this revision):** this file was re-checked
> against the LIVE `src/Network/NetworkRoutes.h`/`.cpp`, `src/Network/NetworkServer.cpp`/`.h`,
> and `src/Application/FrameCaptureBridge.h` (byte for byte), against
> `PHASE0_MASTER_STRATEGY.md`'s own `/list_textures` endpoint contract, and
> against this codebase's own actual, established test conventions for
> `nlohmann::json`-built response functions. The previous revision of this
> file left a real design choice ("Option 1 vs Option 2" for how
> `frames_since_update` gets threaded to `/list_textures`) explicitly OPEN
> for whoever implemented it — that is exactly the kind of ambiguity a
> strategy document must not leave behind, so it has now been **closed,
> definitively, with one concrete, fully-detailed design** (see Step 2/3.3
> below). One real content gap and several smaller precision/consistency
> issues were also found and fixed:
> 1. **MAJOR — resolved "Option 1 vs Option 2" for `frames_since_update`
>    plumbing, for good.** The previous revision sketched `PublishTextureList()`
>    taking either a raw `std::vector<rg::DebugTextureSnapshot>` (Option 2) or
>    `NetworkRoutes.h`'s own `std::vector<TextureListEntryView>` directly
>    (Option 1), and left picking one — "Option 1 is the RECOMMENDED choice
>    ... implement it that way unless a concrete obstacle turns up" — as an
>    exercise for the implementer. Neither shape as originally sketched is
>    actually clean: Option 2 would force `FrameCaptureBridge.h` (Application
>    layer) to `#include` `RenderGraphDebugTextureRegistry.h` (Renderer/
>    RenderGraph layer), breaking this exact file's own header-comment promise
>    ("Deliberately Vulkan-free, Renderer-free, and engine-free"); Option 1
>    would instead force `FrameCaptureBridge.h` to depend on
>    `gte::Network::TextureListEntryView` — a type owned by the LAYER ABOVE IT
>    (`src/Network/`), a backwards dependency this codebase has never made
>    anywhere else (compare `NetworkRoutes.h`'s own existing rule, restated in
>    its `wantsDepth` field's doc comment: it deliberately does NOT
>    `#include FrameCaptureBridge.h`, "since FrameCaptureBridge lives under
>    src/Application/, one layer further from pure than this file wants to
>    depend on" — the same logic forbids the reverse direction too). **The
>    actual resolution (see Step 2/3.3 below): a THIRD, small, plain-data
>    struct, `PublishedTextureListEntry`, is added to `FrameCaptureBridge.h`
>    itself** — built entirely from already-resolved plain scalars (a
>    `regime`/`format` STRING, not an `rg::` enum; a `framesSinceUpdate`
>    integer, already subtracted) — so `FrameCaptureBridge` never sees an
>    `rg::` type at all, and `NetworkRoutes.h`'s own `TextureListEntryView`
>    never crosses back into `FrameCaptureBridge.h`. `Application::Run()` (the
>    one place that legitimately knows about BOTH `RenderGraph` and
>    `FrameCaptureBridge`) does the resolving; `NetworkServer.cpp` (the one
>    place that legitimately knows about BOTH `FrameCaptureBridge` and
>    `NetworkRoutes.h`) does the trivial 1:1 field copy from
>    `PublishedTextureListEntry` into `TextureListEntryView`. This is now
>    shown as ONE concrete, fully-consistent code path across all three files
>    — nothing left open for a later implementer to decide.
> 2. **MAJOR — `/list_textures` was missing an entire field
>    `PHASE0_MASTER_STRATEGY.md` already promises.** Step 1's own endpoint
>    contract says the response carries, per texture, "its regime, extent,
>    **format**, whether it has a depth buffer, and how 'fresh' its last
>    capture is", and the campaign's own "Definition of Done" explicitly
>    checks for "correct regime/extent/**format**/hasDepth metadata" — but the
>    previous revision's `TextureListEntryView`/JSON shape had no `format`
>    field at all. **Fixed**: both `PublishedTextureListEntry` and
>    `TextureListEntryView` now carry a `format` string (the texture's COLOR
>    `VkFormat`, human-readable — e.g. `"B8G8R8A8_UNORM"` — resolved by a new,
>    small `Application.cpp` helper; see Step 3.4). This is a DIFFERENT
>    "format" than `/get_texture`'s own JSON envelope's `"format":"png"` field
>    (the PNG *encoding*, unrelated to the source texture's `VkFormat`) — the
>    two must never be confused; both are called out explicitly below.
> 3. Confirmed live, exact signature match, no drift: `RegisterCaptureRoute(httplib::Server&,
>    const char*, FrameCaptureKind, FrameCaptureBridge*)` and
>    `RegisterRoutes(httplib::Server&, FrameCaptureBridge*, EngineCommandBridge*)`
>    (both `NetworkServer.cpp`) — this phase's own two new routes/wiring lines
>    plug into exactly this existing shape with no adjustment needed.
> 4. Confirmed live: `NetworkRoutes.cpp` already
>    `#include <nlohmann/json.hpp>` (exact spelling) — the previous revision's
>    "confirm the exact existing include spelling... and match it" hedge is
>    now a plain, confirmed fact, not something left for the implementer to
>    verify.
> 5. Confirmed live: `ResolveCaptureResponseFormat(const std::string&, const std::string&)` /
>    `BuildCaptureJsonBody(int, int, const std::string&)` (`NetworkRoutes.h`)
>    match exactly what this phase's own route reuses.
> 6. **Real gap: `NetworkRoutes.h` does not `#include <vector>` today**
>    (confirmed live — only `<cstdint>`/`<string>`) — needed for the new
>    `BuildListTexturesResponseJson(const std::vector<TextureListEntryView>&)`
>    parameter. Added explicitly to Step 3.1 below, mirroring
>    `PHASE1`/`PHASE3`'s own "a real, easy-to-miss include/registration gap"
>    call-outs elsewhere in this campaign.
> 7. **Corrected the Tests section's own guidance — it pointed at the wrong
>    established convention.** The previous revision said to "assert the
>    EXACT output... mirrors Phase 3 of `network-impl-2`'s own 'assert the
>    literal expected JSON string' rule" for `BuildTextureCaptureJsonBody()`/
>    `BuildListTexturesResponseJson()`. That rule only ever applied to
>    `BuildCaptureJsonBody()` — a HAND-FORMATTED string builder with a fixed,
>    guaranteed field order. Both of THIS phase's own builders go through
>    `nlohmann::json`, whose object key ordering is an implementation detail
>    this codebase already deliberately never depends on — confirmed live,
>    `tests/Network/NetworkRoutesTests.cpp`'s own `BuildResponseJsonTests`
>    suite (covering `BuildInstantiatePrimitiveResponseJson()`/
>    `BuildDeleteEntityResponseJson()`, network-impl-3's own nlohmann-based
>    builders) always re-parses the result via `nlohmann::json::parse()` and
>    asserts individual field VALUES, never a raw literal string comparison.
>    Fixed in Step 3.7 below — the one narrow exception where a literal
>    comparison remains safe/appropriate is the trivial single-key empty-list
>    shape, `{"textures":[]}`.
> 8. **Made this endpoint's OWN failure responses internally consistent.**
>    The previous revision mixed `text/plain` bodies (mirroring
>    `/get_swapchain`/`/get_game_view`'s pre-existing convention) for
>    503/409/504 with a `application/json` body (mirroring the POST
>    endpoints' convention) for 400 — all on the SAME endpoint, meaning a
>    caller would need to branch on status code just to know which
>    `Content-Type` to expect. Fixed: every failure response `/get_texture`/
>    `/list_textures` can ever produce is now `application/json` via
>    `BuildGenericErrorResponseJson()` — one predictable shape regardless of
>    which failure occurred. This does NOT touch `/get_swapchain`/
>    `/get_game_view` (still untouched, still `text/plain`, per this
>    campaign's own Non-Goals).
> 9. Sharpened the 409/504 error text so each one actually says which of the
>    two distinct failure causes it is (unknown/never-rendered name vs.
>    unavailable channel), instead of one shared, ambiguous sentence
>    covering both — see Step 3.5.
>
> Everything else (the exact query-parameter names, the `?channel=color|depth`
> validation rules and their exact-lowercase-match precedent, the
> `NamedTexture`/`DebugTextureChannel`/`RequestCaptureAndWait()` plumbing this
> phase consumes from Phase 4, and the `RegisterRoutes()` wiring point) was
> re-checked against the live source tree and found accurate.

Parent: `PHASE0_MASTER_STRATEGY.md`. Depends on: Phase 2 (`RenderGraph::
ListDebugTextures()`/`CurrentDebugTextureFrameCounter()`), Phase 4
(`FrameCaptureBridge`'s `NamedTexture` kind, `DebugTextureChannel`).

## Step 1: The Goal (Where are we going?)

Add the two HTTP-facing pieces: `GET /get_texture` (the actual capture
endpoint) and `GET /list_textures` (the discovery companion, Locked Design
Decision 3). Both are thin wiring layers over already-existing/Phase-4
machinery, following `network-impl-2`'s own "route handler stays a thin
lambda; all real logic lives in a pure, Tier-1-testable `NetworkRoutes.h/.cpp`
function" convention exactly. Concretely, by the end of this phase:

- `GET /get_texture?texture_name=<name>[&channel=color|depth][&format=png|base64|json]`
  is fully wired end to end, with every failure mode (missing/empty
  `texture_name` → 400; unrecognized `channel` value → 400; bridge
  unavailable → 503; another named-texture capture already in flight → 503;
  channel unavailable for this texture → 409; name never registered/never
  rendered within the timeout → 504) mapped to a distinct, correctly-worded
  JSON error body.
- `GET /list_textures` returns a JSON array of every texture name registered
  so far this session, each with `name`/`regime`/`format`/`width`/`height`/
  `has_depth`/`frames_since_update` — matching `PHASE0_MASTER_STRATEGY.md`'s
  own endpoint contract field-for-field (this revision adds the previously-
  missing `format` field — see the audit note above).
- The `frames_since_update` value published for `/list_textures` is computed
  by ONE single, concrete design (no open choice left for the implementer —
  see Step 2/3.3/3.4 below).

## Step 2: The Situation (Where are we now?)

- `NetworkServer.cpp`'s `RegisterCaptureRoute(httplib::Server&, const char*
  path, FrameCaptureKind kind, FrameCaptureBridge* captureBridge)` (confirmed
  live) is the existing shared helper behind `/get_game_view`/`/get_swapchain`.
  It has NO query-parameter reading beyond `format`/`Accept` (via
  `ResolveCaptureResponseFormat()`), and calls `RequestCaptureAndWait(kind)`
  with no extra payload — both insufficient for `/get_texture`, which needs
  `texture_name`/`channel` too. This phase does NOT try to shoehorn
  `/get_texture` into `RegisterCaptureRoute()` — it adds a NEW, small,
  parallel route-registration function instead (`RegisterGetTextureRoute()`),
  since the two now diverge enough (extra required query param, extra
  possible failure mode, richer JSON response) that forcing them through one
  shared helper would need more parameters/branches than it saves — mirrors
  this codebase's own "only extract a shared helper once two call sites are
  genuinely, provably identical" judgment call (see `network-impl-2/
  PHASE5_GET_SWAPCHAIN_ENDPOINT_AND_FORMAT_NEGOTIATION_REUSE.md`'s own
  reasoning for the OPPOSITE direction — extracting `RegisterCaptureRoute()`
  itself — applied here in reverse, since this time the two are NOT
  identical). Confirmed live: `RegisterRoutes(httplib::Server& server,
  FrameCaptureBridge* captureBridge, EngineCommandBridge* commandBridge)` is
  the one function that registers every route today (two `RegisterCaptureRoute()`
  calls, then the two POST routes) — this phase's own two new
  `RegisterXxxRoute(server, captureBridge)` calls slot in right alongside the
  existing two, using the SAME `captureBridge` parameter `RegisterRoutes()`
  already has in scope (no new parameter needed).
- `nlohmann::json` is already vendored (`network-impl-3`, `cmake/FetchJson.cmake`)
  and already used by `NetworkRoutes.cpp` — confirmed live,
  `#include <nlohmann/json.hpp>` (exact spelling) sits right below
  `#include "NetworkRoutes.h"` at the top of the file — this phase's own new
  JSON bodies use it too (see `PHASE0_MASTER_STRATEGY.md`'s Locked Design
  Decision 2), NOT hand-formatted string concatenation like the OLDER
  `BuildCaptureJsonBody()`.
- `httplib::Request::get_param_value("texture_name")`/`get_param_value(
  "channel")`/`get_param_value("format")` (already confirmed present/used
  elsewhere in this same file, e.g. `RegisterCaptureRoute()`'s own
  `req.get_param_value("format")`) are how query parameters are read — no new
  httplib API surface needed.
- **`NetworkRoutes.h` today only `#include`s `<cstdint>`/`<string>`** —
  neither is `<vector>`, which this phase's new
  `BuildListTexturesResponseJson(const std::vector<TextureListEntryView>&)`
  parameter needs. This must be added explicitly (Step 3.1) — a real,
  easy-to-miss compile-breaking gap, exactly like Phase 1's own
  `tests/CMakeLists.txt` registration reminder.
- **`FrameCaptureBridge.h` (confirmed live, its shape BEFORE this phase's own
  Phase 4 additions) is `namespace gte` — a completely different namespace
  from `gte::Network` (where `NetworkRoutes.h`'s types, including
  `TextureListEntryView`, live).** `NetworkServer.cpp` is the one file that
  already `#include`s BOTH `NetworkRoutes.h` and
  `../Application/FrameCaptureBridge.h`, and is written INSIDE
  `namespace gte::Network { ... }` — meaning every `gte`-namespace symbol
  (`FrameCaptureBridge`, `FrameCaptureKind`, `CapturedPngImage`, ...) is
  already used UNQUALIFIED there today (confirmed live — ordinary unqualified
  lookup finds them through the enclosing `gte` scope). This phase's own new
  `PublishedTextureListEntry` type (added to `FrameCaptureBridge.h`, `gte`
  namespace) is used the exact same unqualified way from `NetworkServer.cpp` —
  no new namespace-qualification pattern is introduced.
- **`NetworkRoutes.h`'s own established rule (confirmed live in its
  `ParsedGetTextureQuery::wantsDepth` doc comment from the previous
  revision): this file's functions take PLAIN SCALAR parameters (or a small
  struct THIS file itself owns), never a struct owned by a different layer.**
  `BuildInstantiatePrimitiveResponseJson()`'s own doc comment states this
  explicitly: "Deliberately take only PLAIN SCALAR parameters - never a
  Game-layer/EngineCommandBridge struct type - so this file keeps its
  existing... contract." `TextureListEntryView` (a struct `NetworkRoutes.h`
  itself defines, built only from plain scalars) already satisfies this rule
  — the mistake the previous revision was about to make was handing an
  `rg::`-typed OR an Application-layer-typed vector INTO `FrameCaptureBridge`
  instead, which is the reverse problem (see the audit note's item 1 above)
  — this phase's actual resolution keeps BOTH files honoring their own
  existing "don't take a foreign layer's struct" rule simultaneously, by
  using TWO small, nearly-identical, layer-local structs and one trivial
  1:1 field copy at the ONE place (`NetworkServer.cpp`) that legitimately
  already depends on both.
- `PHASE0_MASTER_STRATEGY.md`'s own Step 1 endpoint contract for
  `GET /list_textures` explicitly lists FIVE per-texture facts a caller
  needs: "its regime, extent, format, whether it has a depth buffer, and how
  'fresh' its last capture is" — the previous revision of this file only
  ever wired FOUR (regime/extent/hasDepth/freshness), silently dropping
  `format`. This revision restores it (Step 3.1/3.4).
- The established, ACTUAL test convention for this codebase's OWN
  `nlohmann::json`-built response functions (confirmed live,
  `tests/Network/NetworkRoutesTests.cpp`'s `BuildResponseJsonTests` suite,
  covering `network-impl-3`'s `BuildInstantiatePrimitiveResponseJson()`/
  `BuildDeleteEntityResponseJson()`) is: build the response, re-parse it via
  `nlohmann::json::parse()`, then assert individual field VALUES via
  `EXPECT_EQ(parsed["field"], ...)` — never a raw, literal, whole-string
  comparison (which would implicitly assume a specific, unguaranteed
  `nlohmann::json` object key ordering). The OLDER `BuildCaptureJsonBody()`
  (`network-impl-2`, HAND-FORMATTED, not `nlohmann::json`-built) is the one
  function in this file family that a literal string comparison is actually
  safe/correct for, precisely because it is not built through `nlohmann::json`
  at all. This phase's own two new builders (`BuildTextureCaptureJsonBody()`/
  `BuildListTexturesResponseJson()`) ARE `nlohmann::json`-built, so their own
  tests must follow the FIRST convention, not the second (see Step 3.7).

## Step 3: The Plan

### 3.1 — `NetworkRoutes.h` additions

**First, a required include fix (new in this revision — see the audit note
above): add `#include <vector>`** to this file's existing include block
(alongside `<cstdint>`/`<string>`) — required for
`BuildListTexturesResponseJson()`'s `const std::vector<TextureListEntryView>&`
parameter; neither existing include is guaranteed to bring it in
transitively.

```cpp
// network-impl-4 campaign, Phase 5
// (task_manager/network-impl-4/PHASE5_HTTP_ENDPOINTS_GET_TEXTURE_AND_LIST_TEXTURES.md).
// Parsed, validated GET /get_texture query parameters. `valid == false`
// means `errorMessage` explains exactly why (a 400 response - see
// NetworkServer.cpp's own route lambda) - every other field is meaningless
// in that case.
struct ParsedGetTextureQuery {
    bool valid = false;
    std::string errorMessage;
    std::string textureName;
    // Mirrors FrameCaptureBridge's own DebugTextureChannel (Phase 4) -
    // NetworkRoutes.h deliberately does NOT #include FrameCaptureBridge.h
    // (this file's own existing convention - see its header comment:
    // "completely Game/ECS-independent" - FrameCaptureBridge lives under
    // src/Application/, one layer further from pure than this file wants to
    // depend on), so this is its OWN small, parallel bool instead of
    // reusing that enum directly - NetworkServer.cpp's route lambda is
    // what converts `wantsDepth` into the real
    // FrameCaptureBridge::DebugTextureChannel value at the one call site
    // that already depends on both headers anyway.
    bool wantsDepth = false;
};

// Validation rules: "texture_name" must be present and non-empty -
// otherwise "missing or empty required query parameter: texture_name".
// "channel" is OPTIONAL; absent or exactly "color" -> wantsDepth = false;
// exactly "depth" -> wantsDepth = true; any OTHER non-empty value ->
// "invalid channel - must be \"color\" or \"depth\"" (a validation
// FAILURE, unlike ResolveCaptureResponseFormat()'s own "unrecognized value
// falls back to a default" convention - a typo'd channel name is much more
// likely to be a caller MISTAKE worth surfacing loudly than a forward-
// compatible "ignore it" case, since guessing wrong here would otherwise
// silently return the WRONG channel's image with no error at all).
// NOTE: matching is EXACT-CASE ("color"/"depth" only, never "Color"/"DEPTH")
// - mirrors ResolveCaptureResponseFormat()'s own exact-lowercase-only
// matching in this same file; this is a deliberate, consistent choice
// across every query-parameter parser in this file, not an oversight - do
// not add case-insensitive matching here without doing the same everywhere
// else in this file first.
ParsedGetTextureQuery ParseGetTextureQuery(const std::string& textureNameParam, const std::string& channelParam);

// Builds GET /get_texture's own JSON/base64 response body (used only when
// CaptureResponseFormat::JsonBase64 is resolved - see
// ResolveCaptureResponseFormat(), reused unchanged from Phase 3 of
// network-impl-2):
// {"width":<int>,"height":<int>,"format":"png","data_base64":"<...>","frames_since_update":<uint>}
// NOTE: this response's OWN "format" field is always the literal string
// "png" - the PNG *encoding*, exactly like BuildCaptureJsonBody()'s
// existing field of the same name. Do NOT confuse this with
// TextureListEntryView::format below, which is the SOURCE texture's
// VkFormat (e.g. "B8G8R8A8_UNORM") - the two are unrelated concepts that
// simply happen to share a JSON key name in two different response shapes.
// Built via nlohmann::json (see this file's own Step 2 note) - NOT
// BuildCaptureJsonBody() (which stays exactly as /get_game_view/
// /get_swapchain need it, untouched by this campaign).
std::string BuildTextureCaptureJsonBody(
    int width, int height, const std::string& base64Png, std::uint64_t framesSinceUpdate);

// One entry of GET /list_textures's own JSON array - see
// BuildListTexturesResponseJson() below. A plain, scalars-only struct THIS
// file owns (see this file's own Step 2 note on why a struct crossing a
// layer boundary is never accepted directly here) - NetworkServer.cpp is
// the one place that copies gte::PublishedTextureListEntry
// (src/Application/FrameCaptureBridge.h, Phase 5's own addition there - see
// Step 3.3 below) into this struct, one field at a time.
struct TextureListEntryView {
    std::string name;
    std::string regime; // "synchronous" or "pipelined" - already resolved to a string upstream (Application::Run(), Step 3.4) - this file never sees rg::ExecuteTimingMode.
    std::string format;  // e.g. "B8G8R8A8_UNORM" - the texture's COLOR VkFormat, already resolved to a string upstream - see this file's own note on BuildTextureCaptureJsonBody() above for why this is a DIFFERENT "format" concept than that function's own field of the same name.
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    bool hasDepth = false;
    std::uint64_t framesSinceUpdate = 0;
};

// Builds the full GET /list_textures response body:
// {"textures":[{"name":"GameView","regime":"synchronous","format":"B8G8R8A8_UNORM","width":1280,"height":720,"has_depth":true,"frames_since_update":0}, ...]}
// An empty `entries` produces {"textures":[]}, never an error - a session
// where nothing has rendered a single named texture yet (e.g. queried
// immediately at startup, before the first frame) is a valid, normal state.
std::string BuildListTexturesResponseJson(const std::vector<TextureListEntryView>& entries);
```

### 3.2 — `NetworkRoutes.cpp` additions

```cpp
ParsedGetTextureQuery ParseGetTextureQuery(const std::string& textureNameParam, const std::string& channelParam)
{
    ParsedGetTextureQuery result;
    if (textureNameParam.empty()) {
        result.errorMessage = "missing or empty required query parameter: texture_name";
        return result;
    }
    result.textureName = textureNameParam;

    if (channelParam.empty() || channelParam == "color") {
        result.wantsDepth = false;
    } else if (channelParam == "depth") {
        result.wantsDepth = true;
    } else {
        result.errorMessage = "invalid channel - must be \"color\" or \"depth\"";
        return result;
    }

    result.valid = true;
    return result;
}

std::string BuildTextureCaptureJsonBody(
    int width, int height, const std::string& base64Png, std::uint64_t framesSinceUpdate)
{
    nlohmann::json body;
    body["width"] = width;
    body["height"] = height;
    body["format"] = "png";
    body["data_base64"] = base64Png;
    body["frames_since_update"] = framesSinceUpdate;
    return body.dump();
}

std::string BuildListTexturesResponseJson(const std::vector<TextureListEntryView>& entries)
{
    nlohmann::json arr = nlohmann::json::array();
    for (const TextureListEntryView& entry : entries) {
        nlohmann::json item;
        item["name"] = entry.name;
        item["regime"] = entry.regime;
        item["format"] = entry.format;
        item["width"] = entry.width;
        item["height"] = entry.height;
        item["has_depth"] = entry.hasDepth;
        item["frames_since_update"] = entry.framesSinceUpdate;
        arr.push_back(std::move(item));
    }
    nlohmann::json body;
    body["textures"] = std::move(arr);
    return body.dump();
}
```

(`#include <nlohmann/json.hpp>` — already present at the top of this file,
confirmed live; no change needed there.)

### 3.3 — `FrameCaptureBridge.h`/`.cpp` additions (revisits Phase 4's own files)

**This is the ONE, definitive resolution of the "frames_since_update
plumbing" design question the previous revision left open — see the audit
note above for why the two originally-sketched options were both rejected.**
Phase 4 itself never touches `/list_textures` at all (it only adds the
`NamedTexture` kind for `/get_texture`), so there is nothing here that
conflicts with anything Phase 4 already specifies — this phase simply adds
more to the same file.

Add, in `FrameCaptureBridge.h` (`namespace gte`), right after the existing
`CapturedPngImage` struct:

```cpp
// network-impl-4 campaign, Phase 5
// (task_manager/network-impl-4/PHASE5_HTTP_ENDPOINTS_GET_TEXTURE_AND_LIST_TEXTURES.md) -
// one fully pre-shaped row of GET /list_textures' response, published once
// per real engine frame by Application::Run() (see PublishTextureList()
// below). Deliberately a SEPARATE, nearly-identical type from
// gte::Network::TextureListEntryView (src/Network/NetworkRoutes.h), not a
// shared one - this keeps BOTH of this engine's "a struct must never cross
// this exact layer boundary" rules intact at once:
//   - THIS class must stay Vulkan/Renderer/RenderGraph-free (see this
//     file's own header comment, above) - so every field here is an
//     ALREADY-RESOLVED plain scalar, never an
//     rg::DebugTextureSnapshot/rg::ExecuteTimingMode. Application::Run()
//     (which DOES know about RenderGraph) resolves `regime`/`format` to
//     plain strings and `framesSinceUpdate` to a plain integer BEFORE ever
//     calling PublishTextureList() below - see Step 3.4.
//   - NetworkRoutes.h's own response-builder functions must never take a
//     struct OWNED BY A DIFFERENT LAYER as a parameter (see
//     BuildInstantiatePrimitiveResponseJson()'s own doc comment) - so this
//     struct never crosses into NetworkRoutes.h either. NetworkServer.cpp
//     (which already depends on BOTH this header and NetworkRoutes.h) is
//     the ONE place that copies this struct's fields into a fresh
//     gte::Network::TextureListEntryView, one field at a time, right before
//     calling BuildListTexturesResponseJson() - see Step 3.6.
// A few bytes of per-request copying for a single-digit-to-low-double-
// digit-sized list is negligible - see PHASE0_MASTER_STRATEGY.md's own
// Locked Design Decision 8 for this engine's general tolerance for this
// class of cost.
struct PublishedTextureListEntry {
    std::string name;
    std::string regime; // "synchronous" or "pipelined" - see Application.cpp's own ToDebugTextureRegimeString() helper (Step 3.4).
    std::string format;  // e.g. "B8G8R8A8_UNORM" - the texture's COLOR VkFormat, already stringified - see Application.cpp's own DebugTextureColorFormatName() helper (Step 3.4). Never the depth format; see hasDepth below for whether one even exists.
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    bool hasDepth = false;
    std::uint64_t framesSinceUpdate = 0;
};
```

Then, new public methods (place right after `FailPendingRequest()`'s own
declaration, under a fresh comment block — this is NOT part of the
`RequestCaptureAndWait()`/`Slot` request-fulfill machinery above, and must
not be confused with it):

```cpp
// --- GET /list_textures support (network-impl-4 campaign, Phase 5) ------
// Unlike RequestCaptureAndWait() above, there is nothing to "wait for" here
// - the list is whatever it is right now. Guarded by its own small,
// dedicated mutex (m_textureListMutex) - deliberately NO condition
// variable, since nothing ever blocks on this.

// --- Called from the MAIN thread (Application::Run()) only --------------
// Publishes a fresh, COMPLETE snapshot of every currently-known texture -
// OVERWRITES whatever was published before wholesale (never merges/
// appends), so an empty vector correctly clears a previously non-empty
// list rather than leaving stale entries behind. Called once per real
// engine frame - see Application.cpp's own wiring, Step 3.4.
void PublishTextureList(std::vector<PublishedTextureListEntry> entries);

// --- Called from the NETWORK thread (a route handler) only --------------
// A cheap, thread-safe COPY of whatever was last published - never blocks.
// Returns an empty vector if PublishTextureList() has never been called yet
// this session (e.g. queried before the very first Run() iteration
// completes) - a valid, normal state, never an error.
std::vector<PublishedTextureListEntry> GetPublishedTextureList() const;
```

New private members, alongside the existing `Slot m_swapchainSlot;`/
`m_gameViewSlot;` (or Phase 4's own `m_namedTextureSlot;`, if this phase is
implemented after Phase 4 as intended):

```cpp
mutable std::mutex m_textureListMutex;
std::vector<PublishedTextureListEntry> m_publishedTextureList;
```

**No new `#include` is required** — `<string>` (needed for
`PublishedTextureListEntry::name`/`regime`/`format`) is already added to this
file by Phase 4 (its own `m_requestedTextureName` member), and `<vector>`/
`<mutex>`/`<cstdint>` are already present today.

`FrameCaptureBridge.cpp` additions:

```cpp
void FrameCaptureBridge::PublishTextureList(std::vector<PublishedTextureListEntry> entries)
{
    std::lock_guard<std::mutex> lock(m_textureListMutex);
    m_publishedTextureList = std::move(entries);
}

std::vector<PublishedTextureListEntry> FrameCaptureBridge::GetPublishedTextureList() const
{
    std::lock_guard<std::mutex> lock(m_textureListMutex);
    return m_publishedTextureList;
}
```

### 3.4 — `Application.cpp` wiring (publishing the list)

Add two small, anonymous-namespace helpers alongside the existing
`IsBgraFormat()` (same file, same section — confirmed live at the top of
`Application.cpp`):

```cpp
// network-impl-4 campaign, Phase 5 - GET /list_textures' own small,
// human-readable resolution helpers. Both are deliberately narrow (only the
// enumerators/formats this engine's render graph can actually produce
// today), mirroring IsBgraFormat()'s own "accepted narrow risk, documented"
// precedent immediately above - an unrecognized regime can never actually
// occur (ExecuteTimingMode has exactly two enumerators, both handled), and
// an unrecognized VkFormat falls back to a safe, clearly-labeled numeric
// string rather than a crash or a silently-wrong label, mirroring
// src/Editor/MemoryPanelData.cpp's own ToString(VkFormat) fallback
// convention (not reused directly - that function lives under
// GTE_ENABLE_EDITOR-gated src/Editor/, and this call site must work in
// every build configuration, editor or not).
const char* ToDebugTextureRegimeString(rg::ExecuteTimingMode mode)
{
    switch (mode) {
    case rg::ExecuteTimingMode::SynchronousImmediateReadback: return "synchronous";
    case rg::ExecuteTimingMode::PipelinedDeferredReadback: return "pipelined";
    }
    return "unknown"; // Unreachable - every real enumerator handled above.
}

std::string DebugTextureColorFormatName(VkFormat format)
{
    switch (format) {
    case VK_FORMAT_B8G8R8A8_UNORM: return "B8G8R8A8_UNORM";
    case VK_FORMAT_B8G8R8A8_SRGB: return "B8G8R8A8_SRGB";
    case VK_FORMAT_R8G8B8A8_UNORM: return "R8G8B8A8_UNORM";
    case VK_FORMAT_R8G8B8A8_SRGB: return "R8G8B8A8_SRGB";
    default: break;
    }
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "VkFormat(%d)", static_cast<int>(format));
    return std::string(buffer);
}
```

Then add ONE new, self-contained block to `Application::Run()`, placed
immediately next to Phase 4's own named-texture capture block (either just
before or just after it — both run unconditionally, once per `Run()`
iteration, and neither depends on the other's result), still before
`Profiling::FrameProfiler::Instance().SetMemorySnapshot(...)`:

```cpp
// network-impl-4 campaign, Phase 5
// (task_manager/network-impl-4/PHASE5_HTTP_ENDPOINTS_GET_TEXTURE_AND_LIST_TEXTURES.md) -
// GET /list_textures' only data source. Resolved to plain
// PublishedTextureListEntry values HERE, never inside FrameCaptureBridge
// itself - see that struct's own doc comment (FrameCaptureBridge.h) for
// why. Cheap - see PHASE0_MASTER_STRATEGY.md's own Locked Design Decision
// 8: O(declared textures), the same order of magnitude as
// Renderer::GetMemoryResources()'s own existing "safe to call every frame"
// per-frame snapshot copy.
{
    const std::vector<rg::DebugTextureSnapshot> snapshots = m_renderGraph.ListDebugTextures();
    const std::uint64_t currentFrameCounter = m_renderGraph.CurrentDebugTextureFrameCounter();

    std::vector<PublishedTextureListEntry> published;
    published.reserve(snapshots.size());
    for (const rg::DebugTextureSnapshot& snap : snapshots) {
        PublishedTextureListEntry entry;
        entry.name = snap.name;
        entry.regime = ToDebugTextureRegimeString(snap.regime);
        entry.format = DebugTextureColorFormatName(snap.target.format);
        entry.width = snap.target.extent.width;
        entry.height = snap.target.extent.height;
        entry.hasDepth = snap.hasDepth;
        // Same subtraction PHASE0_MASTER_STRATEGY.md's own Locked Design
        // Decision 4 defines for /get_texture's single-entry case (Phase 4),
        // just computed here for EVERY known texture at once, once per
        // frame, rather than once per request.
        entry.framesSinceUpdate = currentFrameCounter - snap.lastUpdatedFrameCounter;
        published.push_back(std::move(entry));
    }
    m_captureBridge.PublishTextureList(std::move(published));
}
```

No new `#include` is required in `Application.cpp` beyond what Phase 3/4
already add (`RenderGraph/RenderGraphDebugTextureRegistry.h` for
`rg::DebugTextureSnapshot`, already added by Phase 4's own named-texture
capture block) — `<cstdio>` (for `std::snprintf`) is already included today.

### 3.5 — `NetworkServer.cpp` — `GET /get_texture`

```cpp
void RegisterGetTextureRoute(httplib::Server& server, FrameCaptureBridge* captureBridge)
{
    server.Get("/get_texture", [captureBridge](const httplib::Request& req, httplib::Response& res) {
        const ParsedGetTextureQuery parsed =
            ParseGetTextureQuery(req.get_param_value("texture_name"), req.get_param_value("channel"));
        if (!parsed.valid) {
            res.status = 400;
            res.set_content(BuildGenericErrorResponseJson(parsed.errorMessage), "application/json");
            return;
        }
        if (captureBridge == nullptr) {
            res.status = 503;
            res.set_content(BuildGenericErrorResponseJson("capture bridge not available"), "application/json");
            return;
        }

        const DebugTextureChannel channel = parsed.wantsDepth ? DebugTextureChannel::Depth : DebugTextureChannel::Color;
        const FrameCaptureBridge::RequestResult result =
            captureBridge->RequestCaptureAndWait(FrameCaptureKind::NamedTexture, 3000, parsed.textureName, channel);

        if (result.alreadyPending) {
            res.status = 503;
            res.set_content(BuildGenericErrorResponseJson(
                "a /get_texture (or another named-texture) capture is already in progress"), "application/json");
            return;
        }
        if (result.failure.has_value()) {
            // TimedOut -> this exact texture_name never registered (or
            // never rendered again) within the timeout -> 504.
            // TargetNotAvailable -> a positively-known "no depth buffer on
            // this texture" (Phase 4's own fast-fail) or an unrecognized
            // depth format (Phase 3's own accepted narrow risk) -> 409. Each
            // gets its OWN, distinct, actionable message - never one shared
            // ambiguous sentence for both.
            if (*result.failure == FrameCaptureFailureReason::TimedOut) {
                res.status = 504;
                res.set_content(BuildGenericErrorResponseJson(
                    "texture_name '" + parsed.textureName + "' was never registered (or never rendered again) "
                    "within the timeout - see GET /list_textures for the currently known names"),
                    "application/json");
            } else {
                res.status = 409;
                res.set_content(BuildGenericErrorResponseJson(
                    "requested channel is not available for texture_name '" + parsed.textureName +
                    "' - it either has no depth buffer, or its depth format could not be visualized"),
                    "application/json");
            }
            return;
        }

        const CapturedPngImage& image = *result.image;
        const CaptureResponseFormat format =
            ResolveCaptureResponseFormat(req.get_param_value("format"), req.get_header_value("Accept"));
        if (format == CaptureResponseFormat::RawPng) {
            res.set_content(reinterpret_cast<const char*>(image.pngBytes.data()), image.pngBytes.size(), "image/png");
        } else {
            const std::string base64 = Encoding::EncodeBase64(image.pngBytes);
            res.set_content(BuildTextureCaptureJsonBody(image.width, image.height, base64, image.framesSinceUpdate),
                "application/json");
        }
    });
}
```

(`DebugTextureChannel`/`FrameCaptureKind`/`FrameCaptureBridge`/
`CapturedPngImage`/`FrameCaptureFailureReason` are all `namespace gte` types,
used unqualified here exactly like every existing route in this file already
does — see Step 2's own note on why this compiles today.)

### 3.6 — `NetworkServer.cpp` — `GET /list_textures`

```cpp
void RegisterListTexturesRoute(httplib::Server& server, FrameCaptureBridge* captureBridge)
{
    server.Get("/list_textures", [captureBridge](const httplib::Request&, httplib::Response& res) {
        if (captureBridge == nullptr) {
            res.status = 503;
            res.set_content(BuildGenericErrorResponseJson("capture bridge not available"), "application/json");
            return;
        }

        const std::vector<PublishedTextureListEntry> published = captureBridge->GetPublishedTextureList();
        std::vector<TextureListEntryView> views;
        views.reserve(published.size());
        for (const PublishedTextureListEntry& entry : published) {
            // Trivial 1:1 field copy - the ONE place this campaign
            // deliberately keeps two nearly-identical structs (see
            // PublishedTextureListEntry's own doc comment, FrameCaptureBridge.h,
            // Step 3.3, for why they are not the same type).
            views.push_back(TextureListEntryView{
                entry.name, entry.regime, entry.format, entry.width, entry.height, entry.hasDepth,
                entry.framesSinceUpdate });
        }
        res.set_content(BuildListTexturesResponseJson(views), "application/json");
    });
}
```

Note this route needs NO `RenderGraph`/`rg::`-namespaced type or header at
all — a genuine simplification versus the previous revision's draft (which
had this exact lambda reading `rg::DebugTextureSnapshot`/
`rg::ExecuteTimingMode` directly, requiring `NetworkServer.cpp` to `#include`
a `RenderGraph/` header). Resolving `PublishedTextureListEntry` entirely in
`Application.cpp` (Step 3.4) means `NetworkServer.cpp` only ever touches
plain scalars and its own `TextureListEntryView`, keeping this file exactly
as Renderer/RenderGraph-agnostic as every other route in it already is.

### 3.7 — `RegisterRoutes()` wiring

```cpp
RegisterGetTextureRoute(server, captureBridge);
RegisterListTexturesRoute(server, captureBridge);
```

(Added directly alongside the two existing `RegisterCaptureRoute(...)` calls,
before the `network-impl-3` POST-route block's own comment — confirmed live,
`RegisterRoutes()`'s exact current shape, Step 2 above.)

### 3.8 — What this phase deliberately does NOT do

- Does not modify `RegisterCaptureRoute()`/`BuildCaptureJsonBody()`/
  `ResolveCaptureResponseFormat()` — reused, untouched.
- Does not change `/get_swapchain`/`/get_game_view`'s existing response
  `Content-Type` conventions for their own failure statuses (still
  `text/plain`, per this campaign's own Non-Goals) — this phase's own
  "every failure is JSON" consistency fix (see the audit note above) is
  scoped ONLY to the two new endpoints this phase adds.
- Does not add pagination/filtering to `/list_textures` — this engine
  declares a tiny number of distinct texture names; a flat, unfiltered
  array is sufficient (see `PHASE0_MASTER_STRATEGY.md`'s own Non-Goals for
  the broader "no configurability" theme this mirrors).
- Does not add a `POST` variant of either endpoint.
- Does not report a texture's DEPTH format anywhere in `/list_textures`
  (only its COLOR format) — `hasDepth` alone is enough to know whether
  `?channel=depth` will succeed; a future need to also expose the depth
  format string would be a small, additive follow-up, not something this
  phase needs to anticipate.

### 3.9 — Tests

- **`tests/Network/NetworkRoutesTests.cpp`** (no new test FILE — this
  extends the existing one, already registered in `tests/CMakeLists.txt`, so
  no new CMake registration step is needed here, unlike Phase 1/3's own
  reminders):
  - Table-driven `ParseGetTextureQuery()` coverage (mirroring
    `ResolveCaptureResponseFormatTest`'s own `TEST_P` shape): missing
    `texture_name` → invalid, exact expected message; empty `texture_name` →
    same; `channel` absent → `wantsDepth == false`; `channel == "color"` →
    `wantsDepth == false`; `channel == "depth"` → `wantsDepth == true`;
    `channel == "Depth"`/`"COLOR"`/any other non-empty value → invalid,
    exact expected message (regression-proofs the exact-lowercase-only
    matching rule called out in Step 3.1's own doc comment).
  - `BuildTextureCaptureJsonBody()`/`BuildListTexturesResponseJson()`: build,
    then **re-parse via `nlohmann::json::parse()` and assert individual
    field values** (mirroring `BuildResponseJsonTests`' own established
    convention exactly — see Step 2's own note; do NOT assert a raw literal
    string for these two, unlike `BuildCaptureJsonBody()`'s own existing
    tests). Cover: a populated `BuildTextureCaptureJsonBody()` call (assert
    `width`/`height`/`format == "png"`/`data_base64`/`frames_since_update`);
    `BuildListTexturesResponseJson({})` — this ONE case is simple/safe
    enough to assert as a literal string, `{"textures":[]}`, since there is
    only one key and therefore no ordering ambiguity at all; a
    `BuildListTexturesResponseJson()` call with 2+ entries (assert
    `parsed["textures"].size()`, then each entry's `name`/`regime`/`format`/
    `width`/`height`/`has_depth`/`frames_since_update` by key); and a name
    containing a double-quote character (mirroring
    `InstantiatePrimitiveNameWithQuoteRoundTripsThroughJson`) to prove
    `nlohmann::json`'s own escaping, not hand-formatting, is what's actually
    protecting this endpoint.
- **`tests/Application/FrameCaptureBridgeTests.cpp`** (no new test file —
  extends the existing one, same reasoning as above): a case that publishes
  a small, hand-built `std::vector<PublishedTextureListEntry>` from one
  (simulated) thread and reads it back via `GetPublishedTextureList()` from
  another, asserting an exact round-trip of every field; a case confirming
  `GetPublishedTextureList()` returns an empty vector before ANY
  `PublishTextureList()` call has ever been made; and a case confirming
  publishing an EMPTY list after a non-empty one correctly clears it (never
  "sticky" old entries) — this last case is the concrete regression test for
  `PublishTextureList()`'s own "overwrites wholesale, never merges" contract.
- `Application.cpp`'s own new publish block, and `NetworkServer.cpp`'s own
  two new route lambdas, stay Tier 2 (need a live `Renderer`/`RenderGraph`/
  `httplib::Server`) — covered by Phase 6's manual end-to-end verification,
  which must specifically confirm `/list_textures`' `format`/`regime`/
  `has_depth` values look plausible for `"GameView"`/`"SceneView"`/
  `"Swapchain"` (e.g. `"B8G8R8A8_UNORM"` or `"R8G8B8A8_UNORM"` for `format`,
  never `"VkFormat(...)"` for any of this engine's own real, known targets —
  a `"VkFormat(...)"` fallback showing up for one of THESE specific names
  would mean `DebugTextureColorFormatName()`'s own format list has drifted
  from what this engine's swapchain/render-textures actually negotiate).
- **Fast compile check**: `cmake --build build` must succeed before moving
  to Phase 6.
