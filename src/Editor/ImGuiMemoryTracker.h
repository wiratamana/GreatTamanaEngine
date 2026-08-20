#pragma once

#include <cstddef>
#include <cstdint>

namespace gte {

// Byte-counting wrapper around Dear ImGui's own allocator (see
// ImGui::SetAllocatorFunctions(), imgui.h) - the same "how much memory does
// this dependency use" idea as SdlMemoryTracker (src/Memory/
// SdlMemoryTracker.h) and GpuMemoryTracker (src/Renderer/Memory/
// GpuMemoryTracker.h), surfaced as its own named bucket in the Editor's
// "Memory" panel (Panels/MemoryPanel.cpp). Lives under src/Editor/ (not
// src/Memory/) because its .cpp touches an ImGui header directly, and is
// therefore only ever compiled when GTE_ENABLE_EDITOR is ON - see AGENTS.md,
// "Editor Module Structure".
//
// MUST be installed before ImGui::CreateContext() - Dear ImGui's own
// convention for SetAllocatorFunctions() (some of a context's own internal
// state is allocated the moment it's created). ImGuiEditorLayer's
// constructor (ImGuiEditorLayer.cpp) is the only place this engine ever
// creates an ImGui context, and installs this first.
//
// Not an instance/RAII type: like SdlMemoryTracker, ImGuiMemAllocFunc/
// ImGuiMemFreeFunc (imgui.h) carry only a `void* user_data` (never a
// `this`), and this engine only ever has one ImGui context anyway - a
// static/process-global counter is the simplest correct fit, same reasoning
// as SdlMemoryTracker.
class ImGuiMemoryTracker {
public:
    // Installs the tracking allocator via ImGui::SetAllocatorFunctions().
    // Safe to call more than once - every call after the first is a no-op.
    static void Install();

    // Live totals across every still-outstanding ImGui allocation - O(1),
    // safe to call every frame. Both are 0 if Install() was never called.
    static std::uint64_t LiveBytes() noexcept;
    static std::uint64_t LiveAllocationCount() noexcept;

private:
    static void* TrackedAlloc(std::size_t size, void* userData);
    static void TrackedFree(void* ptr, void* userData);
};

} // namespace gte
