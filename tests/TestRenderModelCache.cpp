#include "game/runtime/render_model_cache/RenderModelCache.h"
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <nlohmann/json.hpp>

namespace {

bool contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

bool hasGrowlAnchorCandidate(const game::runtime::render_model::MeshData& mesh) {
    static constexpr std::array<const char*, 11> kGrowlAnchorTokens = {
        "effmouth01", "mouth01.", "mouth01", "mouth", "jaw", "nose",
        "snout", "muzzle", "head", "neck", "chin"};

    for (const std::string& nodeName : mesh.nodeNames) {
        std::string lower = nodeName;
        std::transform(lower.begin(),
                       lower.end(),
                       lower.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        for (const char* token : kGrowlAnchorTokens) {
            if (token && lower.find(token) != std::string::npos) {
                return true;
            }
        }
    }
    return false;
}

bool readSourceTexcoord0Uvs(const std::string& glbPath,
                            std::vector<glm::vec2>& outUvs,
                            std::string& outError) {
    outUvs.clear();

    std::ifstream in(glbPath, std::ios::binary);
    if (!in.is_open()) {
        outError = "failed to open source GLB";
        return false;
    }

    std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(in)),
                                    std::istreambuf_iterator<char>());
    if (bytes.size() < 12u) {
        outError = "GLB too small";
        return false;
    }

    auto readU32 = [&](std::size_t offset) -> std::uint32_t {
        std::uint32_t value = 0u;
        std::memcpy(&value, bytes.data() + offset, sizeof(value));
        return value;
    };

    constexpr std::uint32_t kGlbMagic = 0x46546C67u;
    constexpr std::uint32_t kJsonChunkType = 0x4E4F534Au;
    constexpr std::uint32_t kBinChunkType = 0x004E4942u;

    const std::uint32_t magic = readU32(0u);
    if (magic != kGlbMagic) {
        outError = "invalid GLB magic";
        return false;
    }

    std::vector<std::uint8_t> jsonChunk;
    std::vector<std::uint8_t> binChunk;
    std::size_t offset = 12u;
    while (offset + 8u <= bytes.size()) {
        const std::uint32_t chunkLength = readU32(offset + 0u);
        const std::uint32_t chunkType = readU32(offset + 4u);
        offset += 8u;
        if (offset + static_cast<std::size_t>(chunkLength) > bytes.size()) {
            outError = "GLB chunk exceeds file length";
            return false;
        }
        const auto begin = bytes.begin() + static_cast<std::ptrdiff_t>(offset);
        const auto end = begin + static_cast<std::ptrdiff_t>(chunkLength);
        if (chunkType == kJsonChunkType) {
            jsonChunk.assign(begin, end);
        } else if (chunkType == kBinChunkType) {
            binChunk.assign(begin, end);
        }
        offset += static_cast<std::size_t>(chunkLength);
    }

    if (jsonChunk.empty() || binChunk.empty()) {
        outError = "GLB missing JSON or BIN chunk";
        return false;
    }

    nlohmann::json asset;
    try {
        asset = nlohmann::json::parse(jsonChunk.begin(), jsonChunk.end());
    } catch (const std::exception& ex) {
        outError = std::string("failed to parse GLB JSON: ") + ex.what();
        return false;
    }

    if (!asset.contains("meshes") || !asset["meshes"].is_array() || asset["meshes"].empty()) {
        outError = "GLB missing mesh array";
        return false;
    }
    const auto& mesh = asset["meshes"][0];
    if (!mesh.contains("primitives") || !mesh["primitives"].is_array() || mesh["primitives"].empty()) {
        outError = "GLB missing primitive array";
        return false;
    }
    const auto& primitive = mesh["primitives"][0];
    if (!primitive.contains("attributes") ||
        !primitive["attributes"].contains("TEXCOORD_0")) {
        outError = "GLB primitive missing TEXCOORD_0";
        return false;
    }

    const std::size_t accessorIndex = primitive["attributes"]["TEXCOORD_0"].get<std::size_t>();
    if (!asset.contains("accessors") || accessorIndex >= asset["accessors"].size()) {
        outError = "UV accessor missing";
        return false;
    }
    const auto& accessor = asset["accessors"][accessorIndex];
    if (!accessor.contains("bufferView")) {
        outError = "UV accessor missing bufferView";
        return false;
    }
    if (accessor.value("componentType", 0) != 5126 ||
        accessor.value("type", std::string{}) != "VEC2") {
        outError = "UV accessor is not FLOAT VEC2";
        return false;
    }

    const std::size_t bufferViewIndex = accessor["bufferView"].get<std::size_t>();
    if (!asset.contains("bufferViews") || bufferViewIndex >= asset["bufferViews"].size()) {
        outError = "UV bufferView missing";
        return false;
    }
    const auto& bufferView = asset["bufferViews"][bufferViewIndex];

    const std::size_t count = accessor.value("count", 0u);
    const std::size_t accessorByteOffset = accessor.value("byteOffset", 0u);
    const std::size_t bufferViewByteOffset = bufferView.value("byteOffset", 0u);
    const std::size_t stride = bufferView.value("byteStride", static_cast<std::size_t>(sizeof(float) * 2u));
    const std::size_t start = bufferViewByteOffset + accessorByteOffset;
    const std::size_t requiredBytes = start + stride * count;
    if (requiredBytes > binChunk.size()) {
        outError = "UV accessor exceeds BIN chunk";
        return false;
    }

    outUvs.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        const std::size_t base = start + stride * i;
        float u = 0.0f;
        float v = 0.0f;
        std::memcpy(&u, binChunk.data() + base + 0u, sizeof(float));
        std::memcpy(&v, binChunk.data() + base + sizeof(float), sizeof(float));
        outUvs.emplace_back(u, v);
    }
    return true;
}

} // namespace

bool test_render_model_cache_contract(std::string& outFail) {
    {
        game::runtime::render_model::MeshData grounded;
        grounded.assetCacheIdentity = "test:semantic-foot-grounding";
        grounded.boundsMin = glm::vec3(-1.0f, -1.0f, -1.0f);
        grounded.nodeNames = {"root", "tail", "left_foot"};
        grounded.skins.resize(1u);
        grounded.skins[0].joints = {0, 1, 2};
        grounded.vertices.resize(4u);
        grounded.vertices[0].position.y = -1.0f;
        grounded.vertices[0].j0 = 1u;
        grounded.vertices[0].w0 = 1.0f;
        for (std::size_t index = 1u; index < 4u; ++index) {
            grounded.vertices[index].position.y =
                0.1f + static_cast<float>(index) * 0.01f;
            grounded.vertices[index].j0 = 2u;
            grounded.vertices[index].w0 = 1.0f;
        }
        grounded.indices = {0u, 1u, 2u, 1u, 2u, 3u};
        grounded.triangleSkinIndex = {0, 0};
        const float supportY =
            game::runtime::render_model::modelSupportContactY(
                grounded);
        if (std::abs(supportY - 0.11f) > 0.0001f) {
            outFail =
                "character grounding used a lower tail/shell bound instead of foot-weighted support geometry";
            return false;
        }
    }

    {
        game::runtime::render_model::MeshData anchored;
        anchored.assetCacheIdentity =
            "test:unweighted-gamefreak-support-anchor";
        anchored.boundsMin = glm::vec3(-1.0f, -15.0f, -1.0f);
        anchored.nodeNames = {"root", "body", "EffFoot01"};
        anchored.bindNodeGlobals = {
            glm::mat4(1.0f),
            glm::mat4(1.0f),
            glm::translate(
                glm::mat4(1.0f),
                glm::vec3(0.0f, -0.17f, 0.0f)),
        };
        anchored.skins.resize(1u);
        anchored.skins[0].joints = {0, 1, 2};
        anchored.vertices.resize(3u);
        for (auto& vertex : anchored.vertices) {
            vertex.position.y = -15.0f;
            vertex.j0 = 1u;
            vertex.w0 = 1.0f;
        }
        anchored.indices = {0u, 1u, 2u};
        anchored.triangleSkinIndex = {0};
        const float supportY =
            game::runtime::render_model::modelSupportContactY(
                anchored);
        if (std::abs(supportY + 0.17f) > 0.0001f) {
            outFail =
                "unweighted Game Freak EffFoot anchor did not override raw native bounds";
            return false;
        }
    }

    {
        game::runtime::render_model::MeshData serpentine;
        serpentine.assetCacheIdentity =
            "test:gamefreak-serpentine-ground-origin";
        serpentine.boundsMin = glm::vec3(-1.0f, -2.08f, -1.0f);
        serpentine.nodeNames = {
            "pm0024_00_00", "origin", "waist", "spine_01", "tail_01"};
        serpentine.bindNodeGlobals.assign(5u, glm::mat4(1.0f));
        serpentine.bindNodeGlobals[1] = glm::translate(
            glm::mat4(1.0f),
            glm::vec3(0.0f, 0.035f, 0.0f));
        serpentine.skins.resize(1u);
        serpentine.skins[0].joints = {0, 1, 2, 3, 4};
        serpentine.vertices.resize(3u);
        for (auto& vertex : serpentine.vertices) {
            vertex.position.y = -2.08f;
            vertex.j0 = 3u;
            vertex.w0 = 1.0f;
        }
        serpentine.indices = {0u, 1u, 2u};
        serpentine.triangleSkinIndex = {0};
        const float supportY =
            game::runtime::render_model::modelSupportContactY(
                serpentine);
        if (std::abs(supportY - 0.035f) > 0.0001f) {
            outFail =
                "Game Freak serpentine grounding used its straight bind bounds instead of the authored floor origin";
            return false;
        }
    }

    using game::runtime::render_model::MeshData;
    using game::runtime::render_model::CachedTextureRgba;
    using game::runtime::render_model::cachePathForModel;
    using game::runtime::render_model::loadMeshFromCache;

    const std::string pathA = cachePathForModel("assets/models/0004_Charmander.glb");
    const std::string pathB = cachePathForModel("assets/models/0004_Charmander.glb");
    const std::string pathC = cachePathForModel("assets/models/0007_Squirtle.glb");

    if (pathA != pathB) {
        outFail = "cachePathForModel should be deterministic for identical inputs";
        return false;
    }
    if (pathA == pathC) {
        outFail = "cachePathForModel should differ for different model paths";
        return false;
    }
    if (!contains(pathA, "cache") || !contains(pathA, ".pacmdl")) {
        outFail = "cachePathForModel should target cache/models/*.pacmdl";
        return false;
    }

    {
        MeshData mesh;
        std::string err;
        if (loadMeshFromCache("", mesh, &err)) {
            outFail = "loadMeshFromCache should fail for empty model path";
            return false;
        }
        if (err.empty()) {
            outFail = "loadMeshFromCache should provide an error for empty model path";
            return false;
        }
        if (!mesh.vertices.empty() ||
            !mesh.indices.empty() ||
            !mesh.triangleBaseColors.empty() ||
            !mesh.triangleOpacity.empty() ||
            !mesh.triangleDoubleSided.empty() ||
            !mesh.vertexBaseColors.empty() ||
            !mesh.submeshMeshIndex.empty() ||
            !mesh.submeshBaseTextures.empty() ||
            !mesh.submeshAlphaMode.empty() ||
            !mesh.submeshAlphaCutoff.empty() ||
            !mesh.meshIndexToNode.empty() ||
            !mesh.triangleNodeIndex.empty() ||
            !mesh.triangleSkinIndex.empty() ||
            !mesh.nodesDefault.empty() ||
            !mesh.bindNodeGlobals.empty() ||
            !mesh.animations.empty()) {
            outFail = "loadMeshFromCache should clear output mesh on failure";
            return false;
        }
    }

    {
        MeshData mesh;
        std::string err;
        if (loadMeshFromCache("assets/models/not_a_real_model.glb", mesh, &err)) {
            outFail = "loadMeshFromCache should fail when cache file is missing";
            return false;
        }
        if (err.empty()) {
            outFail = "loadMeshFromCache should provide an error when cache file is missing";
            return false;
        }
        if (!mesh.vertices.empty() ||
            !mesh.indices.empty() ||
            !mesh.triangleBaseColors.empty() ||
            !mesh.triangleOpacity.empty() ||
            !mesh.triangleDoubleSided.empty() ||
            !mesh.vertexBaseColors.empty() ||
            !mesh.submeshMeshIndex.empty() ||
            !mesh.submeshBaseTextures.empty() ||
            !mesh.submeshAlphaMode.empty() ||
            !mesh.submeshAlphaCutoff.empty() ||
            !mesh.meshIndexToNode.empty() ||
            !mesh.triangleNodeIndex.empty() ||
            !mesh.triangleSkinIndex.empty() ||
            !mesh.nodesDefault.empty() ||
            !mesh.bindNodeGlobals.empty() ||
            !mesh.animations.empty()) {
            outFail = "missing-cache load failure should leave mesh output empty";
            return false;
        }
    }

    {
        namespace fs = std::filesystem;
        const std::string fakeModel = "assets/models/_unit_test_corrupt_cache_probe.glb";
        const fs::path corruptPath = cachePathForModel(fakeModel);
        std::error_code ec;
        fs::create_directories(corruptPath.parent_path(), ec);
        if (ec) {
            outFail = "failed to create cache directory for corrupt-cache probe";
            return false;
        }

        {
            std::ofstream out(corruptPath, std::ios::binary | std::ios::trunc);
            if (!out.is_open()) {
                outFail = "failed to write corrupt cache probe file";
                return false;
            }
            const char payload[] = {'B', 'A', 'D', '!'};
            out.write(payload, sizeof(payload));
        }

        MeshData mesh;
        std::string err;
        const bool ok = loadMeshFromCache(fakeModel, mesh, &err);
        fs::remove(corruptPath, ec);
        if (ok) {
            outFail = "loadMeshFromCache should reject corrupt cache files";
            return false;
        }
        if (err.empty()) {
            outFail = "loadMeshFromCache should surface an error for corrupt cache files";
            return false;
        }
    }

    {
        namespace fs = std::filesystem;
        const fs::path modelsDir = "assets/models";
        for (const auto& entry : fs::directory_iterator(modelsDir)) {
            if (!entry.is_regular_file()) continue;
            if (entry.path().extension() != ".glb") continue;
            if (entry.path().filename() == "pokeball.glb") continue;

            MeshData mesh;
            std::string err;
            const std::string modelPath = entry.path().generic_string();
            if (!loadMeshFromCache(modelPath, mesh, &err)) {
                outFail =
                    "loadMeshFromCache should load every Pokemon mesh for Growl anchor lookup: " +
                    modelPath + " :: " + err;
                return false;
            }
            if (!hasGrowlAnchorCandidate(mesh)) {
                outFail =
                    "Pokemon cached mesh should expose at least one Growl anchor candidate node: " +
                    modelPath;
                return false;
            }
            const bool hasDetailedBaseTexture =
                std::any_of(
                    mesh.submeshBaseTextures.begin(),
                    mesh.submeshBaseTextures.end(),
                    [](const game::runtime::render_model::CachedTextureRgba& texture) {
                        return texture.hasPixels() &&
                               texture.width > 1 &&
                               texture.height > 1;
                    });
            if (!hasDetailedBaseTexture) {
                outFail =
                    "Pokemon cached mesh must retain at least one authored base-color texture instead of only flat fallback pixels: " +
                    modelPath;
                return false;
            }
        }
    }

    {
        MeshData regular;
        MeshData shiny;
        MeshData female;
        MeshData femaleShiny;
        std::string err;
        const std::array<std::pair<const char*, MeshData*>, 4> variants{{
            {"assets/models/0003_Venusaur_SV.phmodel", &regular},
            {"assets/models/0003_Venusaur_SV_Shiny.phmodel", &shiny},
            {"assets/models/0003_Venusaur_SV_Female.phmodel", &female},
            {"assets/models/0003_Venusaur_SV_Female_Shiny.phmodel", &femaleShiny},
        }};
        // The native Pokemon payloads are intentionally private/ignored. Keep
        // the public test suite asset-independent, while making a checkout
        // with the qualification fixtures prove that all four variants load
        // as distinct cooked objects and retain the sex-specific geometry.
        const bool hasQualificationFixtures = std::all_of(
            variants.begin(), variants.end(), [](const auto& variant) {
                return std::filesystem::is_regular_file(variant.first);
            });
        if (hasQualificationFixtures) {
            for (const auto& [path, mesh] : variants) {
                if (!loadMeshFromCache(path, *mesh, &err)) {
                    outFail =
                        "loadMeshFromCache should load the cooked Venusaur variant '" +
                        std::string(path) + "': " + err;
                    return false;
                }
                if (mesh->assetCacheIdentity.empty() ||
                    !contains(mesh->assetCacheIdentity, "phlo:")) {
                    outFail =
                        "cooked Pokemon variants must retain a stable PHLO cache identity: " +
                        std::string(path);
                    return false;
                }
            }
            if (regular.assetCacheIdentity == shiny.assetCacheIdentity ||
                regular.assetCacheIdentity == female.assetCacheIdentity ||
                regular.assetCacheIdentity == femaleShiny.assetCacheIdentity ||
                shiny.assetCacheIdentity == female.assetCacheIdentity ||
                shiny.assetCacheIdentity == femaleShiny.assetCacheIdentity ||
                female.assetCacheIdentity == femaleShiny.assetCacheIdentity) {
                outFail =
                    "regular, shiny, and sex-specific PHLO variants must not alias runtime cache identities";
                return false;
            }
            if (regular.vertices.size() == female.vertices.size()) {
                outFail =
                    "male and female Venusaur variants should retain their distinct source geometry";
                return false;
            }
        }
    }

    {
        MeshData regular;
        MeshData shiny;
        MeshData female;
        MeshData femaleShiny;
        std::string err;
        const std::array<std::pair<const char*, MeshData*>, 4> variants{{
            {"assets/models/0025_Pikachu_SV.phmodel", &regular},
            {"assets/models/0025_Pikachu_SV_Shiny.phmodel", &shiny},
            {"assets/models/0025_Pikachu_SV_Female.phmodel", &female},
            {"assets/models/0025_Pikachu_SV_Female_Shiny.phmodel", &femaleShiny},
        }};
        const bool hasQualificationFixtures = std::all_of(
            variants.begin(), variants.end(), [](const auto& variant) {
                return std::filesystem::is_regular_file(variant.first);
            });
        if (hasQualificationFixtures) {
            for (const auto& [path, mesh] : variants) {
                if (!loadMeshFromCache(path, *mesh, &err)) {
                    outFail =
                        "loadMeshFromCache should load the cooked Pikachu variant '" +
                        std::string(path) + "': " + err;
                    return false;
                }
            }
            if (regular.assetCacheIdentity == shiny.assetCacheIdentity ||
                regular.assetCacheIdentity == female.assetCacheIdentity ||
                regular.assetCacheIdentity == femaleShiny.assetCacheIdentity ||
                shiny.assetCacheIdentity == female.assetCacheIdentity ||
                shiny.assetCacheIdentity == femaleShiny.assetCacheIdentity ||
                female.assetCacheIdentity == femaleShiny.assetCacheIdentity) {
                outFail =
                    "Pikachu regular, shiny, and sex-specific PHLO variants must not alias runtime cache identities";
                return false;
            }
            if (regular.vertices.size() == female.vertices.size()) {
                outFail =
                    "male and female Pikachu variants should retain their distinct source tail geometry";
                return false;
            }

            const CachedTextureRgba* faceTexture = nullptr;
            for (std::size_t submesh = 0u;
                 submesh < regular.submeshMeshIndex.size() &&
                 submesh < regular.submeshBaseTextures.size();
                 ++submesh) {
                const int meshIndex = regular.submeshMeshIndex[submesh];
                if (meshIndex < 0 ||
                    static_cast<std::size_t>(meshIndex) >= regular.meshIndexToNode.size()) {
                    continue;
                }
                const int nodeIndex = regular.meshIndexToNode[static_cast<std::size_t>(meshIndex)];
                if (nodeIndex < 0 ||
                    static_cast<std::size_t>(nodeIndex) >= regular.nodeNames.size()) {
                    continue;
                }
                if (contains(regular.nodeNames[static_cast<std::size_t>(nodeIndex)],
                             "body_mesh_shape:body_c")) {
                    faceTexture = &regular.submeshBaseTextures[submesh];
                    break;
                }
            }
            if (!faceTexture || !faceTexture->hasPixels()) {
                outFail =
                    "Pikachu's body_c face material must retain a detailed cooked base-color texture";
                return false;
            }
            std::size_t redCheekPixels = 0u;
            const std::size_t pixelCount =
                static_cast<std::size_t>(faceTexture->width) *
                static_cast<std::size_t>(faceTexture->height);
            for (std::size_t pixel = 0u; pixel < pixelCount; ++pixel) {
                const std::size_t offset = pixel * 4u;
                const unsigned int r = faceTexture->rgba[offset + 0u];
                const unsigned int g = faceTexture->rgba[offset + 1u];
                const unsigned int b = faceTexture->rgba[offset + 2u];
                if (r >= 128u && g * 4u < r * 3u && b * 4u < r * 3u) {
                    ++redCheekPixels;
                }
            }
            if (redCheekPixels * 100u < pixelCount) {
                outFail =
                    "Pikachu's Scarlet/Violet SSS layer translation must preserve the authored red cheek coverage";
                return false;
            }
        }
    }

    {
        MeshData regular;
        MeshData shiny;
        MeshData female;
        MeshData femaleShiny;
        std::string err;
        const std::array<std::pair<const char*, MeshData*>, 4> variants{{
            {"assets/models/0026_Raichu_SV.phmodel", &regular},
            {"assets/models/0026_Raichu_SV_Shiny.phmodel", &shiny},
            {"assets/models/0026_Raichu_SV_Female.phmodel", &female},
            {"assets/models/0026_Raichu_SV_Female_Shiny.phmodel", &femaleShiny},
        }};
        const bool hasQualificationFixtures = std::all_of(
            variants.begin(), variants.end(), [](const auto& variant) {
                return std::filesystem::is_regular_file(variant.first);
            });
        if (hasQualificationFixtures) {
            std::set<std::string> identities;
            for (const auto& [path, mesh] : variants) {
                if (!loadMeshFromCache(path, *mesh, &err)) {
                    outFail =
                        "loadMeshFromCache should load the cooked Raichu variant '" +
                        std::string(path) + "': " + err;
                    return false;
                }
                identities.insert(mesh->assetCacheIdentity);
            }
            if (identities.size() != variants.size() ||
                regular.vertices.size() == female.vertices.size()) {
                outFail =
                    "Raichu regular, shiny, and sex-specific PHLO variants must retain distinct identities and tail geometry";
                return false;
            }
        }
    }

    {
        MeshData regular;
        MeshData shiny;
        std::string err;
        if (std::filesystem::is_regular_file(
                "assets/models/0172_Pichu_SV.phmodel") &&
            std::filesystem::is_regular_file(
                "assets/models/0172_Pichu_SV_Shiny.phmodel")) {
            if (!loadMeshFromCache(
                    "assets/models/0172_Pichu_SV.phmodel",
                    regular,
                    &err) ||
                !loadMeshFromCache(
                    "assets/models/0172_Pichu_SV_Shiny.phmodel",
                    shiny,
                    &err)) {
                outFail =
                    "loadMeshFromCache should load both cooked Pichu appearances: " +
                    err;
                return false;
            }
            if (regular.assetCacheIdentity == shiny.assetCacheIdentity) {
                outFail =
                    "Pichu regular and shiny PHLO appearances must not alias runtime cache identities";
                return false;
            }
        }
    }

    {
        MeshData regular;
        MeshData shiny;
        std::string err;
        const char* regularPath = "assets/models/0092_Gastly_SV.phmodel";
        const char* shinyPath = "assets/models/0092_Gastly_SV_Shiny.phmodel";
        if (std::filesystem::is_regular_file(regularPath) &&
            std::filesystem::is_regular_file(shinyPath)) {
            if (!loadMeshFromCache(regularPath, regular, &err) ||
                !loadMeshFromCache(shinyPath, shiny, &err)) {
                outFail =
                    "loadMeshFromCache should load both cooked Gastly appearances: " +
                    err;
                return false;
            }
            const auto retainsGastlyLayering = [](const MeshData& mesh) {
                return mesh.submeshMaterialModes.size() == 4u &&
                       mesh.submeshMaterialFlags.size() == 4u &&
                       mesh.submeshMaterialParams3.size() == 4u &&
                       mesh.submeshMaterialModes[0] == 2u &&
                       std::fabs(
                           mesh.submeshMaterialFlags[1] -
                           static_cast<float>(
                               game::runtime::render_model::
                                   kNativeFrontFacingOnlyMaterialFlagBit)) <
                           0.0001f &&
                       std::fabs(
                           mesh.submeshMaterialFlags[2] -
                           static_cast<float>(
                               game::runtime::render_model::
                                   kNativeFrontFacingOnlyMaterialFlagBit)) <
                           0.0001f &&
                       mesh.submeshMaterialModes[1] ==
                           game::runtime::render_model::
                               kNativeAnimatedEyeClearCoatMaterialMode &&
                       mesh.submeshMaterialModes[2] ==
                           game::runtime::render_model::
                               kNativeAnimatedEyeClearCoatMaterialMode &&
                       mesh.submeshNormalTextures.size() == 4u &&
                       mesh.submeshNormalTextures[1].hasPixels() &&
                       mesh.submeshNormalTextures[2].hasPixels() &&
                       mesh.submeshEmissiveTextures.size() == 4u &&
                       mesh.submeshEmissiveTextures[1].hasPixels() &&
                       mesh.submeshEmissiveTextures[2].hasPixels() &&
                       mesh.submeshRoughnessFactor.size() == 4u &&
                       std::fabs(mesh.submeshRoughnessFactor[1] - 0.2f) <
                           0.0001f &&
                       std::fabs(mesh.submeshRoughnessFactor[2] - 0.2f) <
                           0.0001f &&
                       std::fabs(mesh.submeshMaterialParams3[0].x - 0.020f) <
                           0.0001f &&
                       std::fabs(mesh.submeshMaterialParams3[1].x - 0.022f) <
                           0.0001f &&
                       std::fabs(mesh.submeshMaterialParams3[2].x - 0.022f) <
                           0.0001f &&
                       mesh.submeshMaterialModes[3] ==
                           game::runtime::render_model::
                               kNativeLayeredUnlitMaterialMode &&
                       std::fabs(mesh.submeshMaterialFlags[3] - 3.25f) <
                           0.0001f;
            };
            const auto rejectsGastlyEyeBackFaces = [](const MeshData& mesh) {
                const std::size_t triangleCount = mesh.indices.size() / 3u;
                if (mesh.triangleSubmesh.size() != triangleCount ||
                    mesh.triangleDoubleSided.size() != triangleCount) {
                    return false;
                }
                for (std::size_t triangle = 0u;
                     triangle < triangleCount;
                     ++triangle) {
                    const std::uint16_t submesh =
                        mesh.triangleSubmesh[triangle];
                    const std::uint8_t expected =
                        submesh == 1u || submesh == 2u ? 0u : 1u;
                    if (mesh.triangleDoubleSided[triangle] != expected) {
                        return false;
                    }
                }
                return true;
            };
            const auto averageSmokeRgb = [](const MeshData& mesh) {
                glm::dvec3 sum(0.0);
                if (mesh.submeshBaseTextures.size() <= 3u) {
                    return sum;
                }
                const auto& texture = mesh.submeshBaseTextures[3];
                const std::size_t pixelCount = texture.rgba.size() / 4u;
                for (std::size_t pixel = 0u; pixel < pixelCount; ++pixel) {
                    sum.r += texture.rgba[pixel * 4u + 0u];
                    sum.g += texture.rgba[pixel * 4u + 1u];
                    sum.b += texture.rgba[pixel * 4u + 2u];
                }
                return pixelCount > 0u
                    ? sum / static_cast<double>(pixelCount)
                    : glm::dvec3(0.0);
            };
            const glm::dvec3 regularSmokeRgb = averageSmokeRgb(regular);
            const glm::dvec3 shinySmokeRgb = averageSmokeRgb(shiny);
            const bool retainsGastlyPalettes =
                regular.submeshBaseTextures.size() == 4u &&
                shiny.submeshBaseTextures.size() == 4u &&
                regular.submeshBaseTextures[3].hasPixels() &&
                shiny.submeshBaseTextures[3].hasPixels() &&
                regularSmokeRgb.r > regularSmokeRgb.g &&
                regularSmokeRgb.b > regularSmokeRgb.g &&
                shinySmokeRgb.b > shinySmokeRgb.r &&
                shinySmokeRgb.b > shinySmokeRgb.g &&
                regular.submeshBaseTextures[3].rgba !=
                    shiny.submeshBaseTextures[3].rgba;
            if (!retainsGastlyLayering(regular) ||
                !retainsGastlyLayering(shiny) ||
                !rejectsGastlyEyeBackFaces(regular) ||
                !rejectsGastlyEyeBackFaces(shiny) ||
                !retainsGastlyPalettes) {
                const auto describe = [](const MeshData& mesh) {
                    std::string value =
                        " modes=" + std::to_string(mesh.submeshMaterialModes.size()) +
                        " flags=" + std::to_string(mesh.submeshMaterialFlags.size()) +
                        " params3=" + std::to_string(mesh.submeshMaterialParams3.size());
                    for (std::size_t i = 0u;
                         i < mesh.submeshMaterialModes.size();
                         ++i) {
                        value += " [" + std::to_string(i) + ":mode=" +
                                 std::to_string(mesh.submeshMaterialModes[i]);
                        if (i < mesh.submeshMaterialFlags.size()) {
                            value += ",flag=" +
                                     std::to_string(mesh.submeshMaterialFlags[i]);
                        }
                        if (i < mesh.submeshMaterialParams3.size()) {
                            value += ",p3x=" +
                                     std::to_string(mesh.submeshMaterialParams3[i].x);
                        }
                        value += "]";
                    }
                    return value;
                };
                outFail =
                    "cooked Gastly appearances must retain face-before-smoke depth ordering, front-only eye shells, and animated smoke mode; regular" +
                    describe(regular) + " rgb=" +
                    std::to_string(regularSmokeRgb.r) + "," +
                    std::to_string(regularSmokeRgb.g) + "," +
                    std::to_string(regularSmokeRgb.b) + "; shiny" +
                    describe(shiny) + " rgb=" +
                    std::to_string(shinySmokeRgb.r) + "," +
                    std::to_string(shinySmokeRgb.g) + "," +
                    std::to_string(shinySmokeRgb.b);
                return false;
            }
        }
    }

    for (const std::string growlMeshPath : {
             std::string("assets/meshes/growl_1255_mesh.glb"),
             std::string("assets/meshes/growl_1275_mesh.glb"),
         }) {
        MeshData mesh;
        std::string err;
        if (!loadMeshFromCache(growlMeshPath, mesh, &err)) {
            outFail = "loadMeshFromCache should load the Growl sparkle mesh '" + growlMeshPath + "': " + err;
            return false;
        }
        if (mesh.vertices.size() < 4u) {
            outFail = "Growl sparkle mesh should decode at least one textured quad: " + growlMeshPath;
            return false;
        }
        std::vector<glm::vec2> sourceUvs;
        if (!readSourceTexcoord0Uvs(growlMeshPath, sourceUvs, err)) {
            outFail =
                "render_model_cache_contract failed to read source TEXCOORD_0 UVs for '" +
                growlMeshPath + "': " + err;
            return false;
        }
        if (sourceUvs.size() != mesh.vertices.size()) {
            outFail =
                "Growl sparkle mesh cached vertex count should match source TEXCOORD_0 count: " +
                growlMeshPath;
            return false;
        }
        auto approx = [](float a, float b) { return std::fabs(a - b) <= 0.001f; };
        for (std::size_t i = 0; i < sourceUvs.size(); ++i) {
            const glm::vec2& expected = sourceUvs[i];
            const glm::vec2& actual = mesh.vertices[i].uv;
            if (!approx(actual.x, expected.x) || !approx(actual.y, expected.y)) {
                outFail =
                    "Growl sparkle mesh should preserve authored TEXCOORD_0 UVs in cache instead of drifting to generated planar UVs: " +
                    growlMeshPath;
                return false;
            }
        }
    }

    return true;
}
