#include <filesystem>
#include <fstream>
#include <string>

#include "game/runtime/shared/vfx/authored/SharedAuthoredVfxInterop.h"

namespace {

bool fileContainsToken(const std::filesystem::path& path, const std::string& token) {
    std::ifstream in(path);
    if (!in.is_open()) return false;

    std::string line;
    while (std::getline(in, line)) {
        if (line.find(token) != std::string::npos) return true;
    }
    return false;
}

bool expect(bool condition, const std::string& message, std::string& outFail) {
    if (condition) return true;
    outFail = message;
    return false;
}

} // namespace

bool test_shared_authored_vfx_interop_contract(std::string& outFail) {
    vfx::runtime::authored_batches::WorldIndexedBatch src;
    src.textureKey = "tackle_late_stack";
    src.alphaMode = 2u;
    src.blendMode = 1u;
    src.dualSourceBlendEnabled = 1u;

    const game::runtime::shared_world_batches::WorldIndexedBatch dst =
        game::runtime::shared_authored_vfx_interop::toWorldIndexedBatch(src);

    if (!expect(dst.dualSourceBlendEnabled == 1u,
                "toWorldIndexedBatch should preserve dualSourceBlendEnabled from authored VFX batches.",
                outFail)) {
        return false;
    }

    const std::filesystem::path worldBatchesPath =
        "src/game/runtime/shared/world/SharedWorldIndexedBatches.cpp";
    if (!expect(std::filesystem::exists(worldBatchesPath),
                "missing shared world indexed batches source file.",
                outFail)) {
        return false;
    }

    if (!expect(fileContainsToken(worldBatchesPath, "effectiveDualSourceBlendEnabled(") &&
                    fileContainsToken(worldBatchesPath, "tex.dualSourceBlendEnabled =") &&
                    fileContainsToken(worldBatchesPath, "key.material.dualSourceBlendEnabled ="),
                "shared world indexed submit path should propagate dualSourceBlendEnabled through sort and backend texture submission.",
                outFail)) {
        return false;
    }

    return true;
}
