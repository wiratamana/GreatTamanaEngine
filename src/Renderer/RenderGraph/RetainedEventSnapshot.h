#pragma once

// Opaque owner of one retained Frame Debugger event image. Code outside src/Renderer only stores and passes it;
// the image type, its destructor and every GPU call live in RenderGraphEventSnapshotPool.cpp.

#include <memory>

namespace gte::rg {

class EventSnapshotResource;

class RetainedEventSnapshot {
public:
    RetainedEventSnapshot();
    ~RetainedEventSnapshot();

    RetainedEventSnapshot(const RetainedEventSnapshot&) = delete;
    RetainedEventSnapshot& operator=(const RetainedEventSnapshot&) = delete;
    RetainedEventSnapshot(RetainedEventSnapshot&&) noexcept;
    RetainedEventSnapshot& operator=(RetainedEventSnapshot&&) noexcept;

    // Renderer-internal: the image, created empty on first use.
    EventSnapshotResource& Resource();
    // Renderer-internal: null until a capture created the image.
    const EventSnapshotResource* Get() const noexcept { return m_resource.get(); }

private:
    std::unique_ptr<EventSnapshotResource> m_resource;
};

} // namespace gte::rg
