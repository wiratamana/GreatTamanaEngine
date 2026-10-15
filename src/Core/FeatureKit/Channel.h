#pragma once

#include "Frame.h"
#include "Texture.h"

#include <any>
#include <initializer_list>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace gte::fx {

namespace detail {

// Stores `value` under the channel's key for the frame's view and keeps every `keep` texture alive.
void PublishAny(Frame& frame, std::string_view channelName, std::any value, std::span<const Texture> keep);

// The value published this frame under the channel's key, or nothing.
std::optional<std::any> FetchAny(const Frame& frame, std::string_view channelName);

// Logs once that the channel holds another type than the reader asked for.
void ReportChannelTypeMismatch(const Frame& frame, std::string_view channelName);

} // namespace detail

// A typed hand-off between two passes of one feature, per view. The key is "<Feature>.<name>".
// A disabled pass never reaches Publish (its whole callback is gated), so nothing stale is read.
template <typename T>
class Channel {
public:
    explicit Channel(std::string name)
        : m_name(std::move(name))
    {
    }

    // Textures in `keep` stay alive even when no later pass reads them.
    void Publish(Frame& frame, const T& value, std::initializer_list<Texture> keep = {})
    {
        detail::PublishAny(frame, m_name, std::any(value), std::span<const Texture>(keep.begin(), keep.size()));
    }

    std::optional<T> Fetch(const Frame& frame) const
    {
        const std::optional<std::any> stored = detail::FetchAny(frame, m_name);
        if (!stored.has_value()) {
            return std::nullopt;
        }
        if (const T* value = std::any_cast<T>(&*stored)) {
            return *value;
        }
        detail::ReportChannelTypeMismatch(frame, m_name);
        return std::nullopt;
    }

private:
    std::string m_name;
};

} // namespace gte::fx
