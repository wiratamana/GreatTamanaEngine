#pragma once

#include "RenderGraphHandles.h"
#include "../../Core/GraphicsHandles.h"

#include <cstdint>
#include <memory>

namespace gte {
class Renderer;
class RenderTexture;
class Texture2D;
struct VolumeTarget;
} // namespace gte

namespace gte::rg {

class RenderGraphBuilder;
struct ResourceState;

// What a one-shot run leaves an imported texture in. Restore returns it to the state it had when imported.
enum class FinalState : std::uint8_t { Restore, ShaderRead, Keep };

// The frame graph's pass chains, run once outside the frame loop: import textures, declare passes on Builder(),
// then Run() submits and waits. Blocks the CPU, so never per frame. Passes may not use attachments.
// Example: ComputePass(os.Builder(), shader, "Preview", scope, event).Bind("src", a).Bind("dst", b).Dispatch(size);
class OneShot {
public:
    explicit OneShot(Renderer& renderer);
    ~OneShot();

    OneShot(const OneShot&) = delete;
    OneShot& operator=(const OneShot&) = delete;
    OneShot(OneShot&&) = delete;
    OneShot& operator=(OneShot&&) = delete;

    // The texture's tracked state is its state now; Run() writes the final state back, only after the wait.
    TextureHandle Import(RenderTexture& texture, FinalState final = FinalState::Restore);
    TextureHandle Import(Texture2D& texture, FinalState final = FinalState::Restore);

    // A texture the caller may not modify: its state is restored, so nothing is written back.
    // The second form states the texture's state, for textures whose tracked state is unknown.
    TextureHandle ImportReadOnly(const RenderTexture& texture);
    TextureHandle ImportReadOnly(const RenderTexture& texture, const ResourceState& current);

    // A volume whose state the caller tracks. `sampler` lets passes bind it by name as a sampled texture.
    TextureHandle Import(const VolumeTarget& volume, const ResourceState& current, SamplerHandle sampler,
        FinalState final = FinalState::Restore);

    // Copies one 2D color image to another of the same size and format; both need the transfer usage bits.
    bool CopyTexture(TextureHandle src, TextureHandle dst);

    // Declare compute passes on this builder, exactly as in a frame.
    RenderGraphBuilder& Builder() noexcept;

    // Submits and waits. False (one logged error) on failure; imported textures then keep their old tracked state.
    bool Run();

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace gte::rg
