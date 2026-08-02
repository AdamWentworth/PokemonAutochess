#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include "game/config/GameDataDb.h"
#include "game/runtime/session/SessionBackendUnitHydration.h"

namespace {

std::filesystem::path createTempDir(const std::string& label, std::string& outFail) {
    std::error_code ec;
    std::filesystem::path root = std::filesystem::temp_directory_path(ec);
    if (ec) {
        outFail = "temp_directory_path failed: " + ec.message();
        return {};
    }

    const auto stamp = static_cast<long long>(std::chrono::steady_clock::now().time_since_epoch().count());
    root /= "pac_tests_" + label + "_" + std::to_string(stamp);
    std::filesystem::create_directories(root, ec);
    if (ec) {
        outFail = "create_directories failed: " + ec.message();
        return {};
    }
    return root;
}

void removeTempDir(const std::filesystem::path& dir) {
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}

bool writeFile(const std::filesystem::path& path, const std::string& content, std::string& outFail) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file.is_open()) {
        outFail = "Failed to open file for write: " + path.string();
        return false;
    }

    file << content;
    if (!file.good()) {
        outFail = "Failed to write file: " + path.string();
        return false;
    }

    return true;
}

bool nearFloat(float a, float b, float eps = 0.0001f) {
    return std::fabs(a - b) <= eps;
}

engine::render::model_types::AnimationClip makeClip(const std::string& name, float durationSec) {
    engine::render::model_types::AnimationClip clip;
    clip.name = name;
    clip.durationSec = durationSec;
    return clip;
}

} // namespace

bool test_session_backend_unit_hydration_contract(std::string& outFail) {
    const std::filesystem::path tempDir = createTempDir("session_backend_unit_hydration", outFail);
    if (tempDir.empty()) return false;
    const std::filesystem::path originalCwd = std::filesystem::current_path();

    const std::filesystem::path pokemonPath = tempDir / "pokemon.json";
    const std::filesystem::path flyerPath = tempDir / "flyers.json";
    const std::filesystem::path animsetDir = tempDir / "assets" / "models";
    std::error_code ec;
    std::filesystem::create_directories(animsetDir, ec);
    if (ec) {
        outFail = "Failed to create animset directory: " + ec.message();
        removeTempDir(tempDir);
        return false;
    }
    const std::filesystem::path animsetPath = animsetDir / "Testmon.animset.json";
    const std::string pokemonJson = R"({
  "TESTMON": {
    "model": "Testmon.glb",
    "modelScaleMode": "native"
  },
  "BIRDMON": {
    "model": "Birdmon.glb",
    "modelScaleMode": "native"
  }
})";
    const std::string flyerJson = R"({
  "flyers": ["birdmon"],
  "airLocomotionDefaults": {
    "birdmon": {
      "airLiftY": 0.65,
      "takeoffSec": 0.30,
      "landingSec": 0.42,
      "takeoffAnimSpeed": 1.2,
      "landAnimSpeed": 0.9
    }
  }
})";
    const std::string animsetJson = R"({
  "roles": {
    "idle": "battlewait",
    "move": "run_loop",
    "attack1": "attack01",
    "faint": "down01"
  }
})";
    if (!writeFile(pokemonPath, pokemonJson, outFail) ||
        !writeFile(flyerPath, flyerJson, outFail) ||
        !writeFile(animsetPath, animsetJson, outFail)) {
        removeTempDir(tempDir);
        return false;
    }

    GameDataDb db;
    if (!db.pokemon.loadConfig(pokemonPath.string(), nullptr) ||
        !db.flyers.loadConfig(flyerPath.string(), nullptr)) {
        outFail = "Failed to load temporary config for SessionBackendUnitHydration test.";
        removeTempDir(tempDir);
        return false;
    }

    std::unordered_map<std::string, game::runtime::render_model::MeshData> meshes;
    auto& testMesh = meshes["assets/models/Testmon.glb"];
    testMesh.modelScaleFactor = 2.0f;
    testMesh.animations = {
        makeClip("battlewait", 0.50f),
        makeClip("run_loop", 0.60f),
        makeClip("attack01", 0.75f),
        makeClip("down01", 0.90f),
    };
    auto& birdMesh = meshes["assets/models/Birdmon.glb"];
    birdMesh.modelScaleFactor = 1.0f;
    birdMesh.animations = {
        makeClip("battlewait", 0.40f),
        makeClip("walk", 0.50f),
        makeClip("attack01", 0.60f),
        makeClip("down", 0.70f),
    };

    PokemonInstance testmon;
    testmon.name = "testmon";
    testmon.animIdleIndex = -1;
    testmon.animMoveIndex = -1;
    testmon.animAttack1Index = -1;
    testmon.animFaintIndex = -1;
    testmon.animGroundIdleIndex = -1;
    testmon.animAirIdleIndex = -1;
    testmon.animTakeoffIndex = -1;
    testmon.animLandIndex = -1;
    testmon.animLandAIndex = -1;
    testmon.animLandBIndex = -1;
    testmon.animLandCIndex = -1;
    testmon.activeAnimIndex = -1;
    testmon.currentAttackAnimIndex = -1;

    PokemonInstance birdmon = testmon;
    birdmon.name = "birdmon";
    PokemonInstance defaultedBackendUnit;
    defaultedBackendUnit.name = "testmon";

    std::vector<PokemonInstance> boardUnits{testmon};
    std::vector<PokemonInstance> benchUnits{birdmon, defaultedBackendUnit};
    game::runtime::session_backend_unit_hydration::BackendAnimRoleCache cache;
    std::filesystem::current_path(tempDir, ec);
    if (ec) {
        outFail = "Failed to switch test working directory: " + ec.message();
        removeTempDir(tempDir);
        return false;
    }
    game::runtime::session_backend_unit_hydration::hydrateBackendUnits(
        boardUnits,
        benchUnits,
        db,
        cache,
        [&](const std::string& modelPath) -> game::runtime::render_model::MeshData* {
            auto it = meshes.find(modelPath);
            return it == meshes.end() ? nullptr : &it->second;
        });

    const PokemonInstance& hydrated = boardUnits.front();
    const auto attackAliasIt = hydrated.animIndexCache.find("attack01");
    if (hydrated.backendModelPath != "assets/models/Testmon.glb" ||
        hydrated.animIdleIndex != 0 ||
        hydrated.animMoveIndex != 1 ||
        hydrated.animAttack1Index != 2 ||
        hydrated.animFaintIndex != 3 ||
        hydrated.activeAnimIndex != 0 ||
        hydrated.currentAttackAnimIndex != 2 ||
        !nearFloat(hydrated.attackDurationSec, 0.75f) ||
        !nearFloat(hydrated.faintAnimDurationSec, 0.90f) ||
        !nearFloat(hydrated.modelScaleCorrection, 0.5f) ||
        attackAliasIt == hydrated.animIndexCache.end() ||
        attackAliasIt->second != 2 ||
        hydrated.backendAnimDurationsSec.size() != 4u ||
        !nearFloat(hydrated.backendAnimDurationsSec[2], 0.75f)) {
        std::ostringstream oss;
        oss << "hydrated mismatch: path=" << hydrated.backendModelPath
            << " idle=" << hydrated.animIdleIndex
            << " move=" << hydrated.animMoveIndex
            << " attack=" << hydrated.animAttack1Index
            << " faint=" << hydrated.animFaintIndex
            << " active=" << hydrated.activeAnimIndex
            << " currentAttack=" << hydrated.currentAttackAnimIndex
            << " attackSec=" << hydrated.attackDurationSec
            << " faintSec=" << hydrated.faintAnimDurationSec
            << " scale=" << hydrated.modelScaleCorrection
            << " aliasPresent=" << (attackAliasIt != hydrated.animIndexCache.end())
            << " aliasValue=" << ((attackAliasIt != hydrated.animIndexCache.end()) ? attackAliasIt->second : -999)
            << " durationCount=" << hydrated.backendAnimDurationsSec.size()
            << " attackDurIdx2="
            << ((hydrated.backendAnimDurationsSec.size() > 2u) ? hydrated.backendAnimDurationsSec[2] : -999.0f);
        outFail = oss.str();
        std::filesystem::current_path(originalCwd, ec);
        removeTempDir(tempDir);
        return false;
    }

    const PokemonInstance& hydratedBird = benchUnits.front();
    if (!hydratedBird.usesAirLocomotion ||
        !nearFloat(hydratedBird.airLiftY, 0.65f) ||
        !nearFloat(hydratedBird.takeoffSec, 0.30f) ||
        !nearFloat(hydratedBird.landingSec, 0.42f) ||
        !nearFloat(hydratedBird.takeoffAnimSpeed, 1.2f) ||
        !nearFloat(hydratedBird.landAnimSpeed, 0.9f)) {
        outFail = "SessionBackendUnitHydration should apply flyer-based air locomotion defaults when role metadata is absent.";
        std::filesystem::current_path(originalCwd, ec);
        removeTempDir(tempDir);
        return false;
    }

    const PokemonInstance& hydratedDefaultedBackendUnit = benchUnits[1];
    if (hydratedDefaultedBackendUnit.animIdleIndex != 0 ||
        hydratedDefaultedBackendUnit.animMoveIndex != 1 ||
        hydratedDefaultedBackendUnit.animAttack1Index != 2 ||
        hydratedDefaultedBackendUnit.animFaintIndex != 3 ||
        hydratedDefaultedBackendUnit.animGroundIdleIndex != 0 ||
        hydratedDefaultedBackendUnit.animAirIdleIndex != 0 ||
        hydratedDefaultedBackendUnit.activeAnimIndex != 0) {
        std::ostringstream oss;
        oss << "backend default role mismatch: idle=" << hydratedDefaultedBackendUnit.animIdleIndex
            << " move=" << hydratedDefaultedBackendUnit.animMoveIndex
            << " attack=" << hydratedDefaultedBackendUnit.animAttack1Index
            << " faint=" << hydratedDefaultedBackendUnit.animFaintIndex
            << " groundIdle=" << hydratedDefaultedBackendUnit.animGroundIdleIndex
            << " airIdle=" << hydratedDefaultedBackendUnit.animAirIdleIndex
            << " active=" << hydratedDefaultedBackendUnit.activeAnimIndex;
        outFail = oss.str();
        std::filesystem::current_path(originalCwd, ec);
        removeTempDir(tempDir);
        return false;
    }

    if (cache.size() != 2u) {
        outFail = "SessionBackendUnitHydration should cache one backend role entry per resolved model path.";
        std::filesystem::current_path(originalCwd, ec);
        removeTempDir(tempDir);
        return false;
    }

    std::filesystem::current_path(originalCwd, ec);
    removeTempDir(tempDir);
    return true;
}
