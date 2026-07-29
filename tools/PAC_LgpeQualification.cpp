#define SDL_MAIN_HANDLED

#include "engine/assets/lgpe/LgpeCanonicalScene.h"
#include "engine/core/Paths.h"
#include "engine/platform/Window.h"
#include "engine/render/LgpeFieldCliffMaterial.h"
#include "engine/render/LgpeFieldEncounterGrassMaterial.h"
#include "engine/render/LgpeFieldFlowerMaterial.h"
#include "engine/render/LgpeFieldGrassMaterial.h"
#include "engine/render/LgpeFieldGroundMaterial.h"
#include "engine/render/LgpeFieldOverlayMaterial.h"
#include "engine/render/LgpeFieldRockMaterial.h"
#include "engine/render/LgpeFieldSignMaterial.h"
#include "engine/render/LgpeFieldSmallGrassMaterial.h"
#include "engine/render/LgpeFieldObjectTreeMikiMaterial.h"
#include "engine/render/LgpeFieldTree02Material.h"
#include "engine/render/LgpeFieldTree05Material.h"
#include "engine/render/OpenGLRenderBackend.h"
#include "game/assets/DevAssetStore.h"
#include "game/runtime/shared/scene/LgpeWorldSceneAdapter.h"
#include "game/runtime/shared/scene/SharedWorldScene.h"

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <SDL2/SDL.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <cmath>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

struct CameraPreset {
    const char *name = "middle";
    glm::vec3 eye{};
    glm::vec3 target{};
};

using GridCell = std::pair<int, int>;

struct EncounterGrassPlacement {
    std::array<float, 3> center{};
    float phaseCycles = 0.0f;
};

struct EncounterGrassLayer {
    std::string logicalName;
    engine::assets::lgpe::CanonicalScene source;
    game::runtime::lgpe_world_scene::PreparedScene scene;
    std::vector<EncounterGrassPlacement> placements;
    std::vector<std::vector<float>> skinPalettes;
    std::size_t instanceCount = 0u;
};

struct PlacedVegetationSourceDraw {
    IRenderBackend::WorldSceneRenderObjectHandle objectHandle{};
    std::array<float, 16> modelMatrix{};
};

struct PlacedVegetationLayer {
    std::string logicalName;
    engine::assets::lgpe::CanonicalScene source;
    game::runtime::lgpe_world_scene::PreparedScene scene;
    std::vector<std::array<float, 16>> modelMatrices;
    std::vector<PlacedVegetationSourceDraw> sourceDraws;
    std::vector<float> skinPalette;
    std::size_t instanceCount = 0u;
};

std::vector<EncounterGrassPlacement> expandedEncounterGrassPlacements(
    const nlohmann::json &record) {
    const auto &coreJson = record.at("core_cells_source_xz");
    std::set<GridCell> core;
    for (const auto &cell : coreJson) {
        core.emplace(cell.at(0).get<int>(), cell.at(1).get<int>());
    }
    if (core.empty()) {
        throw std::runtime_error("Encounter-grass collision footprint is empty.");
    }

    std::set<GridCell> expanded;
    for (const auto &cell : core) {
        for (int dx = -1; dx <= 1; ++dx) {
            for (int dz = -1; dz <= 1; ++dz) {
                expanded.emplace(cell.first + dx, cell.second + dz);
            }
        }
    }

    const auto translation =
        record.at("translation_cm").get<std::array<float, 3>>();
    const int recordIndex = record.at("record_index").get<int>();
    std::vector<EncounterGrassPlacement> placements;
    placements.reserve(expanded.size());
    for (const auto &cell : expanded) {
        float gridX = static_cast<float>(cell.first);
        float gridZ = static_cast<float>(cell.second);
        if (core.find(cell) == core.end()) {
            const auto nearest = std::min_element(
                core.begin(),
                core.end(),
                [&](const GridCell &lhs, const GridCell &rhs) {
                    const int lhsDx = lhs.first - cell.first;
                    const int lhsDz = lhs.second - cell.second;
                    const int rhsDx = rhs.first - cell.first;
                    const int rhsDz = rhs.second - cell.second;
                    const auto lhsKey = std::array<int, 3>{
                        lhsDx * lhsDx + lhsDz * lhsDz,
                        lhs.first,
                        lhs.second};
                    const auto rhsKey = std::array<int, 3>{
                        rhsDx * rhsDx + rhsDz * rhsDz,
                        rhs.first,
                        rhs.second};
                    return lhsKey < rhsKey;
                });
            gridX = 0.5f * (gridX + static_cast<float>(nearest->first));
            gridZ = 0.5f * (gridZ + static_cast<float>(nearest->second));
        }
        EncounterGrassPlacement placement;
        placement.center = {
            translation[0] + (gridX + 0.5f) * 100.0f,
            translation[1],
            translation[2] + (gridZ + 0.5f) * 100.0f};
        placement.phaseCycles = std::fmod(
            static_cast<float>(recordIndex) * 0.173f +
                gridX * 0.127f + gridZ * 0.193f,
            1.0f);
        if (placement.phaseCycles < 0.0f) {
            placement.phaseCycles += 1.0f;
        }
        placements.push_back(placement);
    }
    return placements;
}

std::vector<float> encounterGrassSkinPalette(
    engine::render::lgpe_field_encounter_grass::SourceVariant variant,
    std::size_t jointCount,
    float placementPhaseCycles,
    float windPhaseCycles) {
    std::vector<float> palette(jointCount * 16u, 0.0f);
    for (std::size_t joint = 0u; joint < jointCount; ++joint) {
        // The shader proves that deformation enters through JointMatrixVS.
        // Its missing CPU producer is reconstructed from the accepted Route 1
        // phase field and eight captured gameplay samples. Source weights
        // still determine every vertex's final response.
        const auto rotation =
            engine::render::lgpe_field_encounter_grass::
                evaluateWindJointRotation(
                    static_cast<std::uint32_t>(joint),
                    placementPhaseCycles,
                    windPhaseCycles);
        const auto pivotValues =
            engine::render::lgpe_field_encounter_grass::
                sourceJointPivot(
                    variant,
                    static_cast<std::uint32_t>(joint));
        const glm::vec3 pivot{
            pivotValues[0],
            pivotValues[1],
            pivotValues[2]};
        // Blender evidence composes Y-bend then X-cross in its Z-up basis.
        // Converted back to the source Y-up basis, that is -Z bend then X.
        const glm::mat4 jointMatrix =
            glm::translate(glm::mat4(1.0f), pivot) *
            glm::rotate(
                glm::mat4(1.0f),
                -rotation.bendRadians,
                glm::vec3(0.0f, 0.0f, 1.0f)) *
            glm::rotate(
                glm::mat4(1.0f),
                rotation.crossRadians,
                glm::vec3(1.0f, 0.0f, 0.0f)) *
            glm::translate(glm::mat4(1.0f), -pivot);
        std::copy(
            glm::value_ptr(jointMatrix),
            glm::value_ptr(jointMatrix) + 16u,
            palette.data() + joint * 16u);
    }
    return palette;
}

void placeEncounterGrassLayer(
    EncounterGrassLayer &layer,
    const std::vector<EncounterGrassPlacement> &placements,
    float windPhaseCycles) {
    std::vector<IRenderBackend::WorldSceneRenderObjectHandle> objects;
    objects.reserve(layer.scene.frame.drawClasses.size());
    for (const auto &drawClass : layer.scene.frame.drawClasses) {
        objects.push_back(drawClass.objectHandle);
    }

    game::runtime::shared_world_scene::beginWorldSceneFrame(layer.scene.frame);
    layer.skinPalettes.clear();
    layer.skinPalettes.reserve(placements.size());
    const auto variant =
        layer.logicalName == "enc_grass02"
            ? engine::render::lgpe_field_encounter_grass::
                  SourceVariant::Grass02
            : engine::render::lgpe_field_encounter_grass::
                  SourceVariant::Grass01;
    std::uint32_t instanceId = 1u;
    for (const auto &placement : placements) {
        std::array<float, 16> modelMatrix{
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            placement.center[0],
            placement.center[1],
            placement.center[2],
            1.0f};
        layer.skinPalettes.push_back(encounterGrassSkinPalette(
            variant,
            layer.source.bones.size(),
            placement.phaseCycles,
            windPhaseCycles));
        const auto &palette = layer.skinPalettes.back();
        for (const auto objectHandle : objects) {
            IRenderBackend::WorldSceneRenderInstanceHandle handle{};
            handle.id = instanceId++;
            game::runtime::shared_world_scene::appendSkinnedInstance(
                layer.scene.frame,
                objectHandle,
                handle,
                modelMatrix,
                1.0f,
                1.0f,
                1.0f,
                1.0f,
                0.0f,
                0u,
                static_cast<std::uint32_t>(layer.source.bones.size()),
                palette.data());
        }
    }
    layer.instanceCount = placements.size();
}

std::vector<EncounterGrassLayer> loadRoute1EncounterGrass(
    game::assets::DevAssetStore &store,
    const std::string &virtualRoot,
    float windPhaseCycles) {
    std::vector<EncounterGrassLayer> layers;
    if (virtualRoot != "cache/lgpe/route1") {
        return layers;
    }

    const std::filesystem::path compositionPath =
        std::filesystem::path(engine::paths::dataRoot()) /
        "tools" / "lgpe_importer" / "route1.composition.json";
    std::ifstream input(compositionPath);
    if (!input) {
        throw std::runtime_error(
            "Could not open Route 1 composition manifest: " +
            compositionPath.string());
    }
    nlohmann::json root;
    input >> root;
    const auto &encounter = root.at("encounter_grass");
    const auto &modelRoots = encounter.at("models");

    std::map<std::string, std::vector<EncounterGrassPlacement>>
        placementsByModel;
    for (const auto &record : encounter.at("records")) {
        auto expanded = expandedEncounterGrassPlacements(record);
        auto &modelPlacements =
            placementsByModel[record.at("model").get<std::string>()];
        modelPlacements.insert(
            modelPlacements.end(), expanded.begin(), expanded.end());
    }

    layers.reserve(placementsByModel.size());
    for (const auto &[logicalName, placements] : placementsByModel) {
        // Prepare directly in the reserved destination. SharedWorldScene's
        // render-object cache is keyed by registry address, so preparing a
        // reusable stack temporary would let the following layer inherit a
        // stale handle when that stack address is recycled.
        layers.emplace_back();
        auto &layer = layers.back();
        layer.logicalName = logicalName;
        const std::string modelRoot =
            modelRoots.at(logicalName).get<std::string>();
        std::string error;
        if (!engine::assets::lgpe::loadCanonicalScene(
                store, modelRoot, layer.source, &error)) {
            throw std::runtime_error(
                "Could not load " + logicalName + ": " + error);
        }
        if (!game::runtime::lgpe_world_scene::prepareCanonicalScene(
                layer.source, layer.scene, &error)) {
            throw std::runtime_error(
                "Could not prepare " + logicalName + ": " + error);
        }
        if (layer.scene.stats.fieldEncounterGrassSurfaceMaterialCount != 1u ||
            layer.scene.frame.drawClasses.size() != 1u) {
            throw std::runtime_error(
                logicalName +
                " did not prepare as one FieldEncGrassShader01 draw class.");
        }
        layer.placements = placements;
        placeEncounterGrassLayer(
            layer,
            layer.placements,
            windPhaseCycles);
    }
    return layers;
}

std::array<float, 16> sourcePlacementMatrix(const nlohmann::json &placement) {
    const auto translation =
        placement.at("translation_cm").get<std::array<float, 3>>();
    const auto rotation =
        placement.at("rotation_degrees").get<std::array<float, 3>>();
    const auto scale = placement.at("scale").get<std::array<float, 3>>();
    const glm::mat4 matrix =
        glm::translate(
            glm::mat4(1.0f),
            glm::vec3(translation[0], translation[1], translation[2])) *
        glm::rotate(
            glm::mat4(1.0f),
            glm::radians(rotation[0]),
            glm::vec3(1.0f, 0.0f, 0.0f)) *
        glm::rotate(
            glm::mat4(1.0f),
            glm::radians(rotation[1]),
            glm::vec3(0.0f, 1.0f, 0.0f)) *
        glm::rotate(
            glm::mat4(1.0f),
            glm::radians(rotation[2]),
            glm::vec3(0.0f, 0.0f, 1.0f)) *
        glm::scale(
            glm::mat4(1.0f),
            glm::vec3(scale[0], scale[1], scale[2]));
    std::array<float, 16> result{};
    std::copy(
        glm::value_ptr(matrix),
        glm::value_ptr(matrix) + result.size(),
        result.begin());
    return result;
}

std::vector<float> vegetationSkinPalette(
    const engine::assets::lgpe::CanonicalScene &source,
    float windPhaseCycles) {
    std::vector<float> palette(source.bones.size() * 16u, 0.0f);
    std::vector<glm::mat4> restWorld(
        source.bones.size(),
        glm::mat4(1.0f));
    for (std::size_t index = 0u; index < source.bones.size(); ++index) {
        const auto &bone = source.bones[index];
        const glm::quat restRotation(
            bone.rotation[3],
            bone.rotation[0],
            bone.rotation[1],
            bone.rotation[2]);
        const glm::mat4 local =
            glm::translate(
                glm::mat4(1.0f),
                glm::vec3(
                    bone.position[0],
                    bone.position[1],
                    bone.position[2])) *
            glm::mat4_cast(restRotation) *
            glm::scale(
                glm::mat4(1.0f),
                glm::vec3(
                    bone.scale[0],
                    bone.scale[1],
                    bone.scale[2]));
        restWorld[index] =
            bone.parentIndex >= 0 &&
                    static_cast<std::size_t>(bone.parentIndex) < index
                ? restWorld[static_cast<std::size_t>(bone.parentIndex)] *
                      local
                : local;

        glm::mat4 jointMatrix(1.0f);
        constexpr std::string_view kGrassJointPrefix = "grass_joint";
        if (bone.name.rfind(kGrassJointPrefix, 0u) == 0u) {
            std::uint32_t componentIndex = 1u;
            const std::string suffix =
                bone.name.substr(kGrassJointPrefix.size());
            if (!suffix.empty() &&
                std::all_of(
                    suffix.begin(),
                    suffix.end(),
                    [](unsigned char value) {
                        return std::isdigit(value) != 0;
                    })) {
                componentIndex =
                    static_cast<std::uint32_t>(std::stoul(suffix)) + 1u;
            }
            // All three build-model assets resolve to the same exact skinned
            // vertex program used by the Route 1 vegetation family. Reuse
            // the accepted capture-bounded joint curve, but rotate around
            // each asset's own authored skeleton pivot.
            const auto rotation =
                engine::render::lgpe_field_encounter_grass::
                    evaluateWindJointRotation(
                        componentIndex,
                        0.0f,
                        windPhaseCycles);
            const glm::vec3 pivot(restWorld[index][3]);
            jointMatrix =
                glm::translate(glm::mat4(1.0f), pivot) *
                glm::rotate(
                    glm::mat4(1.0f),
                    -rotation.bendRadians,
                    glm::vec3(0.0f, 0.0f, 1.0f)) *
                glm::rotate(
                    glm::mat4(1.0f),
                    rotation.crossRadians,
                    glm::vec3(1.0f, 0.0f, 0.0f)) *
                glm::translate(glm::mat4(1.0f), -pivot);
        }
        std::copy(
            glm::value_ptr(jointMatrix),
            glm::value_ptr(jointMatrix) + 16u,
            palette.data() + index * 16u);
    }
    return palette;
}

void placeVegetationLayer(
    PlacedVegetationLayer &layer,
    float windPhaseCycles) {
    if (layer.sourceDraws.empty()) {
        layer.sourceDraws.reserve(layer.scene.frame.drawClasses.size());
        for (const auto &drawClass : layer.scene.frame.drawClasses) {
            if (drawClass.instances.size() != 1u) {
                throw std::runtime_error(
                    layer.logicalName +
                    " source draw does not contain one authored mesh transform.");
            }
            layer.sourceDraws.push_back(
                {drawClass.objectHandle,
                 drawClass.instances.front().modelMatrix});
            if (drawClass.objectHandle.id > 0u &&
                drawClass.objectHandle.id <=
                    layer.scene.registry.renderObjects.size()) {
                layer.scene.registry.renderObjects[
                    drawClass.objectHandle.id - 1u]
                    .skinned = true;
            }
        }
    }

    game::runtime::shared_world_scene::beginWorldSceneFrame(layer.scene.frame);
    layer.skinPalette =
        vegetationSkinPalette(layer.source, windPhaseCycles);
    std::uint32_t instanceId = 1u;
    for (const auto &placementMatrix : layer.modelMatrices) {
        for (const auto &sourceDraw : layer.sourceDraws) {
            const glm::mat4 modelMatrix =
                glm::make_mat4(placementMatrix.data()) *
                glm::make_mat4(sourceDraw.modelMatrix.data());
            std::array<float, 16> composedMatrix{};
            std::copy(
                glm::value_ptr(modelMatrix),
                glm::value_ptr(modelMatrix) + composedMatrix.size(),
                composedMatrix.begin());
            IRenderBackend::WorldSceneRenderInstanceHandle handle{};
            handle.id = instanceId++;
            game::runtime::shared_world_scene::appendSkinnedInstance(
                layer.scene.frame,
                sourceDraw.objectHandle,
                handle,
                composedMatrix,
                1.0f,
                1.0f,
                1.0f,
                1.0f,
                0.0f,
                0u,
                static_cast<std::uint32_t>(layer.source.bones.size()),
                layer.skinPalette.data());
        }
    }
    layer.instanceCount = layer.modelMatrices.size();
}

std::vector<PlacedVegetationLayer> loadRoute1PlacedVegetation(
    game::assets::DevAssetStore &store,
    const std::string &virtualRoot,
    float windPhaseCycles) {
    std::vector<PlacedVegetationLayer> layers;
    if (virtualRoot != "cache/lgpe/route1") {
        return layers;
    }

    const std::filesystem::path dataRoot(engine::paths::dataRoot());
    const std::filesystem::path compositionPath =
        dataRoot / "tools" / "lgpe_importer" / "route1.composition.json";
    std::ifstream compositionInput(compositionPath);
    if (!compositionInput) {
        throw std::runtime_error(
            "Could not open Route 1 composition manifest: " +
            compositionPath.string());
    }
    nlohmann::json composition;
    compositionInput >> composition;
    const auto &vegetation = composition.at("buildmodel_vegetation");
    const auto placementRelativePath =
        vegetation.at("placement_manifest").get<std::string>();
    const auto expectedInstanceCount =
        vegetation.at("expected_instance_count").get<std::size_t>();
    const std::filesystem::path placementPath =
        dataRoot / std::filesystem::path(placementRelativePath);
    std::ifstream placementInput(placementPath);
    if (!placementInput) {
        throw std::runtime_error(
            "Could not open Route 1 build-model placements: " +
            placementPath.string());
    }
    nlohmann::json placementRoot;
    placementInput >> placementRoot;
    if (placementRoot.at("coordinate_system") !=
            "source_centimetres_xyz_y_up" ||
        placementRoot.at("instance_count").get<std::size_t>() !=
            expectedInstanceCount) {
        throw std::runtime_error(
            "Route 1 build-model placement contract changed.");
    }

    const auto &models = placementRoot.at("models");
    const std::array<std::string, 3> logicalNames{
        "grass02", "flowers02", "flowers04"};
    layers.reserve(logicalNames.size());
    std::size_t loadedInstanceCount = 0u;
    for (const auto &logicalName : logicalNames) {
        const auto &model = models.at(logicalName);
        const std::string modelRoot =
            model.at("cache_root").get<std::string>();
        const std::size_t expectedModelInstances =
            model.at("instance_count").get<std::size_t>();

        layers.emplace_back();
        auto &layer = layers.back();
        layer.logicalName = logicalName;
        std::string error;
        if (!engine::assets::lgpe::loadCanonicalScene(
                store, modelRoot, layer.source, &error)) {
            throw std::runtime_error(
                "Could not load " + logicalName + ": " + error);
        }
        if (!game::runtime::lgpe_world_scene::prepareCanonicalScene(
                layer.source, layer.scene, &error)) {
            throw std::runtime_error(
                "Could not prepare " + logicalName + ": " + error);
        }
        if (logicalName == "grass02") {
            if (layer.scene.stats.fieldTree02SurfaceMaterialCount != 1u) {
                throw std::runtime_error(
                    "grass02 did not prepare as FieldTreeShader02.");
            }
        } else if (layer.scene.stats.fieldFlowerSurfaceMaterialCount != 1u) {
            throw std::runtime_error(
                logicalName + " did not prepare as FieldObjectShader flower.");
        }

        for (const auto &placement : model.at("placements")) {
            layer.modelMatrices.push_back(sourcePlacementMatrix(placement));
        }
        if (layer.modelMatrices.size() != expectedModelInstances) {
            throw std::runtime_error(
                logicalName + " placement count changed.");
        }
        placeVegetationLayer(layer, windPhaseCycles);
        loadedInstanceCount += layer.instanceCount;
    }
    if (loadedInstanceCount != expectedInstanceCount) {
        throw std::runtime_error(
            "Route 1 build-model vegetation instance count changed.");
    }
    return layers;
}

CameraPreset cameraPreset(const std::string &name) {
    if (name == "trunk") {
        return {
            "trunk",
            {2300.0f, 430.0f, -2100.0f},
            {3300.0f, 320.0f, -3000.0f}};
    }
    if (name == "tree") {
        return {"tree", {3300.0f, 850.0f, -900.0f}, {3300.0f, 180.0f, -2400.0f}};
    }
    if (name == "canopy") {
        return {
            "canopy",
            {2500.0f, 900.0f, -2050.0f},
            {2050.0f, 360.0f, -3000.0f}};
    }
    if (name == "vegetation") {
        return {
            "vegetation",
            {1950.0f, 760.0f, -850.0f},
            {1950.0f, 130.0f, -1800.0f}};
    }
    if (name == "sign") {
        return {
            "sign",
            {2037.5f, 290.0f, -390.0f},
            {2037.5f, 68.0f, -773.5f}};
    }
    if (name == "south") {
        return {"south", {1800.0f, 700.0f, -150.0f}, {1800.0f, 90.0f, -900.0f}};
    }
    if (name == "north") {
        return {"north", {2500.0f, 850.0f, -2350.0f}, {2500.0f, 210.0f, -3200.0f}};
    }
    return {"middle", {2200.0f, 800.0f, -1200.0f}, {2200.0f, 150.0f, -2100.0f}};
}

const IRenderBackend::WorldSceneRenderObject *renderObject(
    const game::runtime::shared_world_scene::WorldSceneRegistry &registry,
    IRenderBackend::WorldSceneRenderObjectHandle handle) {
    if (handle.id == 0u || handle.id > registry.renderObjects.size()) return nullptr;
    return &registry.renderObjects[handle.id - 1u];
}

const IRenderBackend::WorldSceneGeometry *geometry(
    const game::runtime::shared_world_scene::WorldSceneRegistry &registry,
    IRenderBackend::WorldSceneGeometryHandle handle) {
    if (handle.id == 0u || handle.id > registry.geometries.size()) return nullptr;
    return &registry.geometries[handle.id - 1u];
}

const IRenderBackend::WorldSceneMaterial *material(
    const game::runtime::shared_world_scene::WorldSceneRegistry &registry,
    IRenderBackend::WorldSceneMaterialHandle handle) {
    if (handle.id == 0u || handle.id > registry.materials.size()) return nullptr;
    return &registry.materials[handle.id - 1u];
}

} // namespace

int main(int argc, char **argv) {
    const std::string virtualRoot =
        argc >= 2 && argv[1] && argv[1][0] != '\0'
            ? argv[1]
            : "cache/lgpe/route1";
    const CameraPreset camera =
        cameraPreset(argc >= 3 && argv[2] ? argv[2] : "middle");
    const std::string rawMaterialFilter =
        argc >= 4 && argv[3] &&
                std::string(argv[3]).rfind("--", 0u) != 0u
            ? argv[3]
            : "";
    const std::string materialFilter =
        rawMaterialFilter == "-" ? "" : rawMaterialFilter;
    bool interactive = false;
    int interactiveFrameLimit = 0;
    for (int index = 1; index < argc; ++index) {
        if (!argv[index]) {
            continue;
        }
        const std::string argument(argv[index]);
        if (argument == "--interactive") {
            interactive = true;
        } else if (argument.rfind("--interactive-frames=", 0u) == 0u) {
            interactive = true;
            interactiveFrameLimit =
                std::max(1, std::stoi(argument.substr(21u)));
        }
    }
    const float encounterWindPhaseCycles =
        argc >= 5 && argv[4] &&
                std::string(argv[4]).rfind("--", 0u) != 0u
            ? std::stof(argv[4])
            : 36.0f / 120.0f;

    game::assets::DevAssetStore store(engine::paths::dataRoot());
    engine::assets::lgpe::CanonicalScene source;
    std::string error;
    if (!engine::assets::lgpe::loadCanonicalScene(
            store, virtualRoot, source, &error)) {
        std::cerr << "[PAC_LgpeQualification] FAIL root=" << virtualRoot
                  << " error=" << error << '\n';
        return 1;
    }

    game::runtime::lgpe_world_scene::PreparedScene scene;
    if (!game::runtime::lgpe_world_scene::prepareCanonicalScene(
            source, scene, &error)) {
        std::cerr << "[PAC_LgpeQualification] FAIL root=" << virtualRoot
                  << " world_scene_error=" << error << '\n';
        return 1;
    }

    try {
        auto encounterGrassLayers =
            loadRoute1EncounterGrass(
                store,
                virtualRoot,
                encounterWindPhaseCycles);
        auto placedVegetationLayers =
            loadRoute1PlacedVegetation(
                store,
                virtualRoot,
                encounterWindPhaseCycles);
        std::vector<const game::runtime::lgpe_world_scene::PreparedScene *>
            renderScenes;
        renderScenes.reserve(
            1u + encounterGrassLayers.size() +
            placedVegetationLayers.size());
        renderScenes.push_back(&scene);
        for (const auto &layer : encounterGrassLayers) {
            renderScenes.push_back(&layer.scene);
        }
        for (const auto &layer : placedVegetationLayers) {
            renderScenes.push_back(&layer.scene);
        }

        Window window(
            interactive
                ? "LGPE Route 1 interactive engine review"
                : "LGPE Route 1 direct-source qualification",
            1280,
            720,
            Window::GraphicsApi::OpenGL,
            interactive);
        if (!gladLoadGLLoader(
                reinterpret_cast<GLADloadproc>(SDL_GL_GetProcAddress))) {
            throw std::runtime_error("Failed to initialize GLAD.");
        }

        int width = 0;
        int height = 0;
        window.getDrawableSize(width, height);
        width = width > 0 ? width : 1280;
        height = height > 0 ? height : 720;
        glViewport(0, 0, width, height);

        OpenGLRenderBackend renderer;
        renderer.onResize(width, height);
        renderer.prewarmWorldRenderAssets();

        CameraPreset activeCamera = camera;
        glm::mat4 viewProjection(1.0f);
        std::array<float, 3> cameraWorld{};
        std::array<float, 3> cameraForward{};
        std::array<float, 3> cameraTarget{};
        auto updateCameraState = [&]() {
            const glm::mat4 projection = glm::perspective(
                glm::radians(35.0f),
                static_cast<float>(width) / static_cast<float>(height),
                10.0f,
                10000.0f);
            const glm::mat4 view = glm::lookAt(
                activeCamera.eye,
                activeCamera.target,
                glm::vec3(0.0f, 1.0f, 0.0f));
            viewProjection = projection * view;
            const glm::vec3 forward =
                glm::normalize(activeCamera.target - activeCamera.eye);
            cameraWorld = {
                activeCamera.eye.x,
                activeCamera.eye.y,
                activeCamera.eye.z};
            cameraForward = {forward.x, forward.y, forward.z};
            cameraTarget = {
                activeCamera.target.x,
                activeCamera.target.y,
                activeCamera.target.z};
        };
        updateCameraState();

        std::uint32_t groundGroups = 0u;
        std::uint64_t groundTriangles = 0u;
        std::uint32_t cliffGroups = 0u;
        std::uint64_t cliffTriangles = 0u;
        std::uint32_t grass01Groups = 0u;
        std::uint64_t grass01Triangles = 0u;
        std::uint32_t grass02Groups = 0u;
        std::uint64_t grass02Triangles = 0u;
        std::uint32_t grass04Groups = 0u;
        std::uint64_t grass04Triangles = 0u;
        std::uint32_t grass05Groups = 0u;
        std::uint64_t grass05Triangles = 0u;
        std::uint32_t roadstoneGroups = 0u;
        std::uint64_t roadstoneTriangles = 0u;
        std::uint32_t rockMaskGroups = 0u;
        std::uint64_t rockMaskTriangles = 0u;
        std::uint32_t flowerGroups = 0u;
        std::uint64_t flowerTriangles = 0u;
        std::uint32_t rockGroups = 0u;
        std::uint64_t rockTriangles = 0u;
        std::uint32_t signGroups = 0u;
        std::uint64_t signTriangles = 0u;
        std::uint32_t tree02Groups = 0u;
        std::uint64_t tree02Triangles = 0u;
        std::uint32_t tree04Groups = 0u;
        std::uint64_t tree04Triangles = 0u;
        std::uint32_t tree05Groups = 0u;
        std::uint64_t tree05Triangles = 0u;
        std::uint32_t treeMikiGroups = 0u;
        std::uint64_t treeMikiTriangles = 0u;
        std::uint32_t encounterGrassGroups = 0u;
        std::uint64_t encounterGrassTriangles = 0u;
        std::array<std::uint32_t, 6> groundMipCounts{};
        std::array<std::uint32_t, 5> cliffMipCounts{};
        std::array<std::uint32_t, 6> grass01MipCounts{};
        std::array<std::uint32_t, 6> grass02MipCounts{};
        std::array<std::uint32_t, 5> grass04MipCounts{};
        std::array<std::uint32_t, 6> grass05MipCounts{};
        std::array<std::uint32_t, 2> roadstoneMipCounts{};
        std::array<std::uint32_t, 6> rockMaskMipCounts{};
        std::array<std::uint32_t, 2> flowerMipCounts{};
        std::array<std::uint32_t, 6> rockMipCounts{};
        std::array<std::uint32_t, 2> signMipCounts{};
        std::array<std::uint32_t, 6> tree02MipCounts{};
        std::array<std::uint32_t, 6> tree04MipCounts{};
        std::array<std::uint32_t, 6> tree05MipCounts{};
        std::array<std::uint32_t, 4> treeMikiMipCounts{};
        std::array<std::uint32_t, 4> encounterGrassMipCounts{};
        bool collectStats = true;
        auto renderFrame = [&]() {
            renderer.beginFrame(0.075f, 0.09f, 0.065f, 1.0f);
            renderer.beginWorldIndexedBatchSubmission();
            for (const auto *renderScene : renderScenes) {
                for (const auto &drawClass : renderScene->frame.drawClasses) {
                    const auto *object =
                        renderObject(renderScene->registry, drawClass.objectHandle);
                    if (!object) continue;
                    const auto *mesh =
                        geometry(renderScene->registry, object->geometryHandle);
                    const auto *surface =
                        material(renderScene->registry, object->materialHandle);
                    if (!mesh || !surface) {
                        continue;
                    }
                    if (!materialFilter.empty() &&
                        surface->sourceMaterialName != materialFilter) {
                        continue;
                    }
                    const bool isGround =
                        surface->materialMode ==
                        engine::render::lgpe_field_ground::kMaterialMode;
                    const bool isCliff =
                        surface->materialMode ==
                        engine::render::lgpe_field_cliff::kMaterialMode;
                    const bool isGrass01 =
                        surface->materialMode ==
                        engine::render::lgpe_field_grass::kShader01MaterialMode;
                    const bool isGrass02 =
                        surface->materialMode ==
                        engine::render::lgpe_field_grass::kShader02MaterialMode;
                    const bool isGrass04 =
                        surface->materialMode ==
                        engine::render::lgpe_field_small_grass::
                            kShader04MaterialMode;
                    const bool isGrass05 =
                        surface->materialMode ==
                        engine::render::lgpe_field_small_grass::
                            kShader05MaterialMode;
                    const bool isRoadstone =
                        surface->materialMode ==
                        engine::render::lgpe_field_overlay::
                            kRoadstoneMaterialMode;
                    const bool isRockMask =
                        surface->materialMode ==
                        engine::render::lgpe_field_overlay::
                            kRockMaskMaterialMode;
                    const bool isFlower =
                        surface->materialMode ==
                            engine::render::lgpe_field_flower::kMaterialMode ||
                        surface->materialMode ==
                            engine::render::lgpe_field_flower::
                                kBuildmodelMaterialMode;
                    const bool isRock =
                        surface->materialMode ==
                        engine::render::lgpe_field_rock::kMaterialMode;
                    const bool isSign =
                        surface->materialMode ==
                        engine::render::lgpe_field_sign::kMaterialMode;
                    const bool isTree02 =
                        surface->materialMode ==
                            engine::render::lgpe_field_tree02::kMaterialMode ||
                        surface->materialMode ==
                            engine::render::lgpe_field_tree02::
                                kGrass02MaterialMode;
                    const bool isTree04 =
                        surface->sourceShaderGroup == "FieldTreeShader04" &&
                        surface->materialMode ==
                            engine::render::lgpe_field_tree05::kMaterialMode;
                    const bool isTree05 =
                        surface->sourceShaderGroup == "FieldTreeShader05" &&
                        surface->materialMode ==
                            engine::render::lgpe_field_tree05::kMaterialMode;
                    const bool isTreeMiki =
                        surface->materialMode ==
                        engine::render::lgpe_field_object_tree_miki::kMaterialMode;
                    const bool isEncounterGrass =
                        surface->materialMode ==
                        engine::render::lgpe_field_encounter_grass::kMaterialMode;
                    if (!isGround && !isCliff && !isGrass01 && !isGrass02 &&
                        !isGrass04 && !isGrass05 && !isRoadstone &&
                        !isRockMask && !isFlower && !isRock && !isSign && !isTree02 &&
                        !isTree04 && !isTree05 && !isTreeMiki &&
                        !isEncounterGrass) {
                        continue;
                    }

                    IRenderBackend::WorldTextureData texture =
                        game::runtime::shared_world_scene::makeWorldSceneTextureData(
                            *surface,
                            cameraWorld.data(),
                            cameraForward.data(),
                            cameraTarget.data());
                    if (isGround) {
                        groundMipCounts = {
                            texture.mipLevelCount,
                            texture.normalMipLevelCount,
                            texture.metallicRoughnessMipLevelCount,
                            texture.occlusionMipLevelCount,
                            texture.emissiveMipLevelCount,
                            texture.environmentMipLevelCount};
                    } else if (isCliff) {
                        cliffMipCounts = {
                            texture.mipLevelCount,
                            texture.normalMipLevelCount,
                            texture.metallicRoughnessMipLevelCount,
                            texture.occlusionMipLevelCount,
                            texture.emissiveMipLevelCount};
                    } else if (isGrass01) {
                        grass01MipCounts = {
                            texture.mipLevelCount,
                            texture.normalMipLevelCount,
                            texture.metallicRoughnessMipLevelCount,
                            texture.occlusionMipLevelCount,
                            texture.emissiveMipLevelCount,
                            texture.environmentMipLevelCount};
                    } else if (isGrass02) {
                        grass02MipCounts = {
                            texture.mipLevelCount,
                            texture.normalMipLevelCount,
                            texture.metallicRoughnessMipLevelCount,
                            texture.occlusionMipLevelCount,
                            texture.emissiveMipLevelCount,
                            texture.environmentMipLevelCount};
                    } else if (isGrass04) {
                        grass04MipCounts = {
                            texture.mipLevelCount,
                            texture.normalMipLevelCount,
                            texture.metallicRoughnessMipLevelCount,
                            texture.occlusionMipLevelCount,
                            texture.emissiveMipLevelCount};
                    } else if (isGrass05) {
                        grass05MipCounts = {
                            texture.mipLevelCount,
                            texture.normalMipLevelCount,
                            texture.metallicRoughnessMipLevelCount,
                            texture.occlusionMipLevelCount,
                            texture.emissiveMipLevelCount,
                            texture.environmentMipLevelCount};
                    } else if (isRoadstone) {
                        roadstoneMipCounts = {
                            texture.mipLevelCount,
                            texture.occlusionMipLevelCount};
                    } else if (isRockMask) {
                        rockMaskMipCounts = {
                            texture.mipLevelCount,
                            texture.normalMipLevelCount,
                            texture.metallicRoughnessMipLevelCount,
                            texture.occlusionMipLevelCount,
                            texture.emissiveMipLevelCount,
                            texture.environmentMipLevelCount};
                    } else if (isFlower) {
                        flowerMipCounts = {
                            texture.mipLevelCount,
                            texture.occlusionMipLevelCount};
                    } else if (isRock) {
                        rockMipCounts = {
                            texture.mipLevelCount,
                            texture.normalMipLevelCount,
                            texture.metallicRoughnessMipLevelCount,
                            texture.occlusionMipLevelCount,
                            texture.emissiveMipLevelCount,
                            texture.environmentMipLevelCount};
                    } else if (isSign) {
                        signMipCounts = {
                            texture.mipLevelCount,
                            texture.occlusionMipLevelCount};
                    } else if (isTree02) {
                        tree02MipCounts = {
                            texture.mipLevelCount,
                            texture.normalMipLevelCount,
                            texture.occlusionMipLevelCount,
                            texture.emissiveMipLevelCount,
                            texture.environmentMipLevelCount,
                            texture.lightProjectionMipLevelCount};
                    } else if (isTree04) {
                        tree04MipCounts = {
                            texture.mipLevelCount,
                            texture.normalMipLevelCount,
                            texture.metallicRoughnessMipLevelCount,
                            texture.occlusionMipLevelCount,
                            texture.emissiveMipLevelCount,
                            texture.environmentMipLevelCount};
                    } else if (isTree05) {
                        tree05MipCounts = {
                            texture.mipLevelCount,
                            texture.normalMipLevelCount,
                            texture.metallicRoughnessMipLevelCount,
                            texture.occlusionMipLevelCount,
                            texture.emissiveMipLevelCount,
                            texture.environmentMipLevelCount};
                    } else if (isTreeMiki) {
                        treeMikiMipCounts = {
                            texture.mipLevelCount,
                            texture.normalMipLevelCount,
                            texture.occlusionMipLevelCount,
                            texture.environmentMipLevelCount};
                    } else if (isEncounterGrass) {
                        encounterGrassMipCounts = {
                            texture.mipLevelCount,
                            texture.normalMipLevelCount,
                            texture.occlusionMipLevelCount,
                            texture.lightProjectionMipLevelCount};
                    }
                    renderer.prewarmWorldTextureData(&texture);
                    for (const auto &instance : drawClass.instances) {
                        texture.modelMatrix = instance.modelMatrix;
                        texture.vertexColorMulR = instance.vertexColorMulR;
                        texture.vertexColorMulG = instance.vertexColorMulG;
                        texture.vertexColorMulB = instance.vertexColorMulB;
                        texture.vertexColorMulA = instance.vertexColorMulA;
                        texture.gpuSkinning = instance.gpuSkinning;
                        texture.gpuSkinningMode = instance.gpuSkinningMode;
                        texture.skinMatrixCount = instance.skinMatrixCount;
                        texture.skinMatrices = instance.skinMatrices;
                        renderer.drawWorldIndexedMeshTexturedCached(
                            mesh->geometryCacheKey.c_str(),
                            mesh->vertices,
                            mesh->vertexCount,
                            mesh->indices,
                            mesh->indexCount,
                            &texture,
                            glm::value_ptr(viewProjection),
                            width,
                            height);
                        if (!collectStats) {
                            continue;
                        }
                        if (isGround) {
                            ++groundGroups;
                            groundTriangles += mesh->indexCount / 3u;
                        } else if (isCliff) {
                            ++cliffGroups;
                            cliffTriangles += mesh->indexCount / 3u;
                        } else if (isGrass01) {
                            ++grass01Groups;
                            grass01Triangles += mesh->indexCount / 3u;
                        } else if (isGrass02) {
                            ++grass02Groups;
                            grass02Triangles += mesh->indexCount / 3u;
                        } else if (isGrass04) {
                            ++grass04Groups;
                            grass04Triangles += mesh->indexCount / 3u;
                        } else if (isGrass05) {
                            ++grass05Groups;
                            grass05Triangles += mesh->indexCount / 3u;
                        } else if (isRoadstone) {
                            ++roadstoneGroups;
                            roadstoneTriangles += mesh->indexCount / 3u;
                        } else if (isRockMask) {
                            ++rockMaskGroups;
                            rockMaskTriangles += mesh->indexCount / 3u;
                        } else if (isFlower) {
                            ++flowerGroups;
                            flowerTriangles += mesh->indexCount / 3u;
                        } else if (isRock) {
                            ++rockGroups;
                            rockTriangles += mesh->indexCount / 3u;
                        } else if (isSign) {
                            ++signGroups;
                            signTriangles += mesh->indexCount / 3u;
                        } else if (isTree02) {
                            ++tree02Groups;
                            tree02Triangles += mesh->indexCount / 3u;
                        } else if (isTree04) {
                            ++tree04Groups;
                            tree04Triangles += mesh->indexCount / 3u;
                        } else if (isTree05) {
                            ++tree05Groups;
                            tree05Triangles += mesh->indexCount / 3u;
                        } else if (isTreeMiki) {
                            ++treeMikiGroups;
                            treeMikiTriangles += mesh->indexCount / 3u;
                        } else if (isEncounterGrass) {
                            ++encounterGrassGroups;
                            encounterGrassTriangles += mesh->indexCount / 3u;
                        }
                    }
                }
            }
            renderer.endWorldIndexedBatchSubmission();
            renderer.endFrame();
            window.swapBuffers();
        };
        renderFrame();
        collectStats = false;

        if (interactive) {
            window.setTitle(
                "LGPE Route 1 review | LMB pan | RMB/MMB orbit | wheel zoom");
            std::cout
                << "[PAC_LgpeQualification][Interactive] Controls:"
                << " LMB drag pan, RMB/MMB drag orbit,"
                << " Shift+MMB drag pan, mouse wheel zoom,"
                << " WASD pan, Q/E move vertically, arrows orbit,"
                << " 1/2/3/4 viewpoints,"
                << " R reset, Esc close\n";

            bool running = true;
            int renderedInteractiveFrames = 0;
            const Uint64 clockFrequency = SDL_GetPerformanceFrequency();
            Uint64 previousTick = SDL_GetPerformanceCounter();
            const Uint64 animationStartTick = previousTick;
            while (running) {
                SDL_Event event{};
                float wheelDelta = 0.0f;
                glm::vec2 panPixels(0.0f);
                glm::vec2 orbitRadians(0.0f);
                while (SDL_PollEvent(&event)) {
                    if (event.type == SDL_QUIT) {
                        running = false;
                    } else if (
                        event.type == SDL_KEYDOWN &&
                        event.key.keysym.sym == SDLK_ESCAPE) {
                        running = false;
                    } else if (
                        event.type == SDL_WINDOWEVENT &&
                        (event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED ||
                         event.window.event == SDL_WINDOWEVENT_RESIZED)) {
                        window.getDrawableSize(width, height);
                        width = std::max(width, 1);
                        height = std::max(height, 1);
                        glViewport(0, 0, width, height);
                        renderer.onResize(width, height);
                    } else if (event.type == SDL_MOUSEWHEEL) {
                        wheelDelta += static_cast<float>(event.wheel.y);
                    } else if (event.type == SDL_MOUSEMOTION) {
                        const bool shiftHeld =
                            (SDL_GetModState() & KMOD_SHIFT) != 0;
                        const bool leftHeld =
                            (event.motion.state & SDL_BUTTON_LMASK) != 0u;
                        const bool middleHeld =
                            (event.motion.state & SDL_BUTTON_MMASK) != 0u;
                        const bool rightHeld =
                            (event.motion.state & SDL_BUTTON_RMASK) != 0u;
                        if (leftHeld || (middleHeld && shiftHeld)) {
                            panPixels += glm::vec2(
                                static_cast<float>(event.motion.xrel),
                                static_cast<float>(event.motion.yrel));
                        } else if (rightHeld || middleHeld) {
                            orbitRadians += glm::vec2(
                                                -static_cast<float>(event.motion.xrel),
                                                static_cast<float>(event.motion.yrel)) *
                                            0.006f;
                        }
                    } else if (
                        event.type == SDL_KEYDOWN &&
                        event.key.repeat == 0) {
                        switch (event.key.keysym.sym) {
                        case SDLK_1:
                            activeCamera = cameraPreset("south");
                            break;
                        case SDLK_2:
                            activeCamera = cameraPreset("middle");
                            break;
                        case SDLK_3:
                            activeCamera = cameraPreset("north");
                            break;
                        case SDLK_4:
                            activeCamera = cameraPreset("canopy");
                            break;
                        case SDLK_r:
                            activeCamera = camera;
                            break;
                        default:
                            break;
                        }
                    }
                }
                if (!running) {
                    break;
                }

                const Uint64 now = SDL_GetPerformanceCounter();
                const float deltaSeconds = std::min(
                    0.05f,
                    static_cast<float>(now - previousTick) /
                        static_cast<float>(clockFrequency));
                previousTick = now;
                const Uint8 *keys = SDL_GetKeyboardState(nullptr);

                glm::vec3 horizontalForward =
                    activeCamera.target - activeCamera.eye;
                horizontalForward.y = 0.0f;
                if (glm::length(horizontalForward) < 0.001f) {
                    horizontalForward = glm::vec3(0.0f, 0.0f, -1.0f);
                } else {
                    horizontalForward = glm::normalize(horizontalForward);
                }
                const glm::vec3 right = glm::normalize(glm::cross(
                    horizontalForward, glm::vec3(0.0f, 1.0f, 0.0f)));
                glm::vec3 translation(0.0f);
                if (keys[SDL_SCANCODE_W]) translation += horizontalForward;
                if (keys[SDL_SCANCODE_S]) translation -= horizontalForward;
                if (keys[SDL_SCANCODE_D]) translation += right;
                if (keys[SDL_SCANCODE_A]) translation -= right;
                if (keys[SDL_SCANCODE_E]) translation.y += 1.0f;
                if (keys[SDL_SCANCODE_Q]) translation.y -= 1.0f;
                const float cameraDistance = std::max(
                    glm::length(activeCamera.target - activeCamera.eye),
                    0.001f);
                if (glm::length(panPixels) > 0.001f) {
                    const glm::vec3 viewForward = glm::normalize(
                        activeCamera.target - activeCamera.eye);
                    const glm::vec3 viewRight = glm::normalize(glm::cross(
                        viewForward, glm::vec3(0.0f, 1.0f, 0.0f)));
                    const glm::vec3 viewUp =
                        glm::normalize(glm::cross(viewRight, viewForward));
                    const float worldUnitsPerPixel =
                        2.0f * cameraDistance *
                        std::tan(glm::radians(35.0f) * 0.5f) /
                        static_cast<float>(std::max(height, 1));
                    const glm::vec3 mouseTranslation =
                        (-panPixels.x * viewRight +
                         panPixels.y * viewUp) *
                        worldUnitsPerPixel;
                    activeCamera.eye += mouseTranslation;
                    activeCamera.target += mouseTranslation;
                }
                if (glm::length(translation) > 0.001f) {
                    const float speed =
                        keys[SDL_SCANCODE_LSHIFT] ||
                                keys[SDL_SCANCODE_RSHIFT]
                            ? 1500.0f
                            : 500.0f;
                    translation =
                        glm::normalize(translation) * speed * deltaSeconds;
                    activeCamera.eye += translation;
                    activeCamera.target += translation;
                }

                glm::vec3 offset =
                    activeCamera.eye - activeCamera.target;
                const float orbitStep =
                    glm::radians(55.0f) * deltaSeconds;
                float yawDelta = orbitRadians.x;
                if (keys[SDL_SCANCODE_LEFT]) yawDelta -= orbitStep;
                if (keys[SDL_SCANCODE_RIGHT]) yawDelta += orbitStep;
                if (yawDelta != 0.0f) {
                    offset = glm::vec3(
                        glm::rotate(
                            glm::mat4(1.0f),
                            yawDelta,
                            glm::vec3(0.0f, 1.0f, 0.0f)) *
                        glm::vec4(offset, 0.0f));
                }
                const float pitchDelta =
                    orbitRadians.y +
                    (keys[SDL_SCANCODE_UP] ? orbitStep : 0.0f) -
                    (keys[SDL_SCANCODE_DOWN] ? orbitStep : 0.0f);
                if (pitchDelta != 0.0f) {
                    const float distance =
                        std::max(glm::length(offset), 0.001f);
                    const float horizontalDistance =
                        glm::length(glm::vec2(offset.x, offset.z));
                    float pitch = std::atan2(
                        offset.y, horizontalDistance);
                    pitch += pitchDelta;
                    pitch = glm::clamp(
                        pitch, glm::radians(5.0f), glm::radians(85.0f));
                    glm::vec2 horizontal(offset.x, offset.z);
                    if (glm::length(horizontal) < 0.001f) {
                        horizontal = glm::vec2(0.0f, 1.0f);
                    } else {
                        horizontal = glm::normalize(horizontal);
                    }
                    const float horizontalLength =
                        std::cos(pitch) * distance;
                    offset = glm::vec3(
                        horizontal.x * horizontalLength,
                        std::sin(pitch) * distance,
                        horizontal.y * horizontalLength);
                }
                if (wheelDelta != 0.0f) {
                    const float currentDistance =
                        std::max(glm::length(offset), 0.001f);
                    const float nextDistance = glm::clamp(
                        currentDistance * std::pow(0.88f, wheelDelta),
                        200.0f,
                        6000.0f);
                    offset *= nextDistance / currentDistance;
                }
                activeCamera.eye = activeCamera.target + offset;

                const float animationSeconds =
                    static_cast<float>(now - animationStartTick) /
                    static_cast<float>(clockFrequency);
                const float windPhaseCycles =
                    encounterWindPhaseCycles + animationSeconds / 4.0f;
                for (auto &layer : encounterGrassLayers) {
                    placeEncounterGrassLayer(
                        layer, layer.placements, windPhaseCycles);
                }
                for (auto &layer : placedVegetationLayers) {
                    placeVegetationLayer(layer, windPhaseCycles);
                }
                updateCameraState();
                renderFrame();
                ++renderedInteractiveFrames;
                if (interactiveFrameLimit > 0 &&
                    renderedInteractiveFrames >= interactiveFrameLimit) {
                    running = false;
                }
            }
        }

        std::size_t textureSubresourceCount = 0u;
        for (const auto &texture : source.textures) {
            textureSubresourceCount += texture.subresources.size();
        }
        std::size_t encounterGrassInstanceCount = 0u;
        std::string encounterGrassLayerStats;
        for (const auto &layer : encounterGrassLayers) {
            encounterGrassInstanceCount += layer.instanceCount;
            if (!encounterGrassLayerStats.empty()) {
                encounterGrassLayerStats += ',';
            }
            encounterGrassLayerStats +=
                layer.logicalName + ':' +
                std::to_string(layer.scene.frame.drawClasses.empty()
                                   ? 0u
                                   : layer.scene.frame.drawClasses.front().instances.size()) +
                ":objects=" +
                std::to_string(layer.scene.registry.renderObjects.size()) +
                ":materials=" +
                std::to_string(layer.scene.registry.materials.size()) +
                ":shader=" +
                (layer.scene.registry.materials.empty()
                     ? std::string("<none>")
                     : layer.scene.registry.materials.front().sourceShaderGroup);
            for (const auto &texture : layer.source.textures) {
                textureSubresourceCount += texture.subresources.size();
            }
        }
        std::size_t placedVegetationInstanceCount = 0u;
        std::string placedVegetationLayerStats;
        for (const auto &layer : placedVegetationLayers) {
            placedVegetationInstanceCount += layer.instanceCount;
            if (!placedVegetationLayerStats.empty()) {
                placedVegetationLayerStats += ',';
            }
            placedVegetationLayerStats +=
                layer.logicalName + ':' +
                std::to_string(layer.instanceCount) +
                ":objects=" +
                std::to_string(layer.scene.registry.renderObjects.size()) +
                ":materials=" +
                std::to_string(layer.scene.registry.materials.size());
            for (const auto &texture : layer.source.textures) {
                textureSubresourceCount += texture.subresources.size();
            }
        }
        std::cout
            << "[PAC_LgpeQualification] PASS"
            << " profile=" << source.profileId
            << " preset=" << camera.name
            << " material_filter="
            << (materialFilter.empty() ? "all" : materialFilter)
            << " encounter_wind_phase_cycles="
            << encounterWindPhaseCycles
            << " material_modes="
            << static_cast<unsigned>(
                   engine::render::lgpe_field_ground::kMaterialMode)
            << ','
            << static_cast<unsigned>(
                   engine::render::lgpe_field_cliff::kMaterialMode)
            << ','
            << static_cast<unsigned>(
                   engine::render::lgpe_field_tree05::kMaterialMode)
            << ','
            << static_cast<unsigned>(
                   engine::render::lgpe_field_object_tree_miki::kMaterialMode)
            << ','
            << static_cast<unsigned>(
                   engine::render::lgpe_field_tree02::kMaterialMode)
            << ','
            << static_cast<unsigned>(
                   engine::render::lgpe_field_tree02::
                       kGrass02MaterialMode)
            << ','
            << static_cast<unsigned>(
                   engine::render::lgpe_field_grass::kShader02MaterialMode)
            << ','
            << static_cast<unsigned>(
                   engine::render::lgpe_field_grass::kShader01MaterialMode)
            << ','
            << static_cast<unsigned>(
                   engine::render::lgpe_field_small_grass::
                       kShader04MaterialMode)
            << ','
            << static_cast<unsigned>(
                   engine::render::lgpe_field_small_grass::
                       kShader05MaterialMode)
            << ','
            << static_cast<unsigned>(
                   engine::render::lgpe_field_overlay::
                       kRoadstoneMaterialMode)
            << ','
            << static_cast<unsigned>(
                   engine::render::lgpe_field_overlay::
                       kRockMaskMaterialMode)
            << ','
            << static_cast<unsigned>(
                   engine::render::lgpe_field_flower::kMaterialMode)
            << ','
            << static_cast<unsigned>(
                   engine::render::lgpe_field_flower::
                       kBuildmodelMaterialMode)
            << ','
            << static_cast<unsigned>(
                   engine::render::lgpe_field_rock::kMaterialMode)
            << ','
            << static_cast<unsigned>(
                   engine::render::lgpe_field_sign::kMaterialMode)
            << ','
            << static_cast<unsigned>(
                   engine::render::lgpe_field_encounter_grass::
                       kMaterialMode)
            << " ground_groups=" << groundGroups
            << " ground_triangles=" << groundTriangles
            << " cliff_groups=" << cliffGroups
            << " cliff_triangles=" << cliffTriangles
            << " grass01_groups=" << grass01Groups
            << " grass01_triangles=" << grass01Triangles
            << " grass02_groups=" << grass02Groups
            << " grass02_triangles=" << grass02Triangles
            << " grass04_groups=" << grass04Groups
            << " grass04_triangles=" << grass04Triangles
            << " grass05_groups=" << grass05Groups
            << " grass05_triangles=" << grass05Triangles
            << " roadstone_groups=" << roadstoneGroups
            << " roadstone_triangles=" << roadstoneTriangles
            << " rockmask_groups=" << rockMaskGroups
            << " rockmask_triangles=" << rockMaskTriangles
            << " flower_groups=" << flowerGroups
            << " flower_triangles=" << flowerTriangles
            << " rock_groups=" << rockGroups
            << " rock_triangles=" << rockTriangles
            << " sign_groups=" << signGroups
            << " sign_triangles=" << signTriangles
            << " tree02_groups=" << tree02Groups
            << " tree02_triangles=" << tree02Triangles
            << " tree04_groups=" << tree04Groups
            << " tree04_triangles=" << tree04Triangles
            << " tree05_groups=" << tree05Groups
            << " tree05_triangles=" << tree05Triangles
            << " tree_miki_groups=" << treeMikiGroups
            << " tree_miki_triangles=" << treeMikiTriangles
            << " encounter_grass_groups=" << encounterGrassGroups
            << " encounter_grass_triangles=" << encounterGrassTriangles
            << " encounter_grass_instances=" << encounterGrassInstanceCount
            << " encounter_grass_layers=" << encounterGrassLayerStats
            << " placed_vegetation_instances="
            << placedVegetationInstanceCount
            << " placed_vegetation_layers="
            << placedVegetationLayerStats
            << " authored_texture_subresources="
            << textureSubresourceCount
            << " ground_role_mips="
            << groundMipCounts[0] << ','
            << groundMipCounts[1] << ','
            << groundMipCounts[2] << ','
            << groundMipCounts[3] << ','
            << groundMipCounts[4] << ','
            << groundMipCounts[5]
            << " cliff_role_mips="
            << cliffMipCounts[0] << ','
            << cliffMipCounts[1] << ','
            << cliffMipCounts[2] << ','
            << cliffMipCounts[3] << ','
            << cliffMipCounts[4]
            << " grass01_role_mips="
            << grass01MipCounts[0] << ','
            << grass01MipCounts[1] << ','
            << grass01MipCounts[2] << ','
            << grass01MipCounts[3] << ','
            << grass01MipCounts[4] << ','
            << grass01MipCounts[5]
            << " grass02_role_mips="
            << grass02MipCounts[0] << ','
            << grass02MipCounts[1] << ','
            << grass02MipCounts[2] << ','
            << grass02MipCounts[3] << ','
            << grass02MipCounts[4] << ','
            << grass02MipCounts[5]
            << " grass04_role_mips="
            << grass04MipCounts[0] << ','
            << grass04MipCounts[1] << ','
            << grass04MipCounts[2] << ','
            << grass04MipCounts[3] << ','
            << grass04MipCounts[4]
            << " grass05_role_mips="
            << grass05MipCounts[0] << ','
            << grass05MipCounts[1] << ','
            << grass05MipCounts[2] << ','
            << grass05MipCounts[3] << ','
            << grass05MipCounts[4] << ','
            << grass05MipCounts[5]
            << " roadstone_role_mips="
            << roadstoneMipCounts[0] << ','
            << roadstoneMipCounts[1]
            << " rockmask_role_mips="
            << rockMaskMipCounts[0] << ','
            << rockMaskMipCounts[1] << ','
            << rockMaskMipCounts[2] << ','
            << rockMaskMipCounts[3] << ','
            << rockMaskMipCounts[4] << ','
            << rockMaskMipCounts[5]
            << " flower_role_mips="
            << flowerMipCounts[0] << ','
            << flowerMipCounts[1]
            << " rock_role_mips="
            << rockMipCounts[0] << ','
            << rockMipCounts[1] << ','
            << rockMipCounts[2] << ','
            << rockMipCounts[3] << ','
            << rockMipCounts[4] << ','
            << rockMipCounts[5]
            << " sign_role_mips="
            << signMipCounts[0] << ','
            << signMipCounts[1]
            << " tree02_role_mips="
            << tree02MipCounts[0] << ','
            << tree02MipCounts[1] << ','
            << tree02MipCounts[2] << ','
            << tree02MipCounts[3] << ','
            << tree02MipCounts[4] << ','
            << tree02MipCounts[5]
            << " tree04_role_mips="
            << tree04MipCounts[0] << ','
            << tree04MipCounts[1] << ','
            << tree04MipCounts[2] << ','
            << tree04MipCounts[3] << ','
            << tree04MipCounts[4] << ','
            << tree04MipCounts[5]
            << " tree05_role_mips="
            << tree05MipCounts[0] << ','
            << tree05MipCounts[1] << ','
            << tree05MipCounts[2] << ','
            << tree05MipCounts[3] << ','
            << tree05MipCounts[4] << ','
            << tree05MipCounts[5]
            << " tree_miki_role_mips="
            << treeMikiMipCounts[0] << ','
            << treeMikiMipCounts[1] << ','
            << treeMikiMipCounts[2] << ','
            << treeMikiMipCounts[3]
            << " encounter_grass_role_mips="
            << encounterGrassMipCounts[0] << ','
            << encounterGrassMipCounts[1] << ','
            << encounterGrassMipCounts[2] << ','
            << encounterGrassMipCounts[3]
            << '\n';
        renderer.shutdown();
        return !materialFilter.empty() ||
                       (groundGroups > 0u && cliffGroups > 0u &&
                        grass01Groups > 0u && grass02Groups > 0u &&
                        grass04Groups > 0u && grass05Groups > 0u &&
                        roadstoneGroups > 0u && rockMaskGroups > 0u &&
                        flowerGroups > 0u && rockGroups > 0u &&
                        signGroups > 0u &&
                        tree02Groups > 0u && tree04Groups > 0u &&
                        tree05Groups > 0u && treeMikiGroups > 0u &&
                        encounterGrassGroups == encounterGrassInstanceCount &&
                        encounterGrassInstanceCount == 164u &&
                        placedVegetationInstanceCount == 54u)
                   ? 0
                   : 2;
    } catch (const std::exception &ex) {
        std::cerr << "[PAC_LgpeQualification] FAIL exception="
                  << ex.what() << '\n';
        return 1;
    }
}
