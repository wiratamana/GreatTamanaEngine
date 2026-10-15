#include "Channel.h"

#include "ChannelKey.h"
#include "FrameContext.h"
#include "../../Renderer/RenderGraph/RenderPipeline.h"

#include <cassert>
#include <string>

namespace gte::fx::detail {

namespace {

// The blackboard slot type: a wrapper, because std::any(std::any) would unwrap instead of nest.
struct ChannelValue {
    std::any value;
};

// Notes the name; a second name with the same hash would silently share a slot.
void CheckClash(const Frame& frame, FrameContext& context, std::string_view channelName)
{
    if (context.channels == nullptr) {
        return;
    }
    const std::uint64_t key = ChannelKey(context.featureName, channelName, rg::RenderViewId::Shared());
    if (context.channels->Note(key, context.featureName, channelName) == ChannelNames::Result::Clash) {
        assert(false && "two channel names share one key hash");
        frame.ReportOnce("channel '" + std::string(context.featureName) + "." + std::string(channelName)
            + "' has the same key hash as another channel of this feature");
    }
}

} // namespace

void PublishAny(Frame& frame, std::string_view channelName, std::any value, std::span<const Texture> keep)
{
    FrameContext* context = frame.Context();
    if (context == nullptr || context->blackboard == nullptr) {
        frame.ReportOnce("Channel::Publish needs a frame that belongs to a render callback");
        return;
    }
    CheckClash(frame, *context, channelName);
    for (const Texture& texture : keep) {
        if (!frame.Owns(texture)) {
            frame.ReportOnce("Channel '" + std::string(channelName) + "': a kept texture is invalid or from another frame callback");
        } else if (context->builder != nullptr) {
            context->builder->KeepTextureOutput(texture.handle);
        }
    }
    const rg::RenderPassId key{ ChannelKey(context->featureName, channelName, context->view) };
    context->blackboard->Publish<ChannelValue>(key, ChannelValue{ std::move(value) });
}

std::optional<std::any> FetchAny(const Frame& frame, std::string_view channelName)
{
    FrameContext* context = frame.Context();
    if (context == nullptr || context->blackboard == nullptr) {
        frame.ReportOnce("Channel::Fetch needs a frame that belongs to a render callback");
        return std::nullopt;
    }
    CheckClash(frame, *context, channelName);
    const rg::RenderPassId key{ ChannelKey(context->featureName, channelName, context->view) };
    std::optional<ChannelValue> stored = context->blackboard->Fetch<ChannelValue>(key);
    if (!stored.has_value()) {
        return std::nullopt;
    }
    return std::move(stored->value);
}

void ReportChannelTypeMismatch(const Frame& frame, std::string_view channelName)
{
    frame.ReportOnce("Channel '" + std::string(channelName) + "' holds another type than the one asked for");
}

} // namespace gte::fx::detail
