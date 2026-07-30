#include "engine/core/IAssetStore.h"
#include "engine/render/LgpeFieldSmallGrassMaterial.h"
#include "game/runtime/shared/scene/LgpeRoute1RuntimeEnvironment.h"

#include <cmath>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

class MemoryAssetStore final : public engine::IAssetStore {
public:
    bool readText(
        const std::string& virtualPath,
        std::string& outText,
        std::string* outError) const override {
        const auto found = texts.find(virtualPath);
        if (found == texts.end()) {
            if (outError) *outError = "missing text";
            return false;
        }
        outText = found->second;
        return true;
    }

    bool readBytes(
        const std::string&,
        std::vector<std::uint8_t>&,
        std::string* outError) const override {
        if (outError) *outError = "missing bytes";
        return false;
    }

    bool exists(const std::string& virtualPath) const override {
        return texts.contains(virtualPath);
    }

    std::unordered_map<std::string, std::string> texts;
};

std::array<float, 4> transformPoint(
    const std::array<float, 16>& matrix,
    const std::array<float, 4>& point) {
    std::array<float, 4> out{};
    for (std::size_t row = 0u; row < 4u; ++row) {
        for (std::size_t column = 0u; column < 4u; ++column) {
            out[row] +=
                matrix[column * 4u + row] * point[column];
        }
    }
    return out;
}

bool close(float lhs, float rhs) {
    return std::abs(lhs - rhs) <= 0.002f;
}

float dot(
    const std::array<float, 4>& lhs,
    const std::array<float, 4>& rhs) {
    float out = 0.0f;
    for (std::size_t index = 0u; index < lhs.size(); ++index) {
        out += lhs[index] * rhs[index];
    }
    return out;
}

} // namespace

bool test_lgpe_route1_runtime_environment_contract(std::string& outFail) {
    MemoryAssetStore store;
    store.texts["layout.json"] = R"json(
{
  "schema_version": 1,
  "kind": "lgpe_route1_board_layout_delta",
  "coordinate_system": "source_centimetres_xyz_y_up",
  "source_profile_id": "lgpe_route1_road001_00",
  "source_to_world": {
    "source_units_to_world": 0.01,
    "source_anchor_cm": [2200.0, 50.0, -1700.0],
    "world_anchor": [1.0, -0.04, 2.0],
    "yaw_degrees": 90.0
  },
  "local_layout_deltas": []
}
)json";

    using namespace game::runtime::lgpe_route1_runtime;
    BoardLayoutTransform layout;
    std::string error;
    if (!loadBoardLayoutTransform(
            store,
            "layout.json",
            layout,
            &error)) {
        outFail =
            "Route 1 runtime should load an explicit board-layout "
            "transform: " +
            error;
        return false;
    }
    if (layout.declaredLocalDeltaCount != 0u ||
        layout.sourceUnitsToWorld != 0.01f) {
        outFail =
            "Route 1 runtime should preserve source scale and record the "
            "declared local-delta count.";
        return false;
    }

    const auto worldFromSource = worldFromSourceMatrix(layout);
    const auto sourceFromWorld = sourceFromWorldMatrix(layout);
    const auto mappedAnchor = transformPoint(
        worldFromSource,
        {2200.0f, 50.0f, -1700.0f, 1.0f});
    if (!close(mappedAnchor[0], 1.0f) ||
        !close(mappedAnchor[1], -0.04f) ||
        !close(mappedAnchor[2], 2.0f) ||
        !close(mappedAnchor[3], 1.0f)) {
        outFail =
            "Route 1 source anchor should map exactly to the manifest's "
            "gameplay-world anchor.";
        return false;
    }
    const auto roundTrip =
        transformPoint(sourceFromWorld, mappedAnchor);
    if (!close(roundTrip[0], 2200.0f) ||
        !close(roundTrip[1], 50.0f) ||
        !close(roundTrip[2], -1700.0f) ||
        !close(roundTrip[3], 1.0f)) {
        outFail =
            "Route 1 board registration must have an exact inverse for "
            "source-space shadow projection.";
        return false;
    }
    const std::array<float, 4> sourceSample{
        2450.0f, 125.0f, -2050.0f, 1.0f};
    const auto worldSample =
        transformPoint(worldFromSource, sourceSample);
    const auto projectionRows = route1CloudProjectionRows(layout);
    const auto canonicalUv =
        engine::render::lgpe_field_small_grass::
            projectRoute1CloudTextureUv(
                {sourceSample[0], sourceSample[1], sourceSample[2]});
    if (!close(dot(projectionRows.u, worldSample), canonicalUv[0]) ||
        !close(
            1.0f - dot(projectionRows.v, worldSample),
            canonicalUv[1])) {
        outFail =
            "Route 1 cloud projection must remain source-identical after "
            "the manifest-owned gameplay transform.";
        return false;
    }

    store.texts["bad_layout.json"] = R"json(
{
  "schema_version": 1,
  "kind": "lgpe_route1_board_layout_delta",
  "coordinate_system": "guessed_world_units",
  "source_profile_id": "lgpe_route1_road001_00",
  "source_to_world": {
    "source_units_to_world": 0.01,
    "source_anchor_cm": [0, 0, 0],
    "world_anchor": [0, 0, 0],
    "yaw_degrees": 0
  },
  "local_layout_deltas": []
}
)json";
    if (loadBoardLayoutTransform(
            store,
            "bad_layout.json",
            layout,
            &error)) {
        outFail =
            "Route 1 runtime should reject an implicit or guessed source "
            "coordinate system.";
        return false;
    }
    return true;
}
