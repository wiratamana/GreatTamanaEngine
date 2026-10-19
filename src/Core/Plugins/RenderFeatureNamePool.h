#pragma once

#include <deque>
#include <string>
#include <unordered_map>

namespace gte {

// Interns the compositor's per-(feature, view) target and pass names. Graph names must outlive the frame,
// so each name gets a process-lifetime c_str(). The key universe is bounded: a fixed set of views and
// feature or project-slot keys.
class RenderFeatureNamePool {
public:
    // "<key>_<view>_Private": the feature's own offscreen target.
    const char* PrivateName(const std::string& key, const std::string& viewName)
    {
        return Lookup(key + "_" + viewName + "_Private");
    }

    // "<key>_<view>_Accum": the blend accumulator (the last blend writes the view target instead).
    const char* AccumName(const std::string& key, const std::string& viewName)
    {
        return Lookup(key + "_" + viewName + "_Accum");
    }

    // "<key>_<view>_Blend": the feature's blend pass.
    const char* BlendPassName(const std::string& key, const std::string& viewName)
    {
        return Lookup(key + "_" + viewName + "_Blend");
    }

    // "RenderFeatureCompositor_<view>_Seed": the per-view copy the blend chain starts from.
    const char* SeedName(const std::string& viewName)
    {
        return Lookup("RenderFeatureCompositor_" + viewName + "_Seed");
    }

    // "RenderFeatureCompositor_<view>_SeedCopy": the pass that writes the seed.
    const char* SeedCopyPassName(const std::string& viewName)
    {
        return Lookup("RenderFeatureCompositor_" + viewName + "_SeedCopy");
    }

    // "RenderFeatureCompositor_<view>_FinalCopy": copies the running result to the view target when the last entry is skipped.
    const char* FinalCopyPassName(const std::string& viewName)
    {
        return Lookup("RenderFeatureCompositor_" + viewName + "_FinalCopy");
    }

private:
    const char* Lookup(const std::string& key)
    {
        const auto existing = m_index.find(key);
        if (existing != m_index.end()) {
            return existing->second;
        }
        m_storage.push_back(key);
        const char* interned = m_storage.back().c_str();
        m_index.emplace(key, interned);
        return interned;
    }

    std::deque<std::string> m_storage; // A deque never moves an element, so handed-out pointers stay valid.
    std::unordered_map<std::string, const char*> m_index;
};

} // namespace gte
