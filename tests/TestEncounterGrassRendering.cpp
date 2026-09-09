#include "engine/core/Paths.h"
#include "game/GameConfig.h"
#include "game/GameWorld.h"
#include "game/assets/DevAssetStore.h"
#include "game/render/environment/EncounterGrassMotion.h"
#include "game/render/environment/EncounterGrassLayout.h"
#include "game/arena/EncounterGrassFootprint.h"
#include "game/render/environment/Route1FieldEncounterGrassMaterial.h"
#include "game/runtime/shared/scene/ArenaSceneActivation.h"
#include "game/runtime/shared/scene/Route1RuntimeEnvironment.h"
#include "game/runtime/shared/scene/Route1SceneVariants.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <string>
#include <vector>
#include <glm/gtc/type_ptr.hpp>

namespace {
namespace env = game::runtime::route1_environment;
using Batch = game::runtime::shared_world_batches::WorldIndexedBatch;
struct BladeVertex {
    glm::vec3 rest;
    glm::vec3 posed;
    bool root;
};

// Use the exact indexed geometry and cached skin pointers submitted to the
// renderer. Palette changes alone do not prove that blade tips visibly move.
std::vector<BladeVertex> sampleBlades(const std::vector<Batch> &batches) {
    std::vector<BladeVertex> result;
    for (const auto &batch : batches) {
        const auto &material = batch.sharedTemplate ? *batch.sharedTemplate : batch;
        if (material.materialMode != engine::render::route1_field_encounter_grass::kMaterialMode) continue;
        const auto *vertices = batch.sharedVertices ? batch.sharedVertices : batch.vertices.data();
        const auto *indices = batch.sharedIndices ? batch.sharedIndices : batch.indices.data();
        const auto count = batch.sharedIndices ? batch.sharedIndexCount : batch.indices.size();
        float bottom = 1e9f, top = -1e9f;
        for (std::size_t i = 0; i < count; ++i) {
            bottom = std::min(bottom, vertices[indices[i]].y);
            top = std::max(top, vertices[indices[i]].y);
        }
        for (const auto &instance : batch.instances) {
            if (!instance.gpuSkinning || !instance.skinMatrices) continue;
            const auto model = glm::make_mat4(instance.modelMatrix.data());
            for (std::size_t i = 0; i < count; ++i) {
                const auto &v = vertices[indices[i]];
                const glm::vec4 rest(v.x, v.y, v.z, 1);
                const float joints[]{v.joint0, v.joint1, v.joint2, v.joint3};
                const float weights[]{v.weight0, v.weight1, v.weight2, v.weight3};
                glm::vec4 posed(0);
                for (int j = 0; j < 4; ++j) {
                    if (weights[j] <= 0) continue;
                    const auto joint = static_cast<unsigned>(std::lround(joints[j]));
                    if (joint >= instance.skinMatrixCount) continue;
                    posed += weights[j] * (glm::make_mat4(instance.skinMatrices + joint * 16u) * rest);
                }
                result.push_back({glm::vec3(model * rest), glm::vec3(model * posed), v.y < bottom + (top - bottom) * 0.05f});
            }
        }
    }
    return result;
}
float displacement(const std::vector<BladeVertex> &a, const std::vector<BladeVertex> &b,
                   glm::vec3 actor, bool roots = false, bool distant = false) {
    if (a.empty() || a.size() != b.size()) return -1;
    float result = 0;
    for (std::size_t i = 0; i < a.size(); ++i) {
        const float distance = glm::length(glm::vec2(a[i].rest.x - actor.x, a[i].rest.z - actor.z));
        if (distant ? distance < 2.0f : distance > 1.5f) continue;
        if (roots && !a[i].root) continue;
        result = std::max(result, glm::length(a[i].posed - b[i].posed));
    }
    return result;
}
} // namespace

bool test_encounter_grass_motion(std::string &outFail) {
    const auto source = game::arena::encounterGrassCenters({{-1, -1}, {-1, 0}, {-1, 1}, {0, -1}, {0, 0}, {0, 1}});
    const auto packed = game::render::encounter_grass_layout::centers(source, 1, .5f);
    std::vector<std::array<float, 2>> expected;
    for (float x : {-100.f, -50.f, 50.f, 100.f})
        for (float z : {-25.f, 25.f, 75.f})
            expected.push_back({x, z});
    if (packed != expected || game::render::encounter_grass_layout::centers(source, 1, 1) != source) {
        outFail = "Resized grass beds must place full-size clumps without changing their footprint.";
        return false;
    }
    namespace motion = game::render::encounter_grass_motion;
    motion::State reference;
    for (const int fps : {30, 60, 144}) {
        motion::State state;
        for (int i = 0; i < fps; ++i)
            motion::advance(state, {0.6f, 0.2f}, 1.0f / fps);
        if (fps == 30) reference = state;
        if (glm::length(state.bend - reference.bend) > 0.0001f) {
            outFail = "Grass pressure depends on render frame rate.";
            return false;
        }
        bool rebounded = false;
        for (int i = 0; i < fps; ++i) {
            motion::advance(state, {}, 1.0f / fps);
            rebounded |= state.bend.x < -0.015f;
        }
        if (!rebounded || glm::length(state.bend) > 0.001f) {
            outFail = "Released blades must briefly rebound, then settle.";
            return false;
        }
    }
    const motion::Contact idle{{0, 0, 0}, {}, 0.65f, 0};
    const auto early = motion::target({30, 0, 0}, std::span(&idle, 1), 1, 0);
    const auto late = motion::target({30, 0, 0}, std::span(&idle, 1), 2, 0);
    if (glm::length(early) < 0.1f || glm::length(early - late) > 0.0001f ||
        glm::length(motion::target({30, 50, 0}, std::span(&idle, 1), 1, 0)) > 0.0001f) {
        outFail = "Standing pressure must stop fluttering and cannot reach up a ledge.";
        return false;
    }
    return true;
}

bool test_encounter_grass_rendering(std::string &outFail) {
    namespace variants = game::runtime::route1_scene_variants;
    game::assets::DevAssetStore store(engine::paths::dataRoot());
    env::RuntimeEnvironment environment;
    if (!env::loadCookedEnvironment(store, environment, nullptr, &outFail)) return false;
    for (const auto *variant : {&variants::kRoute1Pilot, &variants::kRoute1SouthClearing, &variants::kRoute1NorthTerraces}) {
        GameConfigData config;
        GameWorld world(config);
        if (!game::runtime::arena_scene_activation::apply(store, *variant, environment, false, true, &outFail) ||
            !game::runtime::arena_scene_activation::applyGameplay(store, *variant, world, &outFail)) return false;
        const auto actor = variant == &variants::kRoute1Pilot ? world.gridToWorld(7, 4) : variant == &variants::kRoute1NorthTerraces ? world.gridToWorld(7, 3) : world.gridToWorld(6, 3);
        std::vector<Batch> cached;
        environment.setEncounterGrassInteractors({});
        environment.updateAnimation(0);
        environment.appendIndexedBatches(0, cached);
        if (variant == &variants::kRoute1Pilot) {
            std::size_t clumps = 0;
            for (const auto &batch : cached) {
                const auto &material = batch.sharedTemplate ? *batch.sharedTemplate : batch;
                if (material.materialMode != engine::render::route1_field_encounter_grass::kMaterialMode) continue;
                const auto *vertices = batch.sharedVertices ? batch.sharedVertices : batch.vertices.data();
                const auto *indices = batch.sharedIndices ? batch.sharedIndices : batch.indices.data();
                const auto count = batch.sharedIndices ? batch.sharedIndexCount : batch.indices.size();
                // The source skeleton has an extra unused bone. Check the
                // actual blade influences: Grass01 uses joints 1..4, while
                // Grass02 also has a fifth cluster.
                for (std::size_t i = 0; i < count; ++i) {
                    const auto &v = vertices[indices[i]];
                    const float joints[]{v.joint0, v.joint1, v.joint2, v.joint3};
                    const float weights[]{v.weight0, v.weight1, v.weight2, v.weight3};
                    for (int j = 0; j < 4; ++j) {
                        if (weights[j] > 0 && joints[j] > 4) {
                            outFail = "Entrance still submits the coarse Grass02 blade geometry.";
                            return false;
                        }
                    }
                }
                for (const auto &instance : batch.instances) {
                    const auto matrix = glm::make_mat4(instance.modelMatrix.data());
                    if (instance.skinMatrixCount < 5 ||
                        std::abs(glm::length(glm::vec3(matrix[0])) - .01f) > 1e-6f ||
                        std::abs(glm::length(glm::vec3(matrix[1])) - .01f) > 1e-6f ||
                        std::abs(glm::length(glm::vec3(matrix[2])) - .01f) > 1e-6f) {
                        outFail = "Entrance must submit Grass01 with full-size blades, including its shortened threshold: joints=" +
                                  std::to_string(instance.skinMatrixCount) + " scale=" + std::to_string(glm::length(glm::vec3(matrix[0]))) + "," +
                                  std::to_string(glm::length(glm::vec3(matrix[1]))) + "," + std::to_string(glm::length(glm::vec3(matrix[2])));
                        return false;
                    }
                    ++clumps;
                }
            }
            if (clumps != 91) {
                outFail = "Entrance lost its 27 + 27 + 25 + 12 authored grass modules.";
                return false;
            }
        }
        environment.updateAnimation(1);
        const auto ambient = sampleBlades(cached);
        environment.updateAnimation(3);
        const auto ambientRecovered = sampleBlades(cached);
        env::EncounterGrassInteractor contact{{actor.x, actor.y, actor.z}, {1, 0, 0}, 0, 0.65f};
        environment.setEncounterGrassInteractors(std::span(&contact, 1));
        for (int i = 0; i <= 60; ++i)
            environment.updateAnimation(i / 60.0f);
        const auto standing = sampleBlades(cached);
        contact.motionStrength = contact.contactStrength = 1;
        environment.setEncounterGrassInteractors(std::span(&contact, 1));
        for (int i = 0; i <= 60; ++i)
            environment.updateAnimation(i / 60.0f);
        const auto moving = sampleBlades(cached);
        environment.setEncounterGrassInteractors({});
        for (int i = 61; i <= 180; ++i)
            environment.updateAnimation(i / 60.0f);
        const auto recovered = sampleBlades(cached);
        const float opening = displacement(ambient, moving, actor);
        const float pressure = displacement(ambient, standing, actor);
        const float roots = displacement(ambient, moving, actor, true);
        const float remote = displacement(ambient, moving, actor, false, true);
        const float recovery = displacement(ambientRecovered, recovered, actor);
        std::cout << variant->sceneId << " grass displacement metres: moving=" << opening
                  << " standing=" << pressure << " roots=" << roots << " remote=" << remote << " recovery=" << recovery << '\n';
        if (opening < 0.12f || pressure < 0.06f || pressure >= opening || roots > 0.02f || remote > 0.0001f || recovery > 0.001f) {
            outFail = "Grass must visibly part around contact, keep its roots planted and distant blades untouched, and recover on both authored maps.";
            return false;
        }
        // Cached palettes freeze on pause; a rewind discards contact and
        // spring velocity. Neither operation needs to rebuild grass geometry.
        environment.setEncounterGrassInteractors(std::span(&contact, 1));
        for (int i = 0; i <= 60; ++i)
            environment.updateAnimation(i / 60.0f);
        const auto beforePause = sampleBlades(cached);
        environment.updateAnimation(1);
        if (displacement(beforePause, sampleBlades(cached), actor) > 0.000001f) {
            outFail = "Grass continues reacting while the simulation is paused.";
            return false;
        }
        environment.setEncounterGrassInteractors({});
        environment.updateAnimation(0);
        environment.updateAnimation(1);
        if (displacement(ambient, sampleBlades(cached), actor) > 0.0001f) {
            outFail = "Grass retained contact deformation after restarting the simulation.";
            return false;
        }
        contact.worldPosition[1] -= 0.5f;
        environment.setEncounterGrassInteractors(std::span(&contact, 1));
        for (int i = 0; i <= 60; ++i)
            environment.updateAnimation(i / 60.0f);
        if (displacement(ambient, sampleBlades(cached), actor) > 0.0001f) {
            outFail = "A Pokemon below the grass platform disturbed its blades.";
            return false;
        }
    }
    return true;
}
