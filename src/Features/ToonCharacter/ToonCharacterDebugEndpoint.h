#pragma once

#include "ToonCharacterTypes.h"
#include "../../Core/FeatureKit/FeatureDebug.h"

#include <optional>
#include <span>
#include <string>

namespace gte {

class ToonCharacterFeature;

struct ToonDebugArgError {
    std::string key;
    std::string message;
};

// Parses `args` into `candidate` (unsanitized). The first bad key or value gives the error; `candidate` is then
// partly written, so callers discard it.
std::optional<ToonDebugArgError> ParseToonCharacterDebugArgs(
    ToonCharacterSettings& candidate, std::span<const fx::FeatureDebugArg> args);

// Text-driven read and tune access to the ToonCharacter feature. Uses only its public API.
class ToonCharacterDebugEndpoint final : public fx::IFeatureDebugEndpoint {
public:
    explicit ToonCharacterDebugEndpoint(ToonCharacterFeature& feature) noexcept : m_feature(feature) { }

    fx::FeatureDebugResult GetState() const override;
    fx::FeatureDebugResult Set(std::span<const fx::FeatureDebugArg> args) override;
    fx::FeatureDebugResult Invoke(std::string_view action, std::span<const fx::FeatureDebugArg> args) override;

private:
    ToonCharacterFeature& m_feature;
};

} // namespace gte
