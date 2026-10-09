# AGENTS.md

Instructions for LLM/AI agents working on this codebase.

## Documentation

**DO NOT UPDATE AGENTS.md**: Human will update this. AI dont update this.

This file covers universal coding guidelines and testability rules, plus a
short summary + link for every subsystem-specific convention. Full detail
for each subsystem lives under [`docs/conventions/`](docs/README.md).

## Coding Guidelines

- **Clean Architecture**: Write clean architecture code. Keep clear
  separation of concerns between layers (e.g. SDL -> Application -> Window
  and Renderer -> Game). Lower-level/core layers must not depend on
  higher-level or framework-specific details. Only the `Application` layer
  should know about SDL directly; other layers must go through the custom
  abstraction objects (Window, Renderer, etc.).
- **RAII**: Every resource-owning piece of code must use RAII (Resource
  Acquisition Is Initialization). Resources (SDL handles, memory, file
  handles, GPU objects, etc.) must be acquired in constructors and released
  in destructors, so lifetime is tied to object scope and cleanup is
  automatic and exception/error-safe. Avoid manual/explicit cleanup calls
  scattered through the code — wrap raw resources in owning types instead.
- **Namespace**: Every new script (every class/function/type this project
  defines) must live inside the `gte` namespace (short for Great Tamana
  Engine), e.g. `namespace gte { class Window { ... }; }`. This keeps engine
  symbols from colliding with SDL's or third-party globals.

# Development & Coding Guidelines

Core Requirements:

1. Ruthless Code Cleanup & Modern Replacement:
   - Identify and delete all bad code, anti-patterns, bloated wrappers, and spaghetti logic.
   - Replace deleted code with minimal, robust, and clean implementations.
   - Remove all dead code, unused structs, and leftover test code.
   - If a test might fails to compile or fails to pass because of deleted code, you are allowed to update or delete that test.
   - Keep the test suite green after each step.
   - Focus on making the build succeed. Proceed with the implementation now.

2. Modern C++ Standard (C++20):
   - Use Modern C++ (C++20) idioms.
   - Enforce strict RAII for all native handles and resources (zero memory leaks, zero resource leaks).
   - Prefer flat data structures, simple functions, value semantics, and `std::span` over complex class hierarchies.
   - Do not use raw `new`/`delete` or useless wrapper classes.

3. Modern Vulkan Standards (Vulkan 1.3 Dynamic Rendering):
   - Apply only if the code related with rendering stuffs
   - Strictly use Vulkan Dynamic Rendering (`vkCmdBeginRendering` / `vkCmdEndRendering` with `VkRenderingInfo`).
   - Do NOT use legacy `VkRenderPass` or `VkFramebuffer` objects. Remove all old render pass boilerplate.
   - Use modern synchronization (Vulkan Synchronization2 / explicit pipeline barriers with minimal stalls).
   - Use dynamic state (dynamic viewport, scissor, depth bias) to avoid pipeline bloat.

4. Concurrency & Performance:
   - Zero resource leaks and rock-solid error checking.
   - Thread safety: no race conditions, no hidden shared state, no lock contention.
   - Design for memory alignment and cache locality.

5. AI Debug Capabilities
   - Focus code on AI driven debugging flow with existing HTTP request.
   - If debugging context involve visual confirmation, you must also provide a way to effectively get the image of the context without relying on taking screenshot entire screen.
