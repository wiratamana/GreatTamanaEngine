#include "ChannelKey.h"

#include "../../Renderer/RenderGraph/RenderPipeline.h"

#include <array>
#include <cstring>
#include <string>

namespace gte::fx {

std::uint64_t ChannelKey(std::string_view featureName, std::string_view channelName, rg::RenderViewId view)
{
    // Short names are joined on the stack; only a long name allocates.
    std::array<char, 128> buffer;
    const std::size_t total = featureName.size() + 1 + channelName.size();
    if (total <= buffer.size()) {
        std::memcpy(buffer.data(), featureName.data(), featureName.size());
        buffer[featureName.size()] = '.';
        std::memcpy(buffer.data() + featureName.size() + 1, channelName.data(), channelName.size());
        return rg::SceneResourceKeyOf(std::string_view(buffer.data(), total), view).hash;
    }
    std::string joined(featureName);
    joined += '.';
    joined += channelName;
    return rg::SceneResourceKeyOf(joined, view).hash;
}

} // namespace gte::fx
