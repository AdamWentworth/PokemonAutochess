#include "game/runtime/shared/projected/core/SharedProjectedRenderItems.h"

#include <string>

bool test_shared_projected_render_items_cpu_rewrite_cache_contract(std::string& outFail) {
    using namespace game::runtime::shared_projected_render_items;

    ProjectedRenderItemRegistry registry;
    beginProjectedRenderItemsFrame(registry);

    ProjectedRenderItemKey key{};
    key.unitId = 25;
    key.itemIndex = 3u;

    auto& entry = ensureProjectedRenderItem(registry, key);
    if (entry.cpuRewriteGeometryTemplateIdentity != nullptr ||
        entry.cpuRewritePoseHash != 0ull ||
        entry.cpuRewriteNeedsLitNormals != 0u ||
        entry.cpuRewriteNeedsTangents != 0u ||
        !entry.cpuRewriteVertices.empty()) {
        outFail = "ProjectedRenderItems should initialize CPU rewrite cache state empty.";
        return false;
    }

    entry.cpuRewriteGeometryTemplateIdentity = reinterpret_cast<const void*>(0x1234);
    entry.cpuRewritePoseHash = 0xfeedbeefull;
    entry.cpuRewriteNeedsLitNormals = 1u;
    entry.cpuRewriteNeedsTangents = 1u;
    entry.cpuRewriteVertices.resize(4u);

    ProjectedRenderItemKey qualityVariantKey = key;
    qualityVariantKey.materialVariant = 6u;
    auto& qualityVariantEntry =
        ensureProjectedRenderItem(registry, qualityVariantKey);
    if (&qualityVariantEntry == &entry || registry.entries.size() != 2u) {
        outFail =
            "ProjectedRenderItems should keep material-quality variants distinct.";
        return false;
    }

    resetProjectedRenderItems(registry);
    if (!registry.entries.empty() || registry.currentFrameId != 0u) {
        outFail = "ProjectedRenderItems reset should clear cached CPU rewrite geometry with the registry.";
        return false;
    }

    return true;
}

