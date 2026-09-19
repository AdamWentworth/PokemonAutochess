#include "engine/core/Paths.h"
#include "game/assets/DevAssetStore.h"
#include "game/preview/ArenaEffectExperiment.h"
#include "game/runtime/shared/scene/Route1SceneVariants.h"
#include "game/runtime/shared/scene/AuthoredArenaBundle.h"
#include "game/runtime/shared/scene/ArenaSceneActivation.h"
#include "game/vfx/SampledEffectClip.h"
#include "game/GameConfig.h"
#include "game/GameWorld.h"
#include <nlohmann/json.hpp>
#include <cstring>
#include <stdexcept>

namespace {
void check(bool ok, const std::string &message) {
    if (!ok) throw std::runtime_error(message);
}
struct ClipStore : engine::IAssetStore {
    std::vector<std::uint8_t> bytes;
    bool readBytes(const std::string &, std::vector<std::uint8_t> &out, std::string *) const override {
        out = bytes;
        return true;
    }
    bool readText(const std::string &, std::string &, std::string *) const override { return false; }
    bool exists(const std::string &) const override { return true; }
};
} // namespace

bool test_sampled_effect_clip_contract(std::string &error) {
    try {
        using J = nlohmann::json;
        const std::vector<float> vertices = {0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 0, 0, 1, 0, 1, 1, 1, 1, 0, 0, 1, 0, 1, 1, 1, 1, 1};
        std::vector<std::uint8_t> payload(vertices.size() * sizeof(float));
        std::memcpy(payload.data(), vertices.data(), payload.size());
        J data = {{"kind", "sampled_effect_clip"}, {"schema", 1}, {"fps", 30}, {"frame_count", 2}, {"floor_y", 0}, {"textures", J::array()}, {"cards", J::array()}, {"meshes", J::array({{{"texture", -1}, {"depth_write", true}, {"indices", {0, 1, 2}}, {"frames", J::array({J::binary({}), J::binary(payload)})}}})}};
        ClipStore store;
        store.bytes = J::to_cbor(data);
        game::vfx::SampledEffectClip clip;
        check(clip.load(store, "fixture", error), error);
        std::vector<game::vfx::SampledEffectClip::Batch> batches;
        const auto append = [&](float t) { clip.append(t, {0, 0, 0}, {1, 1, 1}, glm::mat4(1), {1, 0, 0}, {0, 1, 0}, [](const std::string &, bool) -> game::vfx::SampledEffectClip::Texture * { return nullptr; }, batches); };
        append(-.1f);
        append(0);
        append(clip.duration());
        check(batches.empty(), "Absent, delayed and retired geometry must not leak into the scene");
        append(.04f);
        check(batches.size() == 1 && batches[0].hasGeometry() && batches[0].sharedVertexCount == 3, "Active sampled geometry did not reach the shared renderer path");
        const auto reject = [&](const J &bad) { store.bytes=J::to_cbor(bad); game::vfx::SampledEffectClip candidate;
            check(!candidate.load(store,"fixture",error),"Malformed sampled clip was accepted"); };
        auto bad = data;
        bad["meshes"][0]["indices"] = {0, 1, 99};
        reject(bad);
        bad = data;
        bad["frame_count"] = 3;
        reject(bad);
        bad = data;
        bad["meshes"][0]["frames"][1] = J::binary({1, 2, 3});
        reject(bad);
        bad = data;
        bad["textures"] = {"../outside.png"};
        reject(bad);
        check(!game::preview::arena_effect_experiment::enabled("scripts/states/route1_pilot.lua") &&
                  !game::preview::arena_effect_experiment::enabled("scripts/states/route1_flat_experiment.lua") &&
                  game::preview::arena_effect_experiment::enabled("scripts/states/route1_flat_experiment_earthquake.lua"),
              "The visual experiment must be limited to its explicit preview scenarios");
        return true;
    } catch (const std::exception &e) {
        error = e.what();
        return false;
    }
}

bool test_flat_arena_experiment_contract(std::string &error) {
    try {
        namespace variants = game::runtime::route1_scene_variants;
        namespace activation = game::runtime::arena_scene_activation;
        game::assets::DevAssetStore store(engine::paths::dataRoot());
        const auto &variant = variants::kRoute1FlatExperiment;
        check(&variants::fromStateScriptPath("scripts/states/route1_flat_experiment_earthquake.lua") == &variant, "Experiment resolved to the original arena");
        game::runtime::authored_arena::Bundle bundle;
        check(bundle.load(store, std::string(variant.arenaBundlePath), &error), error);
        check(bundle.map.playableCells.size() == 64 && bundle.map.reserveCells.size() == 16, "Experiment changed board or bench dimensions");
        for (const auto &cells : {bundle.map.playableCells, bundle.map.reserveCells})
            for (const auto &[x, z] : cells) {
                const auto *cell = bundle.map.tileAt(x, z);
                check(cell && cell->height == 0 && cell->ramp == 0 && cell->surface == 1, "Experiment floor must be flat dirt");
                for (float dx : {.05f, .5f, .95f})
                    for (float dz : {.05f, .5f, .95f})
                        check(bundle.map.coverAt((x + dx) * 100, (z + dz) * 100).empty(), "Encounter grass overlaps the experiment board/bench");
            }
        GameConfigData config;
        GameWorld world(config);
        PokemonInstance reserveUnit;
        reserveUnit.id = 9001;
        reserveUnit.position = world.travelBenchPosition(3);
        world.getBenchPokemons().push_back(reserveUnit);
        const float originalBenchZ = reserveUnit.position.z;
        check(activation::applyGameplay(store, variant, world, &error), error);
        check(world.getBenchGapCells() == 1 &&
                  std::abs(world.getBenchPokemons()[0].position.z - originalBenchZ - world.getBoardCellSize()) < .001f &&
                  world.travelBenchSlot(world.getBenchPokemons()[0].position) == 3,
              "Scene activation must move reserves to the detached bench while preserving their slots");
        for (int x = 17; x <= 24; ++x) {
            check(bundle.map.reserveCells.contains({x, -11}) && bundle.map.reserveCells.contains({x, 0}),
                  "Detached bench cells disagree with the dirt pads");
            for (int z : {-10, -1}) {
                const auto *gap = bundle.map.tileAt(x, z);
                check(gap && gap->height == 0 && gap->ramp == 0 && gap->surface == 0 &&
                          !bundle.map.playableCells.contains({x, z}) && !bundle.map.reserveCells.contains({x, z}),
                      "Benches need a lawn gap outside the board and reserve cells");
            }
        }
        for (int x = 17; x <= 21; ++x) {
            const auto *shelf = bundle.map.tileAt(x, -12);
            check(shelf && shelf->height == 1 && shelf->ramp == 0 && shelf->surface == 0,
                  "The rear bench needs a full grass shelf halfway up the bank");
        }
        // Measure both ends and the midpoint of shared edges, so a ramp's side
        // cannot hide a full-height rock face behind a correct centre sample.
        for (int z = -17; z <= 1; ++z)
            for (int x = 13; x <= 28; ++x)
                for (auto delta : {game::arena::Cell{1, 0}, game::arena::Cell{0, 1}}) {
                    const auto *cell = bundle.map.tileAt(x, z);
                    const auto *neighbor = bundle.map.tileAt(x + delta.x, z + delta.z);
                    check(cell && neighbor, "The arena surroundings have a missing terrain tile");
                    for (float t : {0.f, .5f, 1.f}) {
                        const float px = (x + (delta.x ? 1.f : t)) * 100;
                        const float pz = (z + (delta.z ? 1.f : t)) * 100;
                        check(std::abs(cell->heightAt(px, pz) - neighbor->heightAt(px, pz)) <= 50.001f,
                              "Arena ledges must not expose more than one half-metre rock face");
                    }
                }
        check(bundle.map.cover.size() >= 3, "The flat arena lost its surrounding encounter grass");
        // Check actual height continuity along the route behind the enemy bench.
        float lastHeight = 0;
        for (int z : {-11, -12, -13, -14}) {
            const auto *ramp = bundle.map.tileAt(23, z);
            check(ramp != nullptr, "The route behind the north bench has a missing tile");
            check(std::abs(ramp->heightAt(2350.f, static_cast<float>((z + 1) * 100)) - lastHeight) < .001f,
                  "The route exit contains an impassable vertical step");
            lastHeight = ramp->heightAt(2350.f, static_cast<float>(z * 100));
            check(bundle.map.coverAt(2350, (z + .5f) * 100).empty(), "Encounter grass blocks the route exit");
        }
        check(std::abs(lastHeight - 100) < .001f, "The grass ramp does not join the upper route");
        for (int row = 0; row < 8; ++row)
            for (int col = 0; col < 8; ++col) {
                using game::arena::Cell;
                using game::arena::StepKind;
                for (auto delta : {Cell{0, 1}, Cell{1, 0}, Cell{-1, 0}, Cell{0, -1}}) {
                    Cell to{col + delta.x, row + delta.z};
                    if (to.x < 0 || to.x >= 8 || to.z < 0 || to.z >= 8) continue;
                    check(world.combatMap().stepKind({col, row}, to, {}) == StepKind::Walk, "Flat combat adjacency has a cliff or blockage");
                }
            }
        game::runtime::authored_arena::Bundle original;
        check(original.load(store, std::string(variants::kRoute1Pilot.arenaBundlePath), &error), error);
        check(!original.map.cover.empty(), "Original entrance lost its encounter grass");
        check(activation::applyGameplay(store, variants::kRoute1Pilot, world, &error), error);
        check(world.getBenchGapCells() == 0 && std::abs(world.getBenchPokemons()[0].position.z - originalBenchZ) < .001f,
              "Returning to the entrance did not restore its original bench placement");
        check(activation::applyGameplay(store, variant, world, &error), error);
        game::vfx::SampledEffectClip clip;
        check(clip.load(store, "content/phlosion/vfx/earthquake-experiment", error), error);
        check(clip.meshCount() == 77 && clip.cardCount() == 312 && clip.duration() > 2.6f, "The experiment lost recovered Earthquake ingredients");
        return true;
    } catch (const std::exception &e) {
        error = e.what();
        return false;
    }
}
