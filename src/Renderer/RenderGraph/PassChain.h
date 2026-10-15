#pragma once

#include "RenderGraphHandles.h"
#include "Shader.h"
#include "StaticName.h"
#include "../../Core/GraphicsHandles.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace gte::rg {

class RenderGraphBuilder;
struct BindingPlan;

// Marks a texture binding as a depth sampler.
struct DepthRef {
    TextureHandle texture;
};

inline DepthRef Depth(TextureHandle texture) noexcept
{
    return DepthRef{ texture };
}

// State shared by every shader-bound pass chain: the bound arguments, push bytes and the chain's outcome.
// A chain over a shader that is not ready is inert: it declares nothing and stays silent.
struct PassChainState {
    struct Argument {
        std::string_view name;
        TextureHandle texture{};
        BufferObjectHandle buffer = BufferObjectHandle::Invalid;
        bool isBuffer = false;
        bool isDepth = false;
    };

    PassChainState(
        RenderGraphBuilder& builder, Shader& shader, ShaderKind kind, StaticName name, ViewScope scope, RenderPassEvent event);

    void Add(const Argument& argument)
    {
        if (!inert) {
            arguments.push_back(argument);
        }
    }

    void SetPushBytes(const void* data, std::size_t size)
    {
        if (!inert) {
            push.resize(size);
            if (size != 0) {
                std::memcpy(push.data(), data, size);
            }
        }
    }

    template <typename T>
    void SetPush(const T& value)
    {
        static_assert(std::is_trivially_copyable_v<T>, "push constants are copied as raw bytes");
        SetPushBytes(&value, sizeof(T));
    }

    // Logs "Pass '<name>': <text>" once per distinct message for the graph's lifetime.
    void Report(const std::string& text);

    // Starts the terminal call (Dispatch/Draw). False when the chain declares nothing: a second terminal call,
    // an inert chain, or an earlier failure.
    bool Begin(const char* terminal);

    // Reports a chain that ended without its terminal call.
    void ReportUnfinished(const char* terminal);

    // Resolves the bound arguments against the shader. Reports and returns nullopt on any problem.
    std::optional<BindingPlan> BuildPlan();

    RenderGraphBuilder& builder;
    Shader& shader;
    StaticName name;
    ViewScope scope;
    RenderPassEvent event;
    std::vector<Argument> arguments;
    std::vector<std::byte> push;
    bool inert = false;
    bool failed = false;
    bool finished = false;
};

// Chainable Bind/Push calls shared by ComputePass and FullscreenPass; each returns the derived type.
template <typename Derived>
class PassChain {
public:
    Derived& Bind(std::string_view name, TextureHandle texture)
    {
        m_state.Add(PassChainState::Argument{ name, texture, BufferObjectHandle::Invalid, false, false });
        return Self();
    }

    Derived& Bind(std::string_view name, DepthRef depth)
    {
        m_state.Add(PassChainState::Argument{ name, depth.texture, BufferObjectHandle::Invalid, false, true });
        return Self();
    }

    Derived& Bind(std::string_view name, BufferObjectHandle buffer)
    {
        m_state.Add(PassChainState::Argument{ name, TextureHandle{}, buffer, true, false });
        return Self();
    }

    // Push-constant block; its size must match the shader's.
    template <typename T>
    Derived& Push(const T& value)
    {
        m_state.SetPush(value);
        return Self();
    }
    Derived& PushBytes(const void* data, std::size_t size)
    {
        m_state.SetPushBytes(data, size);
        return Self();
    }

    // Makes the terminal call declare nothing, without a "forgotten terminal call" report.
    Derived& Abandon()
    {
        m_state.failed = true;
        return Self();
    }

protected:
    PassChain(RenderGraphBuilder& builder, Shader& shader, ShaderKind kind, StaticName name, ViewScope scope,
        RenderPassEvent event)
        : m_state(builder, shader, kind, name, scope, event)
    {
    }
    ~PassChain() = default;

    PassChainState m_state;

private:
    Derived& Self() noexcept { return static_cast<Derived&>(*this); }
};

} // namespace gte::rg
