#include "SceneJsonFormat.h"

namespace gte {

std::string SerializeSceneDocument(const SceneDocument& document)
{
    nlohmann::json root = nlohmann::json::object();
    root["gtscene_version"] = kSceneJsonFormatVersion;

    nlohmann::json entities = nlohmann::json::array();

    for (const SceneEntityRecord& record : document.entities) {
        nlohmann::json entityJson = nlohmann::json::object();
        entityJson["parent"] = record.parentIndex.has_value() ? nlohmann::json(*record.parentIndex) : nlohmann::json(nullptr);
        entityJson["sibling_index"] = record.siblingIndex;
        // "asset_guid" is only emitted when actually present, per this
        // format's own spec (Appendix A - "OPTIONAL. Present ONLY for a
        // root entity that was spawned from a MeshAssetSource 'recipe'") -
        // this phase never populates it (always empty), but the shape is
        // written correctly now so PHASE4 needs no format-level change.
        if (!record.assetGuid.empty()) {
            entityJson["asset_guid"] = record.assetGuid;
        }
        entityJson["components"] = record.components;
        entities.push_back(std::move(entityJson));
    }

    root["entities"] = std::move(entities);
    return root.dump(2);
}

std::optional<SceneDocument> DeserializeSceneDocument(const std::string& text)
{
    // Non-throwing parse mode, matching Network/NetworkRoutes.cpp's own
    // ParseJsonNoThrow() convention (checked before writing this file) -
    // consistent across the codebase per this phase's own instruction.
    const nlohmann::json root = nlohmann::json::parse(text, /*callback*/ nullptr, /*allow_exceptions*/ false);
    if (root.is_discarded() || !root.is_object()) {
        return std::nullopt;
    }

    // Everything past this point is defensively wrapped in a try/catch as
    // well - nlohmann::json's own .get<T>() can still throw for a
    // value/type mismatch we didn't already explicitly check for (e.g. an
    // integer literal too large for the target type) - this must never
    // escape as an exception across this function's own public API (see
    // PHASE0's Cross-Phase Invariant: "never crash, never throw").
    try {
        if (!root.contains("gtscene_version") || !root["gtscene_version"].is_number_integer()) {
            return std::nullopt;
        }
        if (root["gtscene_version"].get<int>() != kSceneJsonFormatVersion) {
            return std::nullopt;
        }

        if (!root.contains("entities") || !root["entities"].is_array()) {
            return std::nullopt;
        }

        SceneDocument document;
        const nlohmann::json& entitiesJson = root["entities"];
        document.entities.reserve(entitiesJson.size());

        for (const nlohmann::json& entityJson : entitiesJson) {
            if (!entityJson.is_object()) {
                return std::nullopt;
            }

            SceneEntityRecord record;

            if (entityJson.contains("parent") && !entityJson["parent"].is_null()) {
                const nlohmann::json& parentJson = entityJson["parent"];
                if (!parentJson.is_number_integer() || parentJson.get<long long>() < 0) {
                    return std::nullopt;
                }
                record.parentIndex = static_cast<std::size_t>(parentJson.get<unsigned long long>());
            }

            if (entityJson.contains("sibling_index")) {
                const nlohmann::json& siblingJson = entityJson["sibling_index"];
                if (!siblingJson.is_number_integer() || siblingJson.get<long long>() < 0) {
                    return std::nullopt;
                }
                record.siblingIndex = siblingJson.get<std::uint32_t>();
            }

            if (entityJson.contains("asset_guid")) {
                if (!entityJson["asset_guid"].is_string()) {
                    return std::nullopt;
                }
                record.assetGuid = entityJson["asset_guid"].get<std::string>();
            }

            if (entityJson.contains("components")) {
                if (!entityJson["components"].is_object()) {
                    return std::nullopt;
                }
                record.components = entityJson["components"];
            }

            document.entities.push_back(std::move(record));
        }

        // Cross-record validation - a parentIndex can only be checked for
        // range AFTER the full array size is known.
        for (const SceneEntityRecord& record : document.entities) {
            if (record.parentIndex.has_value() && *record.parentIndex >= document.entities.size()) {
                return std::nullopt;
            }
        }

        return document;
    } catch (const nlohmann::json::exception&) {
        return std::nullopt;
    }
}

} // namespace gte
