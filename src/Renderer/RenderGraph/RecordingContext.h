#pragma once

// Vulkan-free handle to the command buffer being recorded. Code outside src/Renderer carries this
// instead of a VkCommandBuffer; only src/Renderer can open it (see RecordingAccess.h).

namespace gte {
class Renderer;
}

namespace gte::rg {
class CommandBuffer;
struct PassContext;
struct RecordingAccess;

class RecordingContext {
public:
    RecordingContext() noexcept = default;

    explicit operator bool() const noexcept { return m_native != nullptr; }

    // High-level recorder for this command buffer. Inside a pass it carries that pass's draw stats and
    // frame-debugger sink; outside a pass it carries neither.
    CommandBuffer Commands() const noexcept;

    // Opaque backend handle for the ImGui Vulkan backend glue only; nothing else may call this.
    void* PlatformHandle() const noexcept { return m_native; }

private:
    friend struct RecordingAccess;

    RecordingContext(void* native, Renderer* renderer, const PassContext* pass) noexcept
        : m_native(native), m_renderer(renderer), m_pass(pass)
    {
    }

    void* m_native = nullptr;
    Renderer* m_renderer = nullptr;
    const PassContext* m_pass = nullptr; // Valid only while that pass executes.
};

} // namespace gte::rg
