// PAC_DataCooker.cpp
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <unordered_set>
#include <vector>

#include <nlohmann/json.hpp>

#include "engine/core/AssetPackFormat.h"

namespace fs = std::filesystem;

namespace {

struct ValidationStats {
    int errors = 0;
    int warnings = 0;
};

void reportError(ValidationStats& stats, const std::string& file, const std::string& message) {
    ++stats.errors;
    std::cerr << "[ERROR] " << file << ": " << message << "\n";
}

void reportWarning(ValidationStats& stats, const std::string& file, const std::string& message) {
    ++stats.warnings;
    std::cerr << "[WARN ] " << file << ": " << message << "\n";
}

bool readFileBytes(const fs::path& path, std::vector<std::uint8_t>& outBytes, std::string* outError = nullptr) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) {
        if (outError) *outError = "Failed to open " + path.string();
        return false;
    }
    const std::ifstream::pos_type size = in.tellg();
    if (size < 0) {
        if (outError) *outError = "Failed to read " + path.string();
        return false;
    }
    outBytes.resize(static_cast<size_t>(size));
    in.seekg(0, std::ios::beg);
    if (!outBytes.empty()) {
        in.read(reinterpret_cast<char*>(outBytes.data()), size);
        if (!in) {
            if (outError) *outError = "Failed to read " + path.string();
            return false;
        }
    }
    return true;
}

bool readFileText(const fs::path& path, std::string& outText, std::string* outError = nullptr) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        if (outError) *outError = "Failed to open " + path.string();
        return false;
    }
    std::string contents((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    outText = std::move(contents);
    return true;
}

std::string normalizeVirtualPath(const fs::path& root, const fs::path& file) {
    fs::path rel = file.lexically_relative(root);
    std::string path = rel.generic_string();
    if (path.rfind("./", 0) == 0) path.erase(0, 2);
    while (!path.empty() && (path.front() == '/' || path.front() == '\\')) {
        path.erase(path.begin());
    }
    return path;
}

bool expectNumber(const nlohmann::json& obj, const char* key, const std::string& ctx,
                  ValidationStats& stats, bool required = true) {
    if (!obj.contains(key)) {
        if (required) reportError(stats, ctx, std::string("Missing numeric field '") + key + "'");
        return !required;
    }
    if (!obj[key].is_number()) {
        reportError(stats, ctx, std::string("Field '") + key + "' must be a number");
        return false;
    }
    return true;
}

bool expectString(const nlohmann::json& obj, const char* key, const std::string& ctx,
                  ValidationStats& stats, bool required = true) {
    if (!obj.contains(key)) {
        if (required) reportError(stats, ctx, std::string("Missing string field '") + key + "'");
        return !required;
    }
    if (!obj[key].is_string()) {
        reportError(stats, ctx, std::string("Field '") + key + "' must be a string");
        return false;
    }
    return true;
}

bool validatePokemonConfig(const nlohmann::json& j, const std::string& file, ValidationStats& stats) {
    if (!j.is_object()) {
        reportError(stats, file, "Root must be a JSON object");
        return false;
    }
    for (const auto& [name, data] : j.items()) {
        const std::string ctx = file + ":" + name;
        if (!data.is_object()) {
            reportError(stats, ctx, "Pokemon entry must be an object");
            continue;
        }
        expectNumber(data, "hp", ctx, stats);
        expectNumber(data, "attack", ctx, stats);
        expectNumber(data, "movementSpeed", ctx, stats);
        expectString(data, "model", ctx, stats);

        if (!data.contains("loadoutByLevel")) {
            reportWarning(stats, ctx, "Missing loadoutByLevel table");
        } else if (!data["loadoutByLevel"].is_object()) {
            reportError(stats, ctx, "loadoutByLevel must be an object");
        } else {
            for (const auto& [lvlKey, row] : data["loadoutByLevel"].items()) {
                const std::string lctx = ctx + ".loadoutByLevel[" + lvlKey + "]";
                if (!row.is_object()) {
                    reportError(stats, lctx, "Loadout entry must be an object");
                    continue;
                }
                if (row.contains("fast") && !row["fast"].is_string()) {
                    reportError(stats, lctx, "fast must be a string");
                }
                if (row.contains("charged") && !row["charged"].is_string()) {
                    reportError(stats, lctx, "charged must be a string");
                }
                try {
                    (void)std::stoi(lvlKey);
                } catch (...) {
                    reportError(stats, lctx, "Level key must be an integer");
                }
            }
        }
    }
    return true;
}

bool validateMovesConfig(const nlohmann::json& j, const std::string& file, ValidationStats& stats) {
    if (!j.is_object()) {
        reportError(stats, file, "Root must be a JSON object");
        return false;
    }
    for (const auto& [name, data] : j.items()) {
        const std::string ctx = file + ":" + name;
        if (!data.is_object()) {
            reportError(stats, ctx, "Move entry must be an object");
            continue;
        }
        expectString(data, "kind", ctx, stats);
        expectNumber(data, "power", ctx, stats);
        expectNumber(data, "cooldownSec", ctx, stats);
        expectNumber(data, "range", ctx, stats);
        expectString(data, "type", ctx, stats);

        if (data.contains("kind") && data["kind"].is_string()) {
            const std::string kind = data["kind"].get<std::string>();
            if (kind == "fast") {
                expectNumber(data, "energyGain", ctx, stats);
            } else if (kind == "charged") {
                expectNumber(data, "energyCost", ctx, stats);
            } else {
                reportError(stats, ctx, "kind must be 'fast' or 'charged'");
            }
        }

        if (data.contains("status")) {
            if (!data["status"].is_object()) {
                reportError(stats, ctx, "status must be an object");
            } else {
                const auto& status = data["status"];
                expectString(status, "effect", ctx + ".status", stats);
                expectString(status, "target", ctx + ".status", stats);
                expectNumber(status, "durationSec", ctx + ".status", stats);
                expectNumber(status, "magnitude", ctx + ".status", stats);
            }
        }
    }
    return true;
}

bool validateAttackAnimConfig(const nlohmann::json& j, const std::string& file, ValidationStats& stats) {
    if (!j.is_object()) {
        reportError(stats, file, "Root must be a JSON object");
        return false;
    }
    for (const auto& [species, data] : j.items()) {
        const std::string ctx = file + ":" + species;
        if (!data.is_object()) {
            reportError(stats, ctx, "Species entry must be an object");
            continue;
        }
        for (const char* section : { "fast", "charged" }) {
            if (!data.contains(section)) continue;
            if (!data[section].is_object()) {
                reportError(stats, ctx, std::string(section) + " must be an object");
                continue;
            }
            for (const auto& [moveName, moveData] : data[section].items()) {
                const std::string mctx = ctx + "." + section + "." + moveName;
                if (!moveData.is_object()) {
                    reportError(stats, mctx, "Move entry must be an object");
                    continue;
                }
                if (moveData.contains("_minRequestSec") && !moveData["_minRequestSec"].is_number()) {
                    reportError(stats, mctx, "_minRequestSec must be a number");
                }
                if (moveData.contains("_hitFrame") && !moveData["_hitFrame"].is_number()) {
                    reportError(stats, mctx, "_hitFrame must be a number");
                }
                for (const char* key : { "loop", "default", "one_shot" }) {
                    if (moveData.contains(key) && !moveData[key].is_string()) {
                        reportError(stats, mctx, std::string(key) + " must be a string");
                    }
                }
            }
        }
    }
    return true;
}

bool validateFlyersConfig(const nlohmann::json& j, const std::string& file, ValidationStats& stats) {
    if (!j.is_object()) {
        reportError(stats, file, "Root must be a JSON object");
        return false;
    }
    if (j.contains("flyers")) {
        if (!j["flyers"].is_array()) {
            reportError(stats, file, "flyers must be an array");
        } else {
            for (const auto& v : j["flyers"]) {
                if (!v.is_string()) {
                    reportError(stats, file, "flyers entries must be strings");
                    break;
                }
            }
        }
    } else {
        reportWarning(stats, file, "Missing flyers list");
    }

    if (j.contains("airLocomotionDefaults")) {
        if (!j["airLocomotionDefaults"].is_object()) {
            reportError(stats, file, "airLocomotionDefaults must be an object");
        } else {
            for (const auto& [species, entry] : j["airLocomotionDefaults"].items()) {
                const std::string ctx = file + ".airLocomotionDefaults." + species;
                if (!entry.is_object()) {
                    reportError(stats, ctx, "Entry must be an object");
                    continue;
                }
                if (entry.contains("airLiftY") && !entry["airLiftY"].is_number()) {
                    reportError(stats, ctx, "airLiftY must be a number");
                }
            }
        }
    }
    return true;
}

bool validatePreloadConfig(const nlohmann::json& j, const std::string& file, ValidationStats& stats) {
    if (!j.is_object()) {
        reportError(stats, file, "Root must be a JSON object");
        return false;
    }
    if (j.contains("model_root") && !j["model_root"].is_string()) {
        reportError(stats, file, "model_root must be a string");
    }
    auto validateStringArray = [&](const char* key) {
        if (!j.contains(key)) return;
        if (!j[key].is_array()) {
            reportError(stats, file, std::string(key) + " must be an array");
            return;
        }
        for (const auto& v : j[key]) {
            if (!v.is_string()) {
                reportError(stats, file, std::string(key) + " entries must be strings");
                break;
            }
        }
    };
    validateStringArray("pokemon");
    validateStringArray("models");
    return true;
}

bool parseJsonFile(const fs::path& path, nlohmann::json& out, std::string& outError) {
    std::string text;
    if (!readFileText(path, text, &outError)) return false;
    try {
        out = nlohmann::json::parse(text);
        return true;
    } catch (const std::exception& e) {
        outError = e.what();
        return false;
    } catch (...) {
        outError = "unknown exception";
        return false;
    }
}

bool writeU32(std::ofstream& out, std::uint32_t v) {
    out.write(reinterpret_cast<const char*>(&v), sizeof(v));
    return static_cast<bool>(out);
}

bool writeU64(std::ofstream& out, std::uint64_t v) {
    out.write(reinterpret_cast<const char*>(&v), sizeof(v));
    return static_cast<bool>(out);
}

struct PackedEntry {
    std::string path;
    std::vector<std::uint8_t> bytes;
    std::uint64_t offset = 0;
};

bool writePackFile(const fs::path& outPath, std::vector<PackedEntry>& entries, std::string& outError) {
    std::ofstream out(outPath, std::ios::binary | std::ios::trunc);
    if (!out) {
        outError = "Failed to open output file: " + outPath.string();
        return false;
    }

    // Header placeholder
    out.write(engine::assets::kPackMagic, sizeof(engine::assets::kPackMagic));
    if (!writeU32(out, engine::assets::kPackVersion)) return false;
    if (!writeU32(out, static_cast<std::uint32_t>(entries.size()))) return false;
    if (!writeU64(out, 0)) return false;

    // Data region
    for (auto& entry : entries) {
        entry.offset = static_cast<std::uint64_t>(out.tellp());
        if (!entry.bytes.empty()) {
            out.write(reinterpret_cast<const char*>(entry.bytes.data()),
                      static_cast<std::streamsize>(entry.bytes.size()));
        }
        if (!out) {
            outError = "Failed to write data for " + entry.path;
            return false;
        }
    }

    const std::uint64_t indexOffset = static_cast<std::uint64_t>(out.tellp());

    // Index
    for (const auto& entry : entries) {
        if (entry.path.size() > engine::assets::kPackMaxPathLength) {
            outError = "Path too long: " + entry.path;
            return false;
        }
        const std::uint32_t pathLen = static_cast<std::uint32_t>(entry.path.size());
        if (!writeU32(out, pathLen)) return false;
        if (!writeU64(out, entry.offset)) return false;
        if (!writeU64(out, static_cast<std::uint64_t>(entry.bytes.size()))) return false;
        out.write(entry.path.data(), static_cast<std::streamsize>(entry.path.size()));
        if (!out) {
            outError = "Failed to write index for " + entry.path;
            return false;
        }
    }

    // Patch header with indexOffset
    out.seekp(0, std::ios::beg);
    out.write(engine::assets::kPackMagic, sizeof(engine::assets::kPackMagic));
    if (!writeU32(out, engine::assets::kPackVersion)) return false;
    if (!writeU32(out, static_cast<std::uint32_t>(entries.size()))) return false;
    if (!writeU64(out, indexOffset)) return false;

    return true;
}

void printUsage() {
    std::cout <<
        "PAC_DataCooker\n"
        "Usage:\n"
        "  PAC_DataCooker [--root <path>] [--out <path>] [--validate-only] [--verbose]\n"
        "\n"
        "Defaults:\n"
        "  --root .\n"
        "  --out  content_pak/content.pak\n";
}

} // namespace

int main(int argc, char** argv) {
    std::string root = ".";
    std::string outPath = "content_pak/content.pak";
    bool validateOnly = false;
    bool verbose = false;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--root" && i + 1 < argc) {
            root = argv[++i];
        } else if (arg == "--out" && i + 1 < argc) {
            outPath = argv[++i];
        } else if (arg == "--validate-only") {
            validateOnly = true;
        } else if (arg == "--verbose") {
            verbose = true;
        } else if (arg == "--help" || arg == "-h") {
            printUsage();
            return 0;
        } else {
            std::cerr << "Unknown argument: " << arg << "\n";
            printUsage();
            return 1;
        }
    }

    fs::path rootPath = fs::absolute(fs::path(root));
    if (!fs::exists(rootPath) || !fs::is_directory(rootPath)) {
        std::cerr << "Root path is not a directory: " << rootPath.string() << "\n";
        return 1;
    }

    ValidationStats stats;

    const std::vector<std::string> requiredConfigs = {
        "config/pokemon_config.json",
        "config/moves_config.json",
        "config/attack_anim_config.json",
        "config/flyers_config.json",
        "config/preload_models.json"
    };

    const std::vector<std::string> requiredScripts = {
        "scripts/states/starter.lua",
        "scripts/states/flow.lua",
        "scripts/states/route1.lua",
        "scripts/systems/round_system.lua",
        "scripts/systems/combat.lua",
        "scripts/systems/movement.lua",
        "scripts/systems/camera.lua"
    };

    for (const auto& rel : requiredConfigs) {
        if (!fs::exists(rootPath / rel)) {
            reportError(stats, rel, "Missing required config file");
        }
    }
    for (const auto& rel : requiredScripts) {
        if (!fs::exists(rootPath / rel)) {
            reportError(stats, rel, "Missing required script file");
        }
    }

    // Validate known JSON configs.
    for (const auto& rel : requiredConfigs) {
        const fs::path cfgPath = rootPath / rel;
        if (!fs::exists(cfgPath)) continue;
        nlohmann::json j;
        std::string err;
        if (!parseJsonFile(cfgPath, j, err)) {
            reportError(stats, rel, "Failed to parse JSON (" + err + ")");
            continue;
        }

        if (rel.find("pokemon_config.json") != std::string::npos) {
            validatePokemonConfig(j, rel, stats);
        } else if (rel.find("moves_config.json") != std::string::npos) {
            validateMovesConfig(j, rel, stats);
        } else if (rel.find("attack_anim_config.json") != std::string::npos) {
            validateAttackAnimConfig(j, rel, stats);
        } else if (rel.find("flyers_config.json") != std::string::npos) {
            validateFlyersConfig(j, rel, stats);
        } else if (rel.find("preload_models.json") != std::string::npos) {
            validatePreloadConfig(j, rel, stats);
        }
    }

    if (stats.errors > 0) {
        std::cerr << "Validation failed (" << stats.errors << " errors, "
                  << stats.warnings << " warnings)\n";
        return 1;
    }

    if (validateOnly) {
        std::cout << "Validation OK (" << stats.warnings << " warnings)\n";
        return 0;
    }

    // Gather files under config/ and scripts/
    std::vector<PackedEntry> entries;
    std::unordered_set<std::string> seen;
    for (const char* dirName : {"config", "scripts"}) {
        fs::path dirPath = rootPath / dirName;
        if (!fs::exists(dirPath)) continue;
        for (const auto& entry : fs::recursive_directory_iterator(dirPath)) {
            if (!entry.is_regular_file()) continue;
            const fs::path filePath = entry.path();
            std::string vpath = normalizeVirtualPath(rootPath, filePath);
            if (vpath.empty()) continue;
            if (!seen.insert(vpath).second) continue;

            std::vector<std::uint8_t> bytes;
            std::string err;
            if (!readFileBytes(filePath, bytes, &err)) {
                reportError(stats, vpath, err);
                continue;
            }
            entries.push_back(PackedEntry{vpath, std::move(bytes), 0});
        }
    }

    if (entries.empty()) {
        std::cerr << "No files found under config/ or scripts/\n";
        return 1;
    }

    std::sort(entries.begin(), entries.end(),
              [](const PackedEntry& a, const PackedEntry& b) { return a.path < b.path; });

    fs::path outFilePath = fs::path(outPath);
    if (outFilePath.is_relative()) outFilePath = rootPath / outFilePath;
    if (!outFilePath.parent_path().empty()) {
        fs::create_directories(outFilePath.parent_path());
    }

    std::string err;
    if (!writePackFile(outFilePath, entries, err)) {
        std::cerr << "Failed to write pack: " << err << "\n";
        return 1;
    }

    std::cout << "Packed " << entries.size() << " files to " << outFilePath.string() << "\n";
    if (stats.warnings > 0) {
        std::cout << "Warnings: " << stats.warnings << "\n";
    }
    if (verbose) {
        for (const auto& entry : entries) {
            std::cout << "  " << entry.path << " (" << entry.bytes.size() << " bytes)\n";
        }
    }

    return 0;
}
