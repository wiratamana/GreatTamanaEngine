# PHASE10 — `ISurfaceProvider` Interface + Window Dependency Inversion

## Parent
`PHASE0_MASTER_STRATEGY.md`. Depends on Phase 9 (two real targets must
already exist).

## Step 1: The Goal

`Renderer`/`VulkanSurface` depend on an abstract `ISurfaceProvider`
interface instead of the concrete `Window` class. `Window` implements it.
This is the seam `Core` (Phase 12) will be constructed through, and the
seam a future headless test fixture (Phase 18) and a future Player host
will use instead of a real `Window`.

## Step 2: The Situation / The Problem

Read `src/Window/Window.h` in full (current state). Confirm it already
forward-declares `struct SDL_Window;` and redefines `VkInstance`/
`VkSurfaceKHR` as opaque pointer typedefs (design doc Section 2.4 says this
is already true — verify, don't assume). Confirm `VulkanInstanceExtensions()`
is currently `static` (design doc Section 5.2 says so) — find every call
site of the static form (likely inside `VulkanInstance.cpp` — confirm via
`search_in_dir` for `Window::VulkanInstanceExtensions`).

## Step 3: The Plan

1. Create `src/Core/ISurfaceProvider.h` (new, `gte_core`-owned):
   ```cpp
   namespace gte {
   class ISurfaceProvider {
   public:
       virtual ~ISurfaceProvider() = default;
       virtual std::vector<std::string> VulkanInstanceExtensions() const = 0;
       virtual VkSurfaceKHR CreateVulkanSurface(VkInstance instance) const = 0;
       virtual int Width() const noexcept = 0;
       virtual int Height() const noexcept = 0;
   };
   } // namespace gte
   ```
   Reuse `Window`'s EXISTING method names/signatures verbatim (this is a
   deliberate minimal-diff choice per the design doc — do not rename
   anything on `Window`'s own public API, do not change return types).
2. Make `Window` inherit from `ISurfaceProvider` (`class Window :
   public ISurfaceProvider { ... }` — confirm this doesn't conflict with
   any existing base class `Window` already has).
3. Convert `Window::VulkanInstanceExtensions()` from `static` to a real,
   virtual instance method. Update every call site found in Step 2's search
   (likely `VulkanInstance.cpp`) to call it on a live `Window`/
   `ISurfaceProvider&` instance instead of the old static form.
4. Find every place `Renderer`/`VulkanSurface` (or whatever the real
   surface-creation code is called — confirm exact file via
   `search_in_dir` for `CreateVulkanSurface`) currently takes a `Window&`
   or `Window*` parameter, and change that parameter type to
   `ISurfaceProvider&`/`ISurfaceProvider*`. This should be a narrow,
   mechanical signature change if `Window`'s public methods already match
   the interface exactly (per Step 1's deliberate design).
5. Compile-check: incremental build (`gte_core` + `gte_editor`, since
   `Window` lives in `gte_editor` after Phase 9's move — confirm this is
   actually true post-Phase-9, since Phase 9 only moved the OLD
   `GTE_ENABLE_EDITOR`-gated file list, and `Window.cpp` was NOT in that
   list per the design doc's own Section 2.2 inventory — meaning `Window`
   currently still lives in `gte_core` at this point in the campaign. This
   is fine and expected: `ISurfaceProvider` is defined in `gte_core`,
   `Window` (still physically in `gte_core` for now) implements it, no
   contradiction yet. Phase 14 is what actually MOVES `Window.cpp` into
   `gte_editor`.)
6. Live smoke check: `run_app_background`, confirm the engine still boots
   and renders identically (this phase changes zero rendering behavior,
   purely an interface-inversion refactor).

## Files Touched

- NEW `src/Core/ISurfaceProvider.h`
- `src/Window/Window.h`/`.cpp`
- `VulkanInstance.cpp` (or real file name/path, confirmed during execution)
- Whatever `Renderer`/`VulkanSurface` file(s) take a `Window&` parameter
  today (confirmed during execution)

## Definition of Done

- `ISurfaceProvider` exists, `Window` implements it, every prior
  `Window&`-taking surface/renderer call site now takes `ISurfaceProvider&`
  instead.
- Incremental build + live boot/render smoke check both succeed.
- `PHASE10_COMPLETION_REPORT.md` + git commit.

## Out of Scope

Do not move `Window.cpp`/`SdlContext` out of `gte_core` yet — that is
Phase 14. This phase only inverts the DEPENDENCY DIRECTION (interface
first), physical file relocation comes later.
