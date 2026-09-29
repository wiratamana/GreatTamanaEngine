#pragma once

#include <deque>
#include <string>
#include <unordered_map>

// editor-core-separation-6 campaign, PHASE4
// (PHASE4_RENDER_FEATURE_COMPOSITOR_CORE_AND_ORDERING.md, Step 3.3) - a
// small, direct adaptation of Core.cpp's own anonymous-namespace
// GpuDrivenBatchNamePool: interns stable, whole-process-lifetime `const
// char*` names for RenderFeatureCompositor's own per-(plugin, view) private/
// accumulator/blend-pass slots and per-view seed/seed-copy slots. A
// RenderGraphBuilder::CreateTexture()/ImportTexture()/AddRenderPass() `name`
// must be a string literal (or otherwise static-storage-duration) `const
// char*` (see RenderGraphBuilder.h's own doc comment) - a plain
// std::string's own `c_str()` is NOT safe to hand to any of those calls
// directly, since the std::string itself would go out of scope. This pool
// exists purely to give RenderFeatureCompositor a safe, stable pointer for
// every one of these dynamically-discovered (plugin, view) combinations,
// mirroring GpuDrivenBatchNamePool's own `std::deque<std::string>` "never
// invalidates an already-handed-out c_str() pointer on further insertion"
// discipline exactly.
//
// The set of loaded _v2 plugins AND the set of known views ("Game"/"Scene")
// are both fixed for the process's entire remaining lifetime (PluginHost
// never unloads before process exit; no third named view is ever introduced
// without an engine-wide RenderViewId::Named() change).
//
// editor-core-separation-23 campaign, PHASE2
// (PHASE2_REGISTER_PROJECT_FEATURE_AND_SLOT_POOL.md, Step 3.4) - a second
// consumer, RenderFeatureCompositor's own Project Assembly render-feature
// path, DOES hot-reload repeatedly over one session - but it never feeds a
// raw, unbounded plugin/project NAME into this pool. It feeds a synthetic
// "ProjectFeatureSlot0".."ProjectFeatureSlot15" key instead
// (kMaxConcurrentProjectRenderFeatures = 16, RenderFeatureCompositor.h) - a
// small, FIXED, bounded universe of possible keys for the process's entire
// lifetime, which is what actually makes this pool's own "look up once,
// lazily, and reuse forever, never re-interned per frame" discipline safe
// for that consumer too. The real invariant this whole pool depends on is
// therefore "the set of distinct KEYS ever looked up is bounded for the
// process's entire lifetime" - not "nothing here is ever reloaded", which is
// no longer an accurate description of every consumer.

namespace gte {

class RenderFeatureNamePool {
public:
    // "<pluginName>_<viewName>_Private" - this plugin's own private
    // offscreen target for this view.
    const char* PrivateName(const std::string& pluginName, const std::string& viewName)
    {
        return Lookup(pluginName + "_" + viewName + "_Private");
    }

    // "<pluginName>_<viewName>_Accum" - the accumulator this plugin's own
    // blend pass writes into for this view (never used for the LAST entry
    // in a view's combined list, which writes directly into the view's own
    // real composited handle instead - see RenderFeatureCompositor.cpp).
    const char* AccumName(const std::string& pluginName, const std::string& viewName)
    {
        return Lookup(pluginName + "_" + viewName + "_Accum");
    }

    // "<pluginName>_<viewName>_Blend" - this plugin's own blend-compositing
    // pass name for this view.
    const char* BlendPassName(const std::string& pluginName, const std::string& viewName)
    {
        return Lookup(pluginName + "_" + viewName + "_Blend");
    }

    // "RenderFeatureCompositor_<viewName>_Seed" - one per VIEW only, shared
    // by whichever combined plugin list runs for that view this frame.
    const char* SeedName(const std::string& viewName)
    {
        return Lookup("RenderFeatureCompositor_" + viewName + "_Seed");
    }

    // "RenderFeatureCompositor_<viewName>_SeedCopy" - the pass name for the
    // one-time-per-view-per-frame dispatch that seeds the chain above.
    const char* SeedCopyPassName(const std::string& viewName)
    {
        return Lookup("RenderFeatureCompositor_" + viewName + "_SeedCopy");
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

    std::deque<std::string> m_storage; // Never invalidates an already-handed-out c_str() pointer.
    std::unordered_map<std::string, const char*> m_index;
};

} // namespace gte
