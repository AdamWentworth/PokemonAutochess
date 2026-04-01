#include "game/runtime/shared/projected/SharedProjectedUnitWorldSceneTrace.h"

#include "game/runtime/shared/projected/SharedProjectedUnitWorldSceneRenderer.h"
#include "engine/core/Environment.h"

#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

#include <glm/gtc/type_ptr.hpp>

namespace support = game::runtime::shared_projected_unit_backend_mesh_support;

namespace game::runtime::shared_projected_unit_world_scene_trace {
namespace {

std::string toLowerCopy(std::string_view value) {
    std::string out;
    out.reserve(value.size());
    for (const char c : value) {
        out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    return out;
}

const std::vector<std::string>& worldSceneTraceTokens() {
    static const std::vector<std::string> tokens = [] {
        std::vector<std::string> out;
        const auto env = engine::env::get("PAC_TRACE_PROJECTED_WORLD_SCENE");
        if (!env.has_value()) return out;

        std::string current;
        auto flush = [&]() {
            if (current.empty()) return;
            out.push_back(toLowerCopy(current));
            current.clear();
        };
        for (const char c : *env) {
            if (c == ',' || c == ';' || std::isspace(static_cast<unsigned char>(c))) {
                flush();
            } else {
                current.push_back(c);
            }
        }
        flush();
        return out;
    }();
    return tokens;
}

const std::vector<std::string>& worldSceneDisableTokens() {
    static const std::vector<std::string> tokens = [] {
        std::vector<std::string> out;
        const auto env = engine::env::get("PAC_DISABLE_PROJECTED_WORLD_SCENE");
        if (!env.has_value()) return out;

        std::string current;
        auto flush = [&]() {
            if (current.empty()) return;
            out.push_back(toLowerCopy(current));
            current.clear();
        };
        for (const char c : *env) {
            if (c == ',' || c == ';' || std::isspace(static_cast<unsigned char>(c))) {
                flush();
            } else {
                current.push_back(c);
            }
        }
        flush();
        return out;
    }();
    return tokens;
}

const std::string& worldSceneTraceFilePath() {
    static const std::string path = []() {
        const auto env = engine::env::get("PAC_TRACE_PROJECTED_WORLD_SCENE_FILE");
        if (env.has_value() && !env->empty()) {
            return *env;
        }
        return std::string("debug_projected_world_scene_trace.log");
    }();
    return path;
}

bool tokenListMatchesUnit(const std::vector<std::string>& tokens, const PokemonInstance& unit) {
    if (tokens.empty()) return false;
    const std::string unitName = toLowerCopy(unit.name);
    const std::string unitId = std::to_string(unit.id);
    for (const std::string& token : tokens) {
        if (token == "*" || token == unitName || token == unitId) {
            return true;
        }
    }
    return false;
}

std::uint64_t fnv1a64Append(std::uint64_t hash, const void* data, std::size_t byteCount) {
    static constexpr std::uint64_t kPrime = 1099511628211ull;
    const auto* bytes = static_cast<const std::uint8_t*>(data);
    for (std::size_t i = 0; i < byteCount; ++i) {
        hash ^= static_cast<std::uint64_t>(bytes[i]);
        hash *= kPrime;
    }
    return hash;
}

void appendTraceLineImpl(std::string_view line) {
    if (line.empty()) {
        return;
    }

    std::cout << line << "\n" << std::flush;

    const std::string& path = worldSceneTraceFilePath();
    if (path.empty()) {
        return;
    }

    static std::mutex traceFileMutex;
    std::lock_guard<std::mutex> lock(traceFileMutex);
    std::error_code ec;
    const std::filesystem::path fsPath(path);
    if (!fsPath.parent_path().empty()) {
        std::filesystem::create_directories(fsPath.parent_path(), ec);
    }
    std::ofstream out(fsPath, std::ios::app);
    if (!out.is_open()) {
        return;
    }
    out << line << "\n";
    out.flush();
}

} // namespace

bool shouldTraceUnit(const PokemonInstance& unit) {
    return tokenListMatchesUnit(worldSceneTraceTokens(), unit);
}

bool shouldDisableUnit(const PokemonInstance& unit) {
    return tokenListMatchesUnit(worldSceneDisableTokens(), unit);
}

void appendTraceLine(std::string_view line) {
    appendTraceLineImpl(line);
}

std::uint64_t hashPoseEval(const game::runtime::shared_backend_pose::PoseEval* scenePose) {
    if (!scenePose || !scenePose->hasScenePose || scenePose->nodeGlobals.empty()) {
        return 0ull;
    }
    std::uint64_t hash = 14695981039346656037ull;
    for (const glm::mat4& nodeGlobal : scenePose->nodeGlobals) {
        hash = fnv1a64Append(hash, glm::value_ptr(nodeGlobal), sizeof(float) * 16u);
    }
    return hash;
}

std::uint64_t hashSkinPayload(const support::GpuSkinBatchState& state) {
    if (!state.sharedSkinMatrices || state.skinMatrixCount == 0u) {
        return 0ull;
    }
    const std::size_t floatCount =
        static_cast<std::size_t>(state.skinMatrixCount) *
        static_cast<std::size_t>(state.gpuSkinningMode == 1u ? 32u : 16u);
    std::uint64_t hash = 14695981039346656037ull;
    hash = fnv1a64Append(hash, state.sharedSkinMatrices, floatCount * sizeof(float));
    hash = fnv1a64Append(hash, &state.skinMatrixCount, sizeof(state.skinMatrixCount));
    hash = fnv1a64Append(hash, &state.gpuSkinningMode, sizeof(state.gpuSkinningMode));
    return hash;
}

void traceFrameSummary(
    const game::runtime::shared_projected_unit_models::Args& args,
    const game::runtime::shared_projected_unit_backend_mesh_prep::PreparedState& prepared,
    std::size_t rigidBatchCount,
    std::size_t skinnedBatchCount,
    std::uint64_t batchHash,
    std::uint64_t poseHash) {
    if (!args.unit || !shouldTraceUnit(*args.unit)) {
        return;
    }

    std::ostringstream line;
    line
        << "[ProjectedTrace][WorldScene] unit=" << args.unit->name
        << " id=" << args.unit->id
        << " active=" << args.unit->activeAnimIndex
        << " idle=" << args.unit->animIdleIndex
        << " time=" << args.unit->animTimeSec
        << " batches=" << (rigidBatchCount + skinnedBatchCount)
        << " rigid=" << rigidBatchCount
        << " skinned=" << skinnedBatchCount
        << " poseHash=0x" << std::hex << poseHash
        << " batchHash=0x" << batchHash << std::dec
        << " sortDepth=" << prepared.indexedBatchSortDepth;
    appendTraceLineImpl(line.str());
}

void traceSkip(const game::runtime::shared_projected_unit_models::Args& args, const char* reason) {
    if (!args.unit || !shouldTraceUnit(*args.unit)) {
        return;
    }
    std::ostringstream line;
    line
        << "[ProjectedTrace][WorldScene][Skip] unit=" << args.unit->name
        << " id=" << args.unit->id
        << " active=" << args.unit->activeAnimIndex
        << " idle=" << args.unit->animIdleIndex
        << " time=" << args.unit->animTimeSec
        << " reason=" << (reason ? reason : "unknown");
    appendTraceLineImpl(line.str());
}

void traceEnter(const game::runtime::shared_projected_unit_models::Args& args) {
    if (!args.unit || !shouldTraceUnit(*args.unit)) {
        return;
    }
    std::ostringstream line;
    line
        << "[ProjectedTrace][WorldScene][Enter] unit=" << args.unit->name
        << " id=" << args.unit->id
        << " active=" << args.unit->activeAnimIndex
        << " idle=" << args.unit->animIdleIndex
        << " time=" << args.unit->animTimeSec
        << " renderer=" << (args.backendId ? args.backendId : "<null>");
    appendTraceLineImpl(line.str());
}

} // namespace game::runtime::shared_projected_unit_world_scene_trace

namespace game::runtime::shared_projected_unit_world_scene {

bool shouldTraceProjectedUnitWorldScene(const PokemonInstance& unit) {
    return shared_projected_unit_world_scene_trace::shouldTraceUnit(unit);
}

void appendProjectedUnitWorldSceneTraceLine(std::string_view line) {
    shared_projected_unit_world_scene_trace::appendTraceLine(line);
}

} // namespace game::runtime::shared_projected_unit_world_scene
