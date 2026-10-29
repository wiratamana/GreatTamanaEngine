#pragma once

#include <string>

namespace gte {

// Optional display name of an entity. Plain data.
// An empty or missing Name makes the panel show an "Entity N" label.
struct Name {
    std::string value;
};

} // namespace gte
