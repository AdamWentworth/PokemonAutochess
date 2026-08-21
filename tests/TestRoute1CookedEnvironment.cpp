#include "engine/core/IAssetStore.h"
#include "engine/core/Paths.h"
#include "game/assets/DevAssetStore.h"
#include "game/runtime/shared/scene/Route1RuntimeEnvironment.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace {

class CookedSceneOnlyStore final : public engine::IAssetStore {
public:
    explicit CookedSceneOnlyStore(
        std::vector<std::uint8_t> sceneBytes)
        : sceneBytes_(std::move(sceneBytes)) {}

    bool readText(
        const std::string& virtualPath,
        std::string&,
        std::string* outError) const override {
        unexpectedReads_.push_back(virtualPath);
        if (outError) {
            *outError = "Cooked-scene host does not expose loose text assets.";
        }
        return false;
    }

    bool readBytes(
        const std::string& virtualPath,
        std::vector<std::uint8_t>& outBytes,
        std::string* outError) const override {
        if (virtualPath !=
            game::runtime::route1_environment::
                kCookedSceneArchivePath) {
            unexpectedReads_.push_back(virtualPath);
            if (outError) {
                *outError =
                    "Cooked-scene host rejected loose asset: " +
                    virtualPath;
            }
            return false;
        }
        ++sceneReads_;
        if (sceneBytes_.empty()) {
            if (outError) {
                *outError = "Cooked Route 1 PHSC is unavailable.";
            }
            return false;
        }
        outBytes = sceneBytes_;
        return true;
    }

    bool exists(const std::string& virtualPath) const override {
        return virtualPath ==
                   game::runtime::route1_environment::
                       kCookedSceneArchivePath &&
            !sceneBytes_.empty();
    }

    std::size_t sceneReads() const noexcept {
        return sceneReads_;
    }

    const std::vector<std::string>& unexpectedReads() const noexcept {
        return unexpectedReads_;
    }

private:
    std::vector<std::uint8_t> sceneBytes_;
    mutable std::size_t sceneReads_ = 0u;
    mutable std::vector<std::string> unexpectedReads_;
};

} // namespace

bool test_route1_cooked_environment_contract(std::string& outFail) {
    namespace route1 =
        game::runtime::route1_environment;

    game::assets::DevAssetStore workspace(
        engine::paths::dataRoot());
    std::vector<std::uint8_t> sceneBytes;
    std::string error;
    if (!workspace.readBytes(
            route1::kCookedSceneArchivePath,
            sceneBytes,
            &error) ||
        sceneBytes.empty()) {
        outFail =
            "Route 1 cooked-environment fixture is unavailable: " +
            error;
        return false;
    }

    CookedSceneOnlyStore isolatedHost(std::move(sceneBytes));
    route1::RuntimeEnvironment environment;
    std::size_t virtualFileCount = 0u;
    if (!route1::loadCookedEnvironment(
            isolatedHost,
            environment,
            &virtualFileCount,
            &error)) {
        outFail =
            "Route 1 PHSC did not load with every loose/source-cache "
            "read denied: " + error;
        return false;
    }

    const route1::RuntimeStats& stats = environment.stats();
    if (!environment.loaded() ||
        virtualFileCount == 0u ||
        stats.sceneCount == 0u ||
        stats.materialCount == 0u ||
        stats.visibleTriangleCount == 0u) {
        outFail =
            "Route 1 PHSC mounted without a complete renderable runtime "
            "environment.";
        return false;
    }
    if (isolatedHost.sceneReads() != 1u ||
        !isolatedHost.unexpectedReads().empty()) {
        outFail =
            "Route 1 cooked startup escaped the PHSC boundary and "
            "requested a loose/source-cache asset.";
        return false;
    }

    // Existing PHSC archives retain the source-era profile identifier while
    // project-owned board manifests use the neutral publication identifier.
    // Live editor preview must accept that same compatibility pair already
    // accepted by committed layout application.
    route1::BoardLayoutTransform neutralPreview = environment.layout();
    neutralPreview.sourceProfileId =
        "route1_environment_road001_00";
    error.clear();
    if (!environment.previewBoardLayout(neutralPreview, &error)) {
        outFail =
            "A neutral project-owned Route 1 layout could not preview "
            "against the compatible legacy cooked scene profile: " +
            error;
        return false;
    }

    CookedSceneOnlyStore missingHost({});
    virtualFileCount = 99u;
    error.clear();
    if (route1::loadCookedEnvironment(
            missingHost,
            environment,
            &virtualFileCount,
            &error) ||
        virtualFileCount != 0u ||
        error.find("PHSC") == std::string::npos ||
        !environment.loaded()) {
        outFail =
            "Missing Route 1 PHSC should fail actionably and preserve the "
            "last valid runtime environment.";
        return false;
    }

    return true;
}
