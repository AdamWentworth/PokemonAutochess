#include "PhlosionNativeModelIr.h"

#include "engine/render/ModelAnimationTypes.h"

#include <nlohmann/json.hpp>
#include <stb_image.h>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace {

namespace fs = std::filesystem;
using game::runtime::render_model::CachedTextureRgba;
using game::runtime::render_model::ContinuousMaterialAnimationTrack;
using game::runtime::render_model::MaterialAnimationKey;
using game::runtime::render_model::MaterialAnimationParameter;
using game::runtime::render_model::MeshData;
using game::runtime::render_model::MeshVertex;
using nlohmann::json;

float gameFreakNativeVToRuntime(float value) {
    // Trinity Pokemon meshes can place material islands in vertically
    // stacked integer UV tiles. The extracted PNG rows are top-down while
    // the native UV within each tile is bottom-up, so flip the fractional
    // position inside its own tile instead of applying a global `1 - v`.
    // A global flip fixes tile zero (Bulbasaur body_a) but sends tile one
    // (body_b: bulb, mouth, tongue, teeth, claws, and vines) negative.
    return std::ceil(value) - value;
}

bool nativeNegativeUnitEyeTile(
    const json& material,
    const std::vector<float>& texcoords) {
    if (material.value("shader_family", std::string{}) != "EyeClearCoat" ||
        texcoords.empty()) {
        return false;
    }
    bool hasNegativeU = false;
    for (std::size_t index = 0u; index + 1u < texcoords.size(); index += 2u) {
        const float u = texcoords[index];
        if (!std::isfinite(u) || u < -1.001f || u > 0.001f) {
            return false;
        }
        hasNegativeU = hasNegativeU || u < -0.001f;
    }
    return hasNegativeU;
}

float gameFreakNegativeUnitUToRuntime(float value) {
    // A small set of Scarlet/Violet EyeClearCoat meshes stores its complete
    // eye island in the signed tile immediately left of the ordinary 0..1
    // texture domain. Exeggcute uses that layout for all six pupil shells.
    // The native character shader resolves the signed tile before its clamp
    // sampler, while an ordinary runtime sampler clamps every negative U to
    // the left edge and drops the pupils. Fold only a fully negative unit
    // eye island into tile zero; mixed/animated UV layouts stay untouched.
    return value - std::floor(value);
}

glm::vec3 gameFreakNativeTangentToRuntime(
    const glm::vec3& tangent) {
    // Trinity's native tangent payload is not laid out in the same basis as
    // the runtime/glTF tangent attribute. The same-source Golduck Blender/GLB
    // export maps native (x, y, z) to (x, z, -y) after its UV-origin bridge;
    // all five of that model's material primitives confirm the mapping.
    return glm::vec3(tangent.x, tangent.z, -tangent.y);
}

constexpr std::string_view kSchema =
    "phlosion-native-model-ir-v1";
constexpr std::size_t kMaxPayloadBytes = 512u * 1024u * 1024u;
constexpr std::size_t kMaxVertices = 2'000'000u;
constexpr std::size_t kMaxIndices = 6'000'000u;
constexpr std::size_t kMaxBones = 4096u;
constexpr std::size_t kMaxAnimations = 1024u;
constexpr std::size_t kMaxSubmeshes =
    std::numeric_limits<std::uint16_t>::max();

bool fail(std::string* outError, std::string message) {
    if (outError) *outError = std::move(message);
    return false;
}

bool readBytes(
    const fs::path& path,
    std::vector<std::uint8_t>& out,
    std::string* outError) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) return fail(outError, "Could not open " + path.string());
    const std::streamoff length = input.tellg();
    if (length < 0 ||
        static_cast<std::uint64_t>(length) > kMaxPayloadBytes) {
        return fail(outError, "Native model IR payload size is invalid.");
    }
    out.resize(static_cast<std::size_t>(length));
    input.seekg(0, std::ios::beg);
    if (!out.empty()) {
        input.read(
            reinterpret_cast<char*>(out.data()),
            static_cast<std::streamsize>(out.size()));
    }
    return input
        ? true
        : fail(outError, "Could not read " + path.string());
}

bool readJson(
    const fs::path& path,
    json& out,
    std::string* outError) {
    std::ifstream input(path);
    if (!input) return fail(outError, "Could not open " + path.string());
    try {
        input >> out;
        return true;
    } catch (const std::exception& exception) {
        return fail(
            outError,
            "Could not parse native model IR: " +
                std::string(exception.what()));
    }
}

bool resolveContainedFile(
    const fs::path& root,
    const std::string& relativeValue,
    fs::path& out,
    std::string* outError) {
    const fs::path relative(relativeValue);
    if (relative.empty() || relative.is_absolute()) {
        return fail(
            outError,
            "Native model IR resource path must be relative.");
    }

    std::error_code error;
    const fs::path canonicalRoot = fs::weakly_canonical(root, error);
    if (error) {
        return fail(
            outError,
            "Could not resolve native model IR root " + root.string());
    }
    const fs::path candidate =
        fs::weakly_canonical(canonicalRoot / relative, error);
    if (error) {
        return fail(
            outError,
            "Could not resolve native model IR resource " +
                relative.string());
    }
    const fs::path withinRoot =
        candidate.lexically_relative(canonicalRoot);
    if (withinRoot.empty() ||
        (*withinRoot.begin()).generic_string() == "..") {
        return fail(
            outError,
            "Native model IR resource escapes its model directory.");
    }
    out = candidate;
    return true;
}

struct View {
    std::size_t offset = 0u;
    std::size_t count = 0u;
    std::size_t components = 0u;
    std::size_t bytes = 0u;
    std::string componentType;
};

bool parseView(
    const json& value,
    View& out,
    std::string* outError) {
    try {
        out.offset = value.at("offset_bytes").get<std::size_t>();
        out.count = value.at("element_count").get<std::size_t>();
        out.components = value.at("components").get<std::size_t>();
        out.componentType = value.at("component_type").get<std::string>();
        out.bytes = value.at("byte_length").get<std::size_t>();
        return true;
    } catch (const std::exception& exception) {
        return fail(
            outError,
            "Native model IR buffer view is invalid: " +
                std::string(exception.what()));
    }
}

template <typename T>
bool decodeView(
    const json& value,
    std::string_view expectedType,
    std::size_t expectedComponents,
    const std::vector<std::uint8_t>& payload,
    std::vector<T>& out,
    std::string* outError) {
    View view;
    if (!parseView(value, view, outError)) return false;
    if (view.componentType != expectedType ||
        view.components != expectedComponents) {
        return fail(outError, "Native model IR buffer view type mismatch.");
    }
    if (view.count > std::numeric_limits<std::size_t>::max() /
            (expectedComponents * sizeof(T))) {
        return fail(outError, "Native model IR buffer view overflows.");
    }
    const std::size_t scalarCount = view.count * expectedComponents;
    const std::size_t expectedBytes = scalarCount * sizeof(T);
    if (view.bytes != expectedBytes ||
        view.offset > payload.size() ||
        expectedBytes > payload.size() - view.offset) {
        return fail(outError, "Native model IR buffer view is out of bounds.");
    }
    out.resize(scalarCount);
    if (expectedBytes > 0u) {
        std::memcpy(out.data(), payload.data() + view.offset, expectedBytes);
    }
    return true;
}

glm::vec3 vec3(const json& value) {
    return glm::vec3(
        value.at(0).get<float>(),
        value.at(1).get<float>(),
        value.at(2).get<float>());
}

glm::quat quat(const json& value) {
    return glm::normalize(glm::quat(
        value.at(3).get<float>(),
        value.at(0).get<float>(),
        value.at(1).get<float>(),
        value.at(2).get<float>()));
}

glm::mat4 trs(const engine::render::model_types::NodeTRS& node) {
    return glm::translate(glm::mat4(1.0f), node.t) *
        glm::mat4_cast(glm::normalize(node.r)) *
        glm::scale(glm::mat4(1.0f), node.s);
}

void buildGlobals(MeshData& out) {
    out.bindNodeGlobals.assign(
        out.nodesDefault.size(), glm::mat4(1.0f));
    const auto visit = [&](const auto& self,
                           int nodeIndex,
                           const glm::mat4& parent) -> void {
        if (nodeIndex < 0 ||
            static_cast<std::size_t>(nodeIndex) >=
                out.nodesDefault.size()) {
            return;
        }
        const std::size_t node = static_cast<std::size_t>(nodeIndex);
        glm::mat4 local = trs(out.nodesDefault[node]);
        const int parentNode =
            node < out.nodeParent.size() ? out.nodeParent[node] : -1;
        if (out.nodesDefault[node].segmentScaleCompensate &&
            parentNode >= 0 &&
            static_cast<std::size_t>(parentNode) < out.nodesDefault.size()) {
            const glm::vec3& parentScale =
                out.nodesDefault[static_cast<std::size_t>(parentNode)].s;
            const glm::vec3 inverseParentScale(
                std::abs(parentScale.x) > 1e-8f ? 1.0f / parentScale.x : 1.0f,
                std::abs(parentScale.y) > 1e-8f ? 1.0f / parentScale.y : 1.0f,
                std::abs(parentScale.z) > 1e-8f ? 1.0f / parentScale.z : 1.0f);
            local = glm::scale(glm::mat4(1.0f), inverseParentScale) * local;
        }
        const glm::mat4 global = parent * local;
        out.bindNodeGlobals[static_cast<std::size_t>(nodeIndex)] = global;
        for (const int child :
             out.nodeChildren[static_cast<std::size_t>(nodeIndex)]) {
            self(self, child, global);
        }
    };
    for (const int root : out.sceneRoots) {
        visit(visit, root, glm::mat4(1.0f));
    }
}

bool loadTextureResource(
    const fs::path& root,
    const json& material,
    const std::string& relative,
    CachedTextureRgba& out,
    std::string* outError) {
    out = CachedTextureRgba{};
    const auto texture = std::find_if(
        material.at("textures").begin(),
        material.at("textures").end(),
        [&](const json& record) {
            return record.at("file").get<std::string>() == relative;
        });
    if (texture == material.at("textures").end()) {
        return fail(outError, "Native material texture evidence is missing.");
    }
    fs::path path;
    if (!resolveContainedFile(root, relative, path, outError)) {
        return false;
    }
    int channels = 0;
    unsigned char* pixels = stbi_load(
        path.string().c_str(),
        &out.width,
        &out.height,
        &channels,
        4);
    if (!pixels || out.width <= 0 || out.height <= 0) {
        if (pixels) stbi_image_free(pixels);
        return fail(outError, "Could not decode native IR texture " + path.string());
    }
    const std::size_t byteCount =
        static_cast<std::size_t>(out.width) *
        static_cast<std::size_t>(out.height) * 4u;
    out.rgba.assign(pixels, pixels + byteCount);
    stbi_image_free(pixels);
    out.wrapS = texture->value("wrap_s", 33071);
    out.wrapT = texture->value("wrap_t", 33071);
    out.minF = texture->value("min_filter", 9729);
    out.magF = texture->value("mag_filter", 9729);
    return true;
}

bool loadTexture(
    const fs::path& root,
    const json& material,
    std::string_view translatedField,
    CachedTextureRgba& out,
    std::string* outError) {
    out = CachedTextureRgba{};
    const auto& translation = material.at("runtime_translation");
    const auto found = translation.find(std::string(translatedField));
    if (found == translation.end() || found->is_null()) return true;
    return loadTextureResource(
        root,
        material,
        found->get<std::string>(),
        out,
        outError);
}

bool loadTextureByRole(
    const fs::path& root,
    const json& material,
    std::string_view role,
    CachedTextureRgba& out,
    std::string* outError) {
    out = CachedTextureRgba{};
    const auto texture = std::find_if(
        material.at("textures").begin(),
        material.at("textures").end(),
        [&](const json& record) {
            return record.value("role", std::string{}) == role;
        });
    if (texture == material.at("textures").end()) return true;
    if (!texture->value("decoded", true) ||
        texture->value("file", std::string{}).empty()) {
        return true;
    }
    return loadTextureResource(
        root,
        material,
        texture->at("file").get<std::string>(),
        out,
        outError);
}

bool vec4Parameter(
    const json& material,
    std::string_view name,
    glm::vec4& out) {
    const auto parameters = material.find("vec4_parameters");
    if (parameters == material.end() || !parameters->is_object()) {
        return false;
    }
    const auto found = parameters->find(std::string(name));
    if (found == parameters->end() ||
        !found->is_array() ||
        found->size() != 4u) {
        return false;
    }
    out = glm::vec4(
        found->at(0).get<float>(),
        found->at(1).get<float>(),
        found->at(2).get<float>(),
        found->at(3).get<float>());
    return true;
}

bool floatParameter(
    const json& material,
    std::string_view name,
    float& out) {
    const auto parameters = material.find("float_parameters");
    if (parameters == material.end() || !parameters->is_object()) {
        return false;
    }
    const auto found = parameters->find(std::string(name));
    if (found == parameters->end() || !found->is_number()) {
        return false;
    }
    out = found->get<float>();
    return true;
}

bool hasTextureRole(const json& material, std::string_view role) {
    const auto textures = material.find("textures");
    if (textures == material.end() || !textures->is_array()) return false;
    return std::any_of(
        textures->begin(),
        textures->end(),
        [&](const json& texture) {
            return texture.value("role", std::string{}) == role;
        });
}

bool shaderOptionEnabled(
    const json& material,
    std::string_view name) {
    const auto options = material.find("shader_options");
    if (options == material.end() || !options->is_object()) {
        return false;
    }
    const auto option = options->find(std::string(name));
    if (option == options->end()) return false;
    if (option->is_boolean()) return option->get<bool>();
    const std::string text = option->is_string()
        ? option->get<std::string>()
        : option->dump();
    return text == "1" || text == "true" || text == "True";
}

bool textureRoleSourceEquals(
    const json& material,
    std::string_view role,
    std::string_view source) {
    const auto textures = material.find("textures");
    if (textures == material.end() || !textures->is_array()) return false;
    return std::any_of(
        textures->begin(),
        textures->end(),
        [&](const json& texture) {
            return texture.value("role", std::string{}) == role &&
                   texture.value("source", std::string{}) == source;
        });
}

bool shaderOptionEquals(
    const json& material,
    std::string_view name,
    std::string_view expected) {
    const auto options = material.find("shader_options");
    if (options == material.end() || !options->is_object()) {
        return false;
    }
    const auto option = options->find(std::string(name));
    return option != options->end() && option->is_string() &&
           option->get<std::string>() == expected;
}

bool shaderOptionNumber(
    const json& material,
    std::string_view name,
    float& out) {
    const auto options = material.find("shader_options");
    if (options == material.end() || !options->is_object()) {
        return false;
    }
    const auto option = options->find(std::string(name));
    if (option == options->end()) return false;
    if (option->is_number()) {
        out = option->get<float>();
        return std::isfinite(out);
    }
    if (!option->is_string()) return false;
    try {
        std::size_t parsed = 0u;
        const std::string text = option->get<std::string>();
        out = std::stof(text, &parsed);
        return parsed == text.size() && std::isfinite(out);
    } catch (...) {
        return false;
    }
}

bool nativeGastlyDisplacedSmoke(const json& material) {
    const std::string family =
        material.value("shader_family", std::string{});
    // Z-A routes Gastly's dusk cloud through IkCharacter; Scarlet/Violet
    // routes the same authored base/layer/displacement texture set through
    // NonDirectional. Both materials expose the same continuous
    // UVScaleOffset/UVScaleOffset3 controller and DisplacementHeight
    // contract. Keep the bridge tied to all three exact smoke textures so an
    // unrelated NonDirectional surface cannot inherit animated displacement.
    return (family == "IkCharacter" || family == "NonDirectional") &&
           textureRoleSourceEquals(
               material,
               "BaseColorMap",
               "pm0092_00_00_smoke_alb.bntx") &&
           textureRoleSourceEquals(
               material,
               "LayerMaskMap",
               "pm0092_00_00_smoke_lym.bntx") &&
           textureRoleSourceEquals(
               material,
               "DisplacementMap",
               "pm0092_00_00_smoke_msk.bntx");
}

bool nativeScarletGastlyDisplacedSmoke(const json& material) {
    return material.value("shader_family", std::string{}) ==
               "NonDirectional" &&
           nativeGastlyDisplacedSmoke(material);
}

std::string supplementalScarletRoughnessFilename(const json& material) {
    const auto& translation = material.at("runtime_translation");
    const auto baseTexture = translation.find("base_color_texture");
    if (baseTexture == translation.end() || baseTexture->is_null()) {
        return {};
    }
    const std::string baseFilename = fs::path(
        baseTexture->get<std::string>()).filename().string();
    static const std::unordered_map<std::string, std::string> kByBaseColor = {
        {"pm0130_00_00_body_a_alb_BaseColorMap_844ab3c6657b.png",
         "pm0130_00_00_body_a_rgn_RoughnessMap_cf5e4d94dd47.png"},
        {"pm0130_00_00_body_b_alb_BaseColorMap_aca11a923b90.png",
         "pm0130_00_00_body_b_rgn_RoughnessMap_3d55a141d141.png"},
        {"pm0130_00_00_body_a_alb_BaseColorMap_24daa2b24357.png",
         "pm0130_00_00_body_a_rgn_RoughnessMap_d54057e80a51.png"},
        {"pm0130_00_00_body_b_alb_BaseColorMap_eabef9aaf9c1.png",
         "pm0130_00_00_body_b_rgn_RoughnessMap_e6d63a25ae01.png"},
        {"pm0137_00_00_body_alb_BaseColorMap_02810eb4db3a.png",
         "pm0137_00_00_body_rgn_RoughnessMap_93db797d858c.png"},
    };
    const auto found = kByBaseColor.find(baseFilename);
    return found != kByBaseColor.end() ? found->second : std::string{};
}

bool loadSupplementalScarletRoughness(
    const fs::path& root,
    const json& material,
    CachedTextureRgba& out,
    std::string* outError) {
    out = CachedTextureRgba{};
    const std::string filename =
        supplementalScarletRoughnessFilename(material);
    if (filename.empty()) return true;
    const fs::path path = root / "za_sv_surface_maps" / filename;
    if (!fs::exists(path)) {
        return fail(
            outError,
            "Supplemental Scarlet/Violet roughness texture is missing: " +
                path.string());
    }
    int channels = 0;
    unsigned char* pixels = stbi_load(
        path.string().c_str(),
        &out.width,
        &out.height,
        &channels,
        4);
    if (!pixels || out.width <= 0 || out.height <= 0) {
        if (pixels) stbi_image_free(pixels);
        return fail(
            outError,
            "Could not decode supplemental Scarlet/Violet roughness texture " +
                path.string());
    }
    const std::size_t byteCount =
        static_cast<std::size_t>(out.width) *
        static_cast<std::size_t>(out.height) * 4u;
    out.rgba.assign(pixels, pixels + byteCount);
    stbi_image_free(pixels);
    out.wrapS = 33071;
    out.wrapT = 33071;
    out.minF = 9729;
    out.magF = 9729;
    return true;
}

bool nativeSssEffectDisplaced(const json& material) {
    return material.value("shader_family", std::string{}) ==
               "SSSEffect" &&
           hasTextureRole(material, "LayerMaskMap") &&
           hasTextureRole(material, "DisplacementMap");
}

bool nativeLayeredUnlitDisplaced(const json& material) {
    if ((material.value("shader_family", std::string{}) != "Unlit" &&
         !nativeGastlyDisplacedSmoke(material) &&
         !nativeSssEffectDisplaced(material)) ||
        !hasTextureRole(material, "LayerMaskMap") ||
        !hasTextureRole(material, "DisplacementMap")) {
        return false;
    }
    const auto options = material.find("shader_options");
    if (options == material.end() || !options->is_object()) return false;
    const auto enabled = options->find("EnableDisplacementMap");
    if (enabled == options->end()) return false;
    const std::string value = enabled->is_string()
        ? enabled->get<std::string>()
        : enabled->dump();
    return value == "1" || value == "true" || value == "True";
}

bool nativeEyeClearCoat(const json& material) {
    const std::string family =
        material.value("shader_family", std::string{});
    // Legends: Arceus uses `Eye` for the Pokemon eye shader while Scarlet
    // also has the related `EyeClearCoat` family.  Both need the dedicated
    // eye material path; treating PLA's family as generic PBR drops its
    // layer-5 authored highlight completely.
    return family == "Eye" || family == "EyeClearCoat";
}

glm::vec4 nativeScarletEyeUvToRuntime(const glm::vec4& source) {
    // SV's Eye shader does not use the conventional `uv * scale + offset`
    // interpretation carried by Phlosion's animated-eye material. Its
    // compiled program applies the authored offset in centered UV space:
    //
    //   u' = (u - offset.x - .5) * scale.x + .5
    //   v' = 1 - ((v - offset.y - .5) * scale.y + .5)
    //
    // Mesh import has already changed the native bottom-up V coordinate to
    // the runtime's top-down convention. Collapse the source equations into
    // Phlosion's affine scale/offset form. Porygon relies on this distinction:
    // its neutral (.5, .5, .5, -.5) selector addresses the top-left pupil
    // cell, while the conventional interpretation clamps into a blank edge.
    return glm::vec4(
        source.x,
        source.y,
        0.5f * (1.0f - source.x) - source.x * source.z,
        0.5f * (1.0f - source.y) + source.y * source.w);
}

bool nativePlaMagnetEyeAtlasAlreadyAddressed(
    const json& material) {
    // The PLA Magnemite-family eye meshes already occupy one atlas cell.
    // Their authored (2,4) values describe the atlas layout while z/w select
    // cells; applying x/y again sends the pupil into a blank region. Keep the
    // exception tied to the two exact shared eye textures so unrelated Eye
    // materials retain their source scale.
    if (material.value("shader_family", std::string{}) != "Eye") {
        return false;
    }
    return textureRoleSourceEquals(
               material,
               "BaseColorMap",
               "pm0081_00_00_eye_alb.bntx") ||
           textureRoleSourceEquals(
               material,
               "BaseColorMap",
               "pm0082_00_00_eye_alb.bntx");
}

bool nativePlaTangelaEyeAtlas(const json& material) {
    // Tangela uses the same kind of flat, animated expression atlas as the
    // Magnemite family, but its mesh UVs do not need the Magnemite atlas-scale
    // correction.  Its NormalMap is the source eye shader's projected sphere,
    // not a portable tangent-space detail normal.  Feeding it to ordinary PBR
    // turns the otherwise clean off-white eye into a faceted silver rosette.
    return material.value("shader_family", std::string{}) == "Eye" &&
           textureRoleSourceEquals(
               material,
               "BaseColorMap",
               "pm0114_00_00_eye_alb.bntx");
}

bool nativePlaFlatAnimatedEyeAtlas(const json& material) {
    return nativePlaMagnetEyeAtlasAlreadyAddressed(material) ||
           nativePlaTangelaEyeAtlas(material);
}

bool nativeChanseyJewelBody(const json& material) {
    // Chansey's egg is authored inside the shared SSS body atlas. Scarlet's
    // EnableJewel lobe keeps its neutral, low-roughness pixels softly lit even
    // when the convex egg faces away from the key light. The portable PBR
    // fallback has no SSS/jewel lobe, so qualify the approximation by the exact
    // native body texture rather than changing every SSS Pokemon.
    return material.value("shader_family", std::string{}) == "SSS" &&
           shaderOptionEnabled(material, "EnableJewel") &&
           textureRoleSourceEquals(
               material,
               "BaseColorMap",
               "pm0113_00_00_body_alb.bntx");
}

bool nativeZaStaryuFamilyJewel(const json& material) {
    // Z-A authors the Staryu-family core as a FresnelEffect surface. Its
    // BaseColorMap is intentionally neutral white; the regular/shiny jewel
    // color lives in the material's BaseColor constant. The portable PBR
    // translation cannot reproduce Trinity's local specular-probe refraction,
    // but it must retain that authored tint instead of exporting a white disk.
    if (material.value("shader_family", std::string{}) != "FresnelEffect") {
        return false;
    }
    return textureRoleSourceEquals(
               material,
               "BaseColorMap",
               "pm0120_00_00_body_01_alb_.bntx") ||
           textureRoleSourceEquals(
               material,
               "BaseColorMap",
               "pm0121_00_00_body_b_alb.bntx");
}

bool nativeKangaskhanEye(const json& material) {
    if (material.value("shader_family", std::string{}) != "IkCharacter" ||
        !shaderOptionEnabled(material, "EnableEyeOptions")) {
        return false;
    }
    return textureRoleSourceEquals(
               material,
               "BaseColorMap",
               "pm0115_00_00_eye_a_alb.bntx") ||
           textureRoleSourceEquals(
               material,
               "BaseColorMap",
               "pm0115_00_00_eye_b_alb.bntx");
}

bool nativeIkCharacterEye(const json& material) {
    // Z-A moved many Pokemon eyes from the older dedicated Eye family into
    // IkCharacter while retaining the same authored layer-5 highlight mask.
    // EnableEyeOptions is the source shader's explicit discriminator; gating
    // on it keeps ordinary IkCharacter body materials out of the eye bake.
    return material.value("shader_family", std::string{}) == "IkCharacter" &&
           shaderOptionEnabled(material, "EnableEyeOptions") &&
           hasTextureRole(material, "HighlightMaskMap");
}

bool nativeKangaskhanBabyEye(const json& material) {
    return nativeKangaskhanEye(material) &&
           textureRoleSourceEquals(
               material,
               "BaseColorMap",
               "pm0115_00_00_eye_b_alb.bntx");
}

void normalizePlaMagnetEyeAtlasScale(
    const json& material,
    glm::vec4& scaleOffset) {
    if (!nativePlaMagnetEyeAtlasAlreadyAddressed(material)) return;
    scaleOffset.x = 1.0f;
    scaleOffset.y = 1.0f;
}

bool nativeScarletEyeClearCoat(const json& material) {
    return material.value("shader_family", std::string{}) ==
        "EyeClearCoat";
}

bool nativeScarletClearCoatAccessory(const json& material) {
    if (!nativeScarletEyeClearCoat(material)) return false;
    const std::string materialName =
        material.value("name", std::string{});
    return materialName.find("eye") == std::string::npos;
}

bool nativeGastlyFaceOverlay(const json& material) {
    // Gastly's Z-A face and eye shells sit inside its opaque displaced
    // smoke volume. The source character pass resolves that layered stack in
    // face/eye order; an ordinary depth-tested draw hides both shells behind
    // the smoke. Keep these rules tied to the shared native texture
    // identities so no unrelated IkCharacter geometry is repositioned.
    return material.value("shader_family", std::string{}) ==
               "IkCharacter" &&
           textureRoleSourceEquals(
               material,
               "BaseColorMap",
               "pm0092_00_00_body_alb.bntx");
}

bool nativeGastlyEyeOverlay(const json& material) {
    return material.value("shader_family", std::string{}) ==
               "IkCharacter" &&
           textureRoleSourceEquals(
               material,
               "BaseColorMap",
               "pm0092_00_00_eye_alb.bntx");
}

bool nativeScarletGastlyFaceDepthOverlay(const json& material) {
    // Scarlet preserves ordinary Standard/EyeClearCoat shading for Gastly's
    // inner face shells, but draws those shells ahead of the opaque displaced
    // smoke. Keep that ordering contract separate from Z-A's facial material
    // and tongue-mask bake.
    if (material.value("shader_family", std::string{}) != "Standard") {
        return false;
    }
    return textureRoleSourceEquals(
               material,
               "BaseColorMap",
               "pm0092_00_00_body_alb.bntx") ||
           textureRoleSourceEquals(
               material,
               "BaseColorMap",
               "pm0092_00_00_body_rare_alb.bntx");
}

bool nativeScarletGastlyEyeDepthOverlay(const json& material) {
    return material.value("shader_family", std::string{}) ==
               "EyeClearCoat" &&
           textureRoleSourceEquals(
               material,
               "BaseColorMap",
               "pm0092_00_00_eye_alb.bntx");
}

bool nativeLgpeLayeredColor(const json& material) {
    return material.value("shader_family", std::string{}) ==
               "PokeDefaultShader" &&
           shaderOptionEnabled(material, "Layer1Enable") &&
           hasTextureRole(material, "Col0Tex") &&
           hasTextureRole(material, "LyCol0Tex");
}

bool nativeNumberedEyeUvParameter(const std::string& parameter) {
    constexpr std::string_view kPrefix = "UVScaleOffset";
    if (parameter.size() <= kPrefix.size() ||
        parameter.compare(0u, kPrefix.size(), kPrefix) != 0) {
        return false;
    }
    return std::all_of(
        parameter.begin() +
            static_cast<std::ptrdiff_t>(kPrefix.size()),
        parameter.end(),
        [](unsigned char value) {
            return std::isdigit(value) != 0;
        });
}

std::string nativeClipBoundEyeUvParameter(
    const json& animationRecords,
    const json& material) {
    const std::string materialName =
        material.value("name", std::string{});
    std::string loweredName = materialName;
    std::transform(
        loweredName.begin(),
        loweredName.end(),
        loweredName.begin(),
        [](unsigned char value) {
            return static_cast<char>(std::tolower(value));
        });
    if (loweredName.find("eye") == std::string::npos &&
        !nativeEyeClearCoat(material)) {
        return {};
    }
    std::string numberedFallback;
    for (const auto& animation : animationRecords) {
        const auto parameters = animation.find("material_parameters");
        if (parameters == animation.end() || !parameters->is_array()) {
            continue;
        }
        for (const auto& track : *parameters) {
            if (track.value("material", std::string{}) != materialName) {
                continue;
            }
            const std::string parameter =
                track.value("parameter", std::string{});
            // Some Trinity eye programs bind their animated color atlas to
            // UVScaleOffset1. Prefer the unnumbered base channel whenever it
            // exists, otherwise use the lowest numbered authored channel.
            // UVScaleOffsetNormal is deliberately excluded.
            if (parameter == "UVScaleOffset") {
                return parameter;
            }
            if (nativeNumberedEyeUvParameter(parameter) &&
                (numberedFallback.empty() ||
                 parameter < numberedFallback)) {
                numberedFallback = parameter;
            }
        }
    }
    return numberedFallback;
}

bool nativeSimpleEyeAtlasCoordinate(float value) {
    // Authored eye atlases use compact rational cell coordinates (quarters,
    // halves, thirds, and a few source-specific offsets such as Dodrio's
    // twelfths). Smooth pupil motion instead carries ordinary floating-point
    // curve values. Keep a small tolerance for exporter round-off.
    constexpr float kTolerance = 0.0015f;
    for (int denominator = 1; denominator <= 16; ++denominator) {
        const float scaled = value * static_cast<float>(denominator);
        if (std::abs(scaled - std::round(scaled)) <= kTolerance) {
            return true;
        }
    }
    // Dodrio's EyeB source stores its twelfth-cell origin rounded to three
    // decimals (-0.167, 0.083, 0.333, 0.583). Accept that explicit decimal
    // quantization without widening the rational tolerance for ordinary
    // high-precision pupil curves such as Pidgeotto and Sandshrew.
    const float millesimal = std::round(value * 1000.0f) / 1000.0f;
    if (std::abs(value - millesimal) > 0.0000015f) return false;
    for (int denominator = 1; denominator <= 16; ++denominator) {
        const float denominatorValue =
            static_cast<float>(denominator);
        const float nearest =
            std::round(millesimal * denominatorValue) / denominatorValue;
        if (std::abs(millesimal - nearest) <= kTolerance) {
            return true;
        }
    }
    return false;
}

bool nativeEyeUvTrackSelectsDiscreteAtlasCells(
    const json& sourceTrack) {
    constexpr std::array<const char*, 4u> kComponents{
        "x", "y", "z", "w"};
    constexpr float kNoiseRange = 0.005f;
    constexpr float kClusterTolerance = 0.0015f;
    constexpr std::size_t kMaximumAtlasCoordinates = 16u;
    bool hasMeaningfulChange = false;
    for (const char* component : kComponents) {
        const auto keys = sourceTrack.find(component);
        if (keys == sourceTrack.end() || !keys->is_array() ||
            keys->empty()) {
            continue;
        }
        float minimum = std::numeric_limits<float>::max();
        float maximum = std::numeric_limits<float>::lowest();
        std::vector<float> coordinates;
        coordinates.reserve(keys->size());
        for (const auto& key : *keys) {
            const float value = key.value("value", 0.0f);
            minimum = std::min(minimum, value);
            maximum = std::max(maximum, value);
            coordinates.push_back(value);
        }
        if (maximum - minimum <= kNoiseRange) {
            continue;
        }
        hasMeaningfulChange = true;
        std::vector<float> uniqueCoordinates;
        for (const float value : coordinates) {
            if (!nativeSimpleEyeAtlasCoordinate(value)) return false;
            const bool alreadyPresent = std::any_of(
                uniqueCoordinates.begin(),
                uniqueCoordinates.end(),
                [&](float existing) {
                    return std::abs(existing - value) <=
                        kClusterTolerance;
                });
            if (!alreadyPresent) {
                uniqueCoordinates.push_back(value);
                if (uniqueCoordinates.size() >
                    kMaximumAtlasCoordinates) {
                    return false;
                }
            }
        }
    }
    return hasMeaningfulChange;
}

float nativeLoopResetFrequency(
    const json& keys,
    float durationSeconds,
    bool positiveReset) {
    if (!keys.is_array() || keys.size() < 2u || durationSeconds <= 0.0f) {
        return 0.0f;
    }
    std::size_t resets = 0u;
    float previous = keys.front().value("value", 0.0f);
    for (std::size_t index = 1u; index < keys.size(); ++index) {
        const float value = keys[index].value("value", previous);
        const float delta = value - previous;
        if ((positiveReset && delta > 0.5f) ||
            (!positiveReset && delta < -0.5f)) {
            ++resets;
        }
        previous = value;
    }
    return resets > 0u
        ? static_cast<float>(resets) / durationSeconds
        : 0.0f;
}

bool nativeContinuousMaterialController(const json& animation) {
    if (!animation.value("loop", false)) return false;
    const std::string name =
        animation.value("name", std::string{});
    return name.find("_08201_loop01_loop") != std::string::npos ||
           name.find("_28201_loop01_loop") != std::string::npos;
}

bool nativeKoffingIdleSmokeController(const json& animation) {
    // Koffing's SV controller is a one-second paired-puff cycle: its
    // skeletal tracks expand and carry the two side clouds, and its material
    // tracks advance the matching displacement phase. Unlike Weezing's
    // otherwise equivalent controller, the shipped TRACM marks every smoke
    // mesh hidden. The game still couples this controller to the repeating
    // FLOAT_GAS event. Restore only the missing visibility gate, using the
    // same per-puff windows authored by the family's Weezing controller.
    return nativeContinuousMaterialController(animation) &&
        animation.value("name", std::string{}).find(
            "pm0109_00_00_28201_loop01_loop") != std::string::npos &&
        animation.value("frame_rate", 0u) == 60u &&
        animation.value("frame_count", 0u) == 61u;
}

bool nativeKoffingIdleSmokeVisibility(
    const json& animation,
    std::string_view meshName,
    std::vector<int>& keyFrames,
    std::vector<bool>& values) {
    if (!nativeKoffingIdleSmokeController(animation)) return false;
    const bool firstPuff =
        meshName.find("_smokegeom_b1_") != std::string_view::npos ||
        meshName.find("_smokemask_b1_") != std::string_view::npos;
    const bool secondPuff =
        meshName.find("_smokegeom_b2_") != std::string_view::npos ||
        meshName.find("_smokemask_b2_") != std::string_view::npos;
    if (!firstPuff && !secondPuff) return false;
    keyFrames = firstPuff
        ? std::vector<int>{0, 10, 41}
        : std::vector<int>{0, 12, 44};
    values = {false, true, false};
    return true;
}

bool nativeContinuousVisibilityController(const json& animation) {
    if (!nativeContinuousMaterialController(animation)) return false;
    if (nativeKoffingIdleSmokeController(animation)) return true;
    const auto tracks = animation.find("mesh_visibility");
    if (tracks == animation.end() || !tracks->is_array()) return false;
    for (const auto& track : *tracks) {
        const auto values = track.find("values");
        if (values == track.end() || !values->is_array()) continue;
        bool hasVisible = false;
        bool hasHidden = false;
        for (const auto& value : *values) {
            if (!value.is_boolean()) continue;
            if (value.get<bool>()) {
                hasVisible = true;
            } else {
                hasHidden = true;
            }
        }
        if (hasVisible && hasHidden) return true;
    }
    return false;
}

bool nativeMaterialTrackTargetsSubmesh(
    const json& sourceTrack,
    std::string_view submeshName) {
    const std::string meshName =
        sourceTrack.value("mesh", std::string{});
    // Older/synthetic native IR records can bind a material track only by
    // material name. Retain that behavior when no mesh target is authored.
    if (meshName.empty()) return true;
    if (submeshName == meshName) return true;
    return submeshName.size() > meshName.size() &&
        submeshName.compare(0u, meshName.size(), meshName) == 0 &&
        submeshName[meshName.size()] == ':';
}

glm::vec2 nativeContinuousUvLoopRates(
    const json& animationRecords,
    const json& material,
    std::string_view submeshName) {
    const std::string materialName =
        material.value("name", std::string{});
    for (const auto& animation : animationRecords) {
        // Game Freak's animation controller enables a numbered loop01
        // channel continuously and layers it over the selected body
        // animation. Scarlet uses 08201 for effects such as Charmander's
        // flame, while Z-A uses 28201 for effects such as Gastly's smoke.
        // Treating either as an ordinary mutually-exclusive skeletal clip
        // freezes the effect.
        if (!nativeContinuousMaterialController(animation)) {
            continue;
        }
        const float durationSeconds =
            animation.value("duration_seconds", 0.0f);
        const auto parameters = animation.find("material_parameters");
        if (parameters == animation.end() || !parameters->is_array()) {
            continue;
        }
        float baseLayerHz = 0.0f;
        float displacementHz = 0.0f;
        for (const auto& track : *parameters) {
            if (track.value("material", std::string{}) != materialName ||
                !nativeMaterialTrackTargetsSubmesh(track, submeshName)) {
                continue;
            }
            const std::string parameter =
                track.value("parameter", std::string{});
            if (parameter == "UVScaleOffset") {
                baseLayerHz = nativeLoopResetFrequency(
                    track.value("z", json::array()),
                    durationSeconds,
                    true);
            } else if (parameter == "UVScaleOffset3") {
                displacementHz = std::max(
                    nativeLoopResetFrequency(
                        track.value("z", json::array()),
                        durationSeconds,
                        false),
                    nativeLoopResetFrequency(
                        track.value("w", json::array()),
                        durationSeconds,
                        false));
            }
        }
        if (baseLayerHz > 0.0f || displacementHz > 0.0f) {
            return glm::vec2(baseLayerHz, displacementHz);
        }
    }
    return glm::vec2(0.0f);
}

bool appendNativeContinuousMaterialTracks(
    const json& animationRecords,
    const json& material,
    std::size_t submeshIndex,
    std::string_view submeshName,
    MeshData& out) {
    const std::string materialName =
        material.value("name", std::string{});
    constexpr std::array<const char*, 4u> kComponents{
        "x", "y", "z", "w"};

    for (const auto& animation : animationRecords) {
        if (!nativeContinuousMaterialController(animation)) {
            continue;
        }
        const float durationSec =
            animation.value("duration_seconds", 0.0f);
        const float framesPerSecond =
            animation.value("frame_rate", 0.0f);
        const auto parameters = animation.find("material_parameters");
        if (durationSec <= 0.0f ||
            framesPerSecond <= 0.0f ||
            parameters == animation.end() ||
            !parameters->is_array()) {
            continue;
        }

        bool appended = false;
        bool appendedBaseTransform = false;
        bool appendedDisplacementTransform = false;
        for (const auto& sourceTrack : *parameters) {
            if (sourceTrack.value("material", std::string{}) !=
                    materialName ||
                !nativeMaterialTrackTargetsSubmesh(
                    sourceTrack,
                    submeshName)) {
                continue;
            }
            const std::string parameterName =
                sourceTrack.value("parameter", std::string{});
            MaterialAnimationParameter parameter;
            if (parameterName == "UVScaleOffset") {
                parameter = MaterialAnimationParameter::UvScaleOffset;
            } else if (parameterName == "UVScaleOffset3") {
                parameter = MaterialAnimationParameter::UvScaleOffset3;
            } else {
                continue;
            }

            ContinuousMaterialAnimationTrack track;
            track.submeshIndex = submeshIndex;
            track.parameter = parameter;
            track.durationSec = durationSec;
            track.sourceFrameRate = framesPerSecond;
            track.loop = true;
            (void)vec4Parameter(
                material,
                parameterName,
                track.defaultValue);
            for (std::size_t component = 0u;
                 component < kComponents.size();
                 ++component) {
                const auto keys = sourceTrack.find(kComponents[component]);
                if (keys == sourceTrack.end() || !keys->is_array()) {
                    continue;
                }
                auto& destination = track.components[component].keys;
                destination.reserve(keys->size());
                for (const auto& key : *keys) {
                    const float frame = key.value("frame", 0.0f);
                    const float value = key.value("value", 0.0f);
                    if (!std::isfinite(frame) ||
                        !std::isfinite(value) ||
                        frame < 0.0f) {
                        continue;
                    }
                    destination.push_back(MaterialAnimationKey{
                        frame / framesPerSecond,
                        value});
                }
                std::sort(
                    destination.begin(),
                    destination.end(),
                    [](const MaterialAnimationKey& a,
                       const MaterialAnimationKey& b) {
                        return a.timeSec < b.timeSec;
                    });
            }
            out.continuousMaterialAnimations.push_back(std::move(track));
            appended = true;
            appendedBaseTransform =
                appendedBaseTransform ||
                parameter == MaterialAnimationParameter::UvScaleOffset;
            appendedDisplacementTransform =
                appendedDisplacementTransform ||
                parameter == MaterialAnimationParameter::UvScaleOffset3;
        }
        // Exact playback must not reinterpret the legacy rate payload as a
        // static transform when a material animates only one UV layer. Keep
        // the source material defaults for the missing layer explicitly.
        const auto appendStaticDefault = [&](MaterialAnimationParameter parameter,
                                             const char* parameterName) {
            ContinuousMaterialAnimationTrack track;
            track.submeshIndex = submeshIndex;
            track.parameter = parameter;
            track.durationSec = durationSec;
            track.sourceFrameRate = framesPerSecond;
            track.loop = true;
            (void)vec4Parameter(
                material,
                parameterName,
                track.defaultValue);
            out.continuousMaterialAnimations.push_back(std::move(track));
        };
        if (appended && !appendedBaseTransform) {
            appendStaticDefault(
                MaterialAnimationParameter::UvScaleOffset,
                "UVScaleOffset");
        }
        if (appended && !appendedDisplacementTransform) {
            appendStaticDefault(
                MaterialAnimationParameter::UvScaleOffset3,
                "UVScaleOffset3");
        }
        return appended;
    }
    return false;
}

std::vector<ContinuousMaterialAnimationTrack>
nativeClipBoundMaterialTracks(
    const json& animation,
    const json& materials,
    const json& submeshes,
    const std::vector<std::string>& materialClipBoundEyeUvParameter,
    bool nativeScarletSource,
    std::string* outError) {
    std::vector<ContinuousMaterialAnimationTrack> out;
    const float durationSec =
        animation.value("duration_seconds", 0.0f);
    const float framesPerSecond =
        animation.value("frame_rate", 0.0f);
    const auto parameters = animation.find("material_parameters");
    if (durationSec <= 0.0f ||
        framesPerSecond <= 0.0f ||
        parameters == animation.end() ||
        !parameters->is_array()) {
        return out;
    }

    constexpr std::array<const char*, 4u> kComponents{
        "x", "y", "z", "w"};
    for (const auto& sourceTrack : *parameters) {
        const std::string parameterName =
            sourceTrack.value("parameter", std::string{});
        const std::string materialName =
            sourceTrack.value("material", std::string{});
        std::size_t materialIndex = materials.size();
        for (std::size_t index = 0u; index < materials.size(); ++index) {
            if (materials[index].value("name", std::string{}) ==
                materialName) {
                materialIndex = index;
                break;
            }
        }
        if (materialIndex >= materials.size()) {
            continue;
        }

        const bool clipBoundEye =
            materialIndex < materialClipBoundEyeUvParameter.size() &&
            !materialClipBoundEyeUvParameter[materialIndex].empty() &&
            parameterName ==
                materialClipBoundEyeUvParameter[materialIndex];
        const bool clipBoundSssEffect =
            nativeSssEffectDisplaced(materials[materialIndex]) &&
            (parameterName == "UVScaleOffset" ||
             parameterName == "UVScaleOffset3");
        if (!clipBoundEye && !clipBoundSssEffect) continue;

        ContinuousMaterialAnimationTrack prototype;
        prototype.parameter = parameterName == "UVScaleOffset3"
            ? MaterialAnimationParameter::UvScaleOffset3
            : MaterialAnimationParameter::UvScaleOffset;
        prototype.durationSec = durationSec;
        prototype.sourceFrameRate = framesPerSecond;
        prototype.loop = animation.value("loop", false);
        prototype.sampling =
            clipBoundEye &&
                nativeEyeUvTrackSelectsDiscreteAtlasCells(sourceTrack)
            ? game::runtime::render_model::
                  MaterialAnimationSampling::HoldSourceFrame
            : game::runtime::render_model::
                  MaterialAnimationSampling::Linear;
        if (!vec4Parameter(
                materials[materialIndex],
                parameterName,
                prototype.defaultValue)) {
            (void)vec4Parameter(
                materials[materialIndex],
                "UVScaleOffset",
                prototype.defaultValue);
        }
        const bool normalizePlaMagnetScale = clipBoundEye &&
            nativePlaMagnetEyeAtlasAlreadyAddressed(
                materials[materialIndex]);
        normalizePlaMagnetEyeAtlasScale(
            materials[materialIndex],
            prototype.defaultValue);
        if (clipBoundEye && nativeScarletSource) {
            prototype.defaultValue =
                nativeScarletEyeUvToRuntime(prototype.defaultValue);
        }
        for (std::size_t component = 0u;
             component < kComponents.size();
             ++component) {
            const auto keys =
                sourceTrack.find(kComponents[component]);
            if (keys == sourceTrack.end() || !keys->is_array()) {
                continue;
            }
            auto& destination =
                prototype.components[component].keys;
            destination.reserve(keys->size());
            for (const auto& key : *keys) {
                const float frame = key.value("frame", 0.0f);
                const float value = key.value("value", 0.0f);
                if (!std::isfinite(frame) ||
                    !std::isfinite(value) ||
                    frame < 0.0f) {
                    if (outError) {
                        *outError =
                            "Native material animation key is invalid.";
                    }
                    return {};
                }
                float runtimeValue =
                    normalizePlaMagnetScale && component < 2u
                        ? 1.0f
                        : value;
                if (clipBoundEye && nativeScarletSource &&
                    component >= 2u) {
                    const glm::vec4 sourceDefault = [&] {
                        glm::vec4 result(1.0f, 1.0f, 0.0f, 0.0f);
                        if (!vec4Parameter(
                                materials[materialIndex],
                                parameterName,
                                result)) {
                            (void)vec4Parameter(
                                materials[materialIndex],
                                "UVScaleOffset",
                                result);
                        }
                        return result;
                    }();
                    runtimeValue = component == 2u
                        ? 0.5f * (1.0f - sourceDefault.x) -
                              sourceDefault.x * value
                        : 0.5f * (1.0f - sourceDefault.y) +
                              sourceDefault.y * value;
                }
                destination.push_back(MaterialAnimationKey{
                    frame / framesPerSecond,
                    runtimeValue});
            }
            std::sort(
                destination.begin(),
                destination.end(),
                [](const MaterialAnimationKey& a,
                   const MaterialAnimationKey& b) {
                    return a.timeSec < b.timeSec;
                });
        }

        for (std::size_t submeshIndex = 0u;
             submeshIndex < submeshes.size();
             ++submeshIndex) {
            if (submeshes[submeshIndex]
                    .at("material")
                    .get<std::size_t>() != materialIndex ||
                !nativeMaterialTrackTargetsSubmesh(
                    sourceTrack,
                    submeshes[submeshIndex]
                        .value("name", std::string{}))) {
                continue;
            }
            ContinuousMaterialAnimationTrack track = prototype;
            track.submeshIndex = submeshIndex;
            out.push_back(std::move(track));
        }
    }
    return out;
}

float srgbToLinear(float value) {
    value = glm::clamp(value, 0.0f, 1.0f);
    return value <= 0.04045f
        ? value / 12.92f
        : std::pow((value + 0.055f) / 1.055f, 2.4f);
}

float linearToSrgb(float value) {
    value = glm::clamp(value, 0.0f, 1.0f);
    return value <= 0.0031308f
        ? value * 12.92f
        : 1.055f * std::pow(value, 1.0f / 2.4f) - 0.055f;
}

glm::vec4 sampleTexture(
    const CachedTextureRgba& texture,
    float u,
    float v,
    const glm::vec4& fallback) {
    if (!texture.hasPixels()) return fallback;
    const float x =
        u * static_cast<float>(texture.width) - 0.5f;
    const float y =
        v * static_cast<float>(texture.height) - 0.5f;
    const int x0 = static_cast<int>(std::floor(x));
    const int y0 = static_cast<int>(std::floor(y));
    const float tx = x - static_cast<float>(x0);
    const float ty = y - static_cast<float>(y0);
    const auto wrappedIndex = [](int value, int size, int wrapMode) {
        if (size <= 1) return 0;
        // GL_REPEAT
        if (wrapMode == 10497) {
            const int wrapped = value % size;
            return wrapped < 0 ? wrapped + size : wrapped;
        }
        // GL_MIRRORED_REPEAT. Mirror texel indices rather than clamping the
        // transformed coordinate so filtering across each authored tile edge
        // matches the native sampler.
        if (wrapMode == 33648) {
            const int period = size * 2;
            int wrapped = value % period;
            if (wrapped < 0) wrapped += period;
            return wrapped < size
                ? wrapped
                : period - 1 - wrapped;
        }
        // GL_CLAMP_TO_EDGE and unknown native values conservatively clamp.
        return std::clamp(value, 0, size - 1);
    };
    const auto pixel = [&](int px, int py) {
        px = wrappedIndex(px, texture.width, texture.wrapS);
        py = wrappedIndex(py, texture.height, texture.wrapT);
        const std::size_t offset =
            (static_cast<std::size_t>(py) *
                 static_cast<std::size_t>(texture.width) +
             static_cast<std::size_t>(px)) * 4u;
        return glm::vec4(
            static_cast<float>(texture.rgba[offset + 0u]) / 255.0f,
            static_cast<float>(texture.rgba[offset + 1u]) / 255.0f,
            static_cast<float>(texture.rgba[offset + 2u]) / 255.0f,
            static_cast<float>(texture.rgba[offset + 3u]) / 255.0f);
    };
    const glm::vec4 top = glm::mix(
        pixel(x0, y0),
        pixel(x0 + 1, y0),
        tx);
    const glm::vec4 bottom = glm::mix(
        pixel(x0, y0 + 1),
        pixel(x0 + 1, y0 + 1),
        tx);
    return glm::mix(top, bottom, ty);
}

unsigned char toByte(float value) {
    return static_cast<unsigned char>(std::lround(
        glm::clamp(value, 0.0f, 1.0f) * 255.0f));
}

glm::vec2 transformedMaterialUv(
    const json& material,
    std::string_view parameter,
    float u,
    float v,
    bool preserveSourceAtlas = false) {
    if (preserveSourceAtlas) {
        return glm::vec2(u, v);
    }
    if (parameter == "UVScaleOffset" &&
        nativePlaMagnetEyeAtlasAlreadyAddressed(material)) {
        return glm::vec2(u, v);
    }
    glm::vec4 scaleOffset(1.0f, 1.0f, 0.0f, 0.0f);
    (void)vec4Parameter(
        material,
        std::string(parameter),
        scaleOffset);
    return glm::vec2(
        u * scaleOffset.x + scaleOffset.z,
        v * scaleOffset.y + scaleOffset.w);
}

void setTextureAlphaFromEmission(
    CachedTextureRgba& texture,
    const CachedTextureRgba& emission) {
    if (!texture.hasPixels() || !emission.hasPixels()) return;
    for (int y = 0; y < texture.height; ++y) {
        for (int x = 0; x < texture.width; ++x) {
            const float u =
                (static_cast<float>(x) + 0.5f) /
                static_cast<float>(texture.width);
            const float v =
                (static_cast<float>(y) + 0.5f) /
                static_cast<float>(texture.height);
            const glm::vec3 sample = glm::vec3(sampleTexture(
                emission,
                u,
                v,
                glm::vec4(0.0f)));
            texture.rgba[
                (static_cast<std::size_t>(y) *
                     static_cast<std::size_t>(texture.width) +
                 static_cast<std::size_t>(x)) * 4u + 3u] =
                toByte(std::max(sample.r, std::max(sample.g, sample.b)));
        }
    }
}

void setTextureAlpha(
    CachedTextureRgba& texture,
    float alpha) {
    if (!texture.hasPixels()) return;
    const unsigned char encodedAlpha = toByte(alpha);
    for (std::size_t offset = 3u;
         offset < texture.rgba.size();
         offset += 4u) {
        texture.rgba[offset] = encodedAlpha;
    }
}

bool bakeLgpeLayeredColor(
    const fs::path& root,
    const json& material,
    CachedTextureRgba& baseTexture,
    std::string* outError) {
    if (!nativeLgpeLayeredColor(material) || !baseTexture.hasPixels()) {
        return true;
    }

    CachedTextureRgba layerTexture;
    if (!loadTextureByRole(
            root,
            material,
            "LyCol0Tex",
            layerTexture,
            outError)) {
        return false;
    }
    if (!layerTexture.hasPixels()) return true;

    float baseScaleU = 1.0f;
    float baseScaleV = 1.0f;
    float baseTranslateU = 0.0f;
    float baseTranslateV = 0.0f;
    float baseShiftU = 0.0f;
    float baseShiftV = 0.0f;
    float layerScaleU = 1.0f;
    float layerScaleV = 1.0f;
    float layerTranslateU = 0.0f;
    float layerTranslateV = 0.0f;
    float layerShiftU = 0.0f;
    float layerShiftV = 0.0f;
    (void)floatParameter(material, "ColorUVScaleU", baseScaleU);
    (void)floatParameter(material, "ColorUVScaleV", baseScaleV);
    (void)floatParameter(material, "ColorUVTranslateU", baseTranslateU);
    (void)floatParameter(material, "ColorUVTranslateV", baseTranslateV);
    (void)floatParameter(material, "ColorBaseU", baseShiftU);
    (void)floatParameter(material, "ColorBaseV", baseShiftV);
    (void)floatParameter(material, "Layer1UVScaleU", layerScaleU);
    (void)floatParameter(material, "Layer1UVScaleV", layerScaleV);
    (void)floatParameter(material, "Layer1UVTranslateU", layerTranslateU);
    (void)floatParameter(material, "Layer1UVTranslateV", layerTranslateV);
    (void)floatParameter(material, "Layer1BaseU", layerShiftU);
    (void)floatParameter(material, "Layer1BaseV", layerShiftV);
    if (std::abs(baseScaleU) <= 1e-6f ||
        std::abs(baseScaleV) <= 1e-6f) {
        return fail(
            outError,
            "LGPE layered material has a zero base-color UV scale.");
    }

    // The LGPE exporter stores vertices in the already-transformed Col0Tex
    // coordinate system. Re-express the independent LyCol0Tex transform in
    // that same coordinate system so the iris/expression atlas remains
    // aligned without changing the canonical vertices a second time. Native
    // Layer1Base is applied before Layer1UVScale (Rattata uses 0.25 * 4.0 as
    // one complete mirrored-repeat period), unlike a post-scale atlas shift.
    const float ratioU = layerScaleU / baseScaleU;
    const float ratioV = layerScaleV / baseScaleV;
    const float offsetU =
        layerScaleU * (layerTranslateU + layerShiftU) -
        ratioU * (baseTranslateU + baseShiftU);
    // Stored V coordinates are flipped after the native material transform.
    const float offsetV =
        1.0f - ratioV +
        ratioV * (baseTranslateV + baseShiftV) -
        layerScaleV * (layerTranslateV + layerShiftV);

    CachedTextureRgba baked = baseTexture;
    for (int y = 0; y < baked.height; ++y) {
        for (int x = 0; x < baked.width; ++x) {
            const float u =
                (static_cast<float>(x) + 0.5f) /
                static_cast<float>(baked.width);
            const float v =
                (static_cast<float>(y) + 0.5f) /
                static_cast<float>(baked.height);
            const std::size_t offset =
                (static_cast<std::size_t>(y) *
                     static_cast<std::size_t>(baked.width) +
                 static_cast<std::size_t>(x)) * 4u;
            const glm::vec4 encodedBase(
                static_cast<float>(baseTexture.rgba[offset + 0u]) / 255.0f,
                static_cast<float>(baseTexture.rgba[offset + 1u]) / 255.0f,
                static_cast<float>(baseTexture.rgba[offset + 2u]) / 255.0f,
                static_cast<float>(baseTexture.rgba[offset + 3u]) / 255.0f);
            const glm::vec4 encodedLayer = sampleTexture(
                layerTexture,
                u * ratioU + offsetU,
                v * ratioV + offsetV,
                glm::vec4(1.0f));
            const float baseCoverage =
                glm::clamp(encodedBase.a, 0.0f, 1.0f);
            const glm::vec3 baseLinear(
                srgbToLinear(encodedBase.r),
                srgbToLinear(encodedBase.g),
                srgbToLinear(encodedBase.b));
            const glm::vec3 layerLinear(
                srgbToLinear(encodedLayer.r),
                srgbToLinear(encodedLayer.g),
                srgbToLinear(encodedLayer.b));
            const glm::vec3 color = glm::mix(
                layerLinear,
                baseLinear,
                baseCoverage);
            baked.rgba[offset + 0u] = toByte(linearToSrgb(color.r));
            baked.rgba[offset + 1u] = toByte(linearToSrgb(color.g));
            baked.rgba[offset + 2u] = toByte(linearToSrgb(color.b));
            // PokeDefaultShader consumes Col0Tex alpha as the Layer1 mask; it
            // is not surface transparency. The composed eye/body is opaque.
            baked.rgba[offset + 3u] = 255u;
        }
    }
    baseTexture = std::move(baked);
    return true;
}

void bakeStaticUvTransform(
    const json& material,
    std::string_view parameter,
    CachedTextureRgba& texture) {
    if (!texture.hasPixels()) return;
    glm::vec4 scaleOffset(1.0f, 1.0f, 0.0f, 0.0f);
    if (!vec4Parameter(
            material,
            std::string(parameter),
            scaleOffset) ||
        (std::abs(scaleOffset.x - 1.0f) < 1e-6f &&
         std::abs(scaleOffset.y - 1.0f) < 1e-6f &&
         std::abs(scaleOffset.z) < 1e-6f &&
         std::abs(scaleOffset.w) < 1e-6f)) {
        return;
    }
    const CachedTextureRgba source = texture;
    for (int y = 0; y < texture.height; ++y) {
        for (int x = 0; x < texture.width; ++x) {
            const float u =
                (static_cast<float>(x) + 0.5f) /
                static_cast<float>(texture.width);
            const float v =
                (static_cast<float>(y) + 0.5f) /
                static_cast<float>(texture.height);
            const glm::vec2 sourceUv = transformedMaterialUv(
                material,
                parameter,
                u,
                v);
            const glm::vec4 sample = sampleTexture(
                source,
                sourceUv.x,
                sourceUv.y,
                glm::vec4(1.0f));
            const std::size_t offset =
                (static_cast<std::size_t>(y) *
                     static_cast<std::size_t>(texture.width) +
                 static_cast<std::size_t>(x)) * 4u;
            texture.rgba[offset + 0u] = toByte(sample.r);
            texture.rgba[offset + 1u] = toByte(sample.g);
            texture.rgba[offset + 2u] = toByte(sample.b);
            texture.rgba[offset + 3u] = toByte(sample.a);
        }
    }
}

bool bakeLayeredBaseColor(
    const fs::path& root,
    const json& material,
    CachedTextureRgba& baseTexture,
    bool preserveSourceAtlas,
    float* outHdrScale,
    std::string* outError) {
    const std::string shaderFamily =
        material.value("shader_family", std::string{});
    const bool lerpBaseColorEmission =
        (shaderFamily == "Standard" || shaderFamily == "Unlit") &&
        shaderOptionEnabled(material, "EnableLerpBaseColorEmission");
    glm::vec4 baseUvScaleOffset(1.0f, 1.0f, 0.0f, 0.0f);
    (void)vec4Parameter(
        material,
        "UVScaleOffset",
        baseUvScaleOffset);
    // PLA's Ponyta-family bodies are the qualified Standard materials whose
    // red mask channel is authored base-map coverage instead of literal
    // Layer1. The source keeps their pale coats, while similarly structured
    // materials such as Machamp body_b use red for the blue-gray arm/foot
    // tint. Shader options and UV transforms are not sufficient to distinguish
    // those responses, so preserve exact native texture identities rather than
    // a heuristic that drops another Pokemon's principal body color.
    // Game Freak's Pokemon body shaders keep markings and fine sculpted
    // definition in BaseColorMap, then use the material layers as tints.
    // Replacing the sampled albedo with a flat layer color erases PLA details
    // such as Abra's closed eyelids and Machamp's arm/foot definition. It also
    // turns Pikachu's red cheeks white because its green mask selects a
    // literal white multiplier. Z-A advertises the same operation with
    // BaseColorMultiply; PLA Standard and Scarlet's SSS family imply it.
    const bool multiplyBaseColor =
        shaderFamily == "Standard" || shaderFamily == "SSS" ||
        shaderOptionEnabled(material, "BaseColorMultiply");
    const bool orderedIkCharacterLayers = shaderFamily == "IkCharacter";
    // Scarlet also routes glossy body accessories through EyeClearCoat.
    // Golduck's body_c forehead jewel is the notable case: its red is
    // authored in BaseColorMap while BaseColorLayer3 is neutral white. The
    // native shader treats that white as an identity tint; replacing the
    // atlas sample with it turns the whole jewel white. Keep actual eye
    // materials on their established EyeFinal path, and preserve the atlas
    // only for neutral layers on non-eye clear-coat accessories.
    const bool clearCoatAccessory =
        nativeScarletClearCoatAccessory(material);
    CachedTextureRgba layerMask;
    if (!loadTextureByRole(
            root,
            material,
            "LayerMaskMap",
            layerMask,
            outError)) {
        return false;
    }
    if (!layerMask.hasPixels()) return true;
    const bool redChannelSelectsBaseColor =
        lerpBaseColorEmission && shaderFamily == "Standard" &&
        (textureRoleSourceEquals(
             material,
             "BaseColorMap",
             "pm0077_00_00_body_alb.bntx") ||
         textureRoleSourceEquals(
             material,
             "BaseColorMap",
             "pm0078_00_00_body_a_alb.bntx") ||
         textureRoleSourceEquals(
             material,
             "BaseColorMap",
             "pm0078_00_00_body_b_alb.bntx"));

    std::array<glm::vec4, 4u> layerColors{};
    std::array<bool, 4u> hasLayer{};
    std::array<float, 4u> layerScales{1.0f, 1.0f, 1.0f, 1.0f};
    bool anyLayer = false;
    for (std::size_t layer = 0u; layer < layerColors.size(); ++layer) {
        (void)floatParameter(
            material,
            "LayerMaskScale" + std::to_string(layer + 1u),
            layerScales[layer]);
        hasLayer[layer] = vec4Parameter(
            material,
            "BaseColorLayer" + std::to_string(layer + 1u),
            layerColors[layer]);
        // The PHMODEL exporter has already translated Trinity's W/X/Y/Z
        // storage into semantic X/Y/Z/W order.  BaseColorLayer values are
        // therefore conventional RGBA here; rotating them a second time
        // turns Scarlet's HDR red/orange flame colors yellow-white.
        anyLayer = anyLayer || hasLayer[layer];
    }
    if (!anyLayer) return true;

    float hdrScale = 1.0f;
    if (outHdrScale) {
        for (std::size_t layer = 0u; layer < layerColors.size(); ++layer) {
            if (!hasLayer[layer]) continue;
            hdrScale = std::max(
                hdrScale,
                std::max(
                    layerColors[layer].r,
                    std::max(layerColors[layer].g, layerColors[layer].b)));
        }
        *outHdrScale = hdrScale;
    }

    CachedTextureRgba baked;
    // Layer masks are often deliberately tiny constants (Beedrill's wing
    // selector is 32x32) while BaseColorMap carries high-resolution line
    // art. Never let the selector downsample the authored albedo.
    baked.width = std::max(
        layerMask.width,
        baseTexture.hasPixels() ? baseTexture.width : 0);
    baked.height = std::max(
        layerMask.height,
        baseTexture.hasPixels() ? baseTexture.height : 0);
    baked.wrapS = baseTexture.hasPixels() ? baseTexture.wrapS : layerMask.wrapS;
    baked.wrapT = baseTexture.hasPixels() ? baseTexture.wrapT : layerMask.wrapT;
    baked.minF = baseTexture.hasPixels() ? baseTexture.minF : layerMask.minF;
    baked.magF = baseTexture.hasPixels() ? baseTexture.magF : layerMask.magF;
    const std::size_t pixelCount =
        static_cast<std::size_t>(baked.width) *
        static_cast<std::size_t>(baked.height);
    baked.rgba.resize(pixelCount * 4u);
    for (int y = 0; y < baked.height; ++y) {
        for (int x = 0; x < baked.width; ++x) {
            const float u =
                (static_cast<float>(x) + 0.5f) /
                static_cast<float>(baked.width);
            const float v =
                (static_cast<float>(y) + 0.5f) /
                static_cast<float>(baked.height);
            const glm::vec2 sourceUv = transformedMaterialUv(
                material,
                "UVScaleOffset",
                u,
                v,
                preserveSourceAtlas);
            const glm::vec4 encodedBase = sampleTexture(
                baseTexture,
                sourceUv.x,
                sourceUv.y,
                glm::vec4(1.0f));
            const glm::vec4 mask = sampleTexture(
                layerMask,
                sourceUv.x,
                sourceUv.y,
                glm::vec4(0.0f));
            const glm::vec3 baseColor(
                srgbToLinear(encodedBase.r),
                srgbToLinear(encodedBase.g),
                srgbToLinear(encodedBase.b));
            glm::vec4 baseMultiplier(1.0f);
            if (orderedIkCharacterLayers) {
                (void)vec4Parameter(material, "BaseColor", baseMultiplier);
            }
            const glm::vec3 resolvedBaseColor =
                baseColor * glm::vec3(baseMultiplier);
            float layerWeightSum = 0.0f;
            std::array<float, 4u> layerWeights{};
            for (std::size_t layer = 0u;
                 layer < layerColors.size();
                 ++layer) {
                if (!hasLayer[layer]) continue;
                // The qualified PLA Ponyta-family body sources use red as
                // authored base-color coverage rather than an ordinary
                // Layer1 tint.
                // Other PLA body atlases—including Machamp body_b—and Z-A
                // IkCharacter materials use red as a real Layer1 selector.
                if (redChannelSelectsBaseColor && layer == 0u) continue;
                layerWeights[layer] = glm::clamp(
                    mask[static_cast<glm::length_t>(layer)] *
                        std::max(0.0f, layerScales[layer]),
                    0.0f,
                    1.0f);
                layerWeightSum += layerWeights[layer];
            }
            // Z-A's shipped IkCharacter ColorProcess starts with
            // BaseColorMap * BaseColor, then applies Layer1..4 as ordered
            // lerps using the scaled RGBA mask channels. It does not use the
            // premultiplied-coverage normalization emitted by Scarlet's
            // Unlit variation 48. Sharing that Scarlet path washed partial
            // mask transitions into pale facial patches (most visibly on
            // Machop). Keep the two source programs distinct.
            float coverage = orderedIkCharacterLayers
                ? 1.0f
                : glm::clamp(1.0f - layerWeightSum, 0.0f, 1.0f);
            glm::vec3 color = resolvedBaseColor * coverage;
            for (std::size_t layer = 0u;
                 layer < layerColors.size();
                 ++layer) {
                if (!hasLayer[layer]) continue;
                glm::vec3 resolvedLayerColor(
                    layerColors[layer]);
                const bool neutralAccessoryTint =
                    clearCoatAccessory &&
                    layerColors[layer].r >= 1.0f - 1e-6f &&
                    layerColors[layer].g >= 1.0f - 1e-6f &&
                    layerColors[layer].b >= 1.0f - 1e-6f;
                if ((multiplyBaseColor || neutralAccessoryTint) &&
                    baseTexture.hasPixels()) {
                    // PLA Standard and Z-A IkCharacter materials retain line
                    // work, subtle shading, and other authored detail in
                    // BaseColorMap. Their material-layer colors tint that map;
                    // replacing it with a flat layer color erases Abra's
                    // eyelids, Machamp's limb detail, and Beedrill's wing
                    // veins. Neutral EyeClearCoat accessory layers likewise
                    // retain source color such as Golduck's forehead jewel.
                    resolvedLayerColor *= baseColor;
                }
                color = glm::mix(
                    color,
                    resolvedLayerColor,
                    layerWeights[layer]);
                if (!orderedIkCharacterLayers) {
                    coverage += layerWeights[layer] * (1.0f - coverage);
                }
            }
            if (!orderedIkCharacterLayers) {
                color /= std::max(coverage, 1e-6f);
            }
            color /= hdrScale;
            const std::size_t offset =
                (static_cast<std::size_t>(y) *
                     static_cast<std::size_t>(baked.width) +
                 static_cast<std::size_t>(x)) * 4u;
            baked.rgba[offset + 0u] = toByte(linearToSrgb(color.r));
            baked.rgba[offset + 1u] = toByte(linearToSrgb(color.g));
            baked.rgba[offset + 2u] = toByte(linearToSrgb(color.b));
            baked.rgba[offset + 3u] = toByte(encodedBase.a);
        }
    }
    baseTexture = std::move(baked);
    return true;
}

bool bakeNativeGastlyFaceAuxiliary(
    const fs::path& root,
    const json& material,
    const CachedTextureRgba& baseTexture,
    CachedTextureRgba& shadowSpecTexture,
    CachedTextureRgba& rimMaskTexture,
    std::string* outError) {
    CachedTextureRgba layerMask;
    CachedTextureRgba specularMask;
    CachedTextureRgba rimMask;
    if (!loadTextureByRole(
            root,
            material,
            "LayerMaskMap",
            layerMask,
            outError) ||
        !loadTextureByRole(
            root,
            material,
            "SpecularMaskMap",
            specularMask,
            outError) ||
        !loadTextureByRole(
            root,
            material,
            "RimLightMaskMap",
            rimMask,
            outError)) {
        return false;
    }
    if (!layerMask.hasPixels() || !rimMask.hasPixels()) return true;

    glm::vec4 baseShadow(0.0f);
    (void)vec4Parameter(material, "ShadowingColor", baseShadow);
    float baseSpecular = 0.0f;
    (void)floatParameter(material, "SpecularIntensity", baseSpecular);
    std::array<glm::vec4, 4u> layerShadows{};
    std::array<float, 4u> layerSpecular{};
    std::array<float, 4u> layerScales{1.0f, 1.0f, 1.0f, 1.0f};
    for (std::size_t layer = 0u; layer < layerShadows.size(); ++layer) {
        layerShadows[layer] = baseShadow;
        layerSpecular[layer] = baseSpecular;
        const std::string suffix = std::to_string(layer + 1u);
        (void)vec4Parameter(
            material,
            "ShadowingColorLayer" + suffix,
            layerShadows[layer]);
        (void)floatParameter(
            material,
            "SpecularLayer" + suffix + "Intensity",
            layerSpecular[layer]);
        (void)floatParameter(
            material,
            "LayerMaskScale" + suffix,
            layerScales[layer]);
    }

    const int width = std::max({
        layerMask.width,
        specularMask.hasPixels() ? specularMask.width : 0,
        rimMask.width});
    const int height = std::max({
        layerMask.height,
        specularMask.hasPixels() ? specularMask.height : 0,
        rimMask.height});
    CachedTextureRgba auxiliary;
    CachedTextureRgba resolvedRim;
    auxiliary.width = resolvedRim.width = width;
    auxiliary.height = resolvedRim.height = height;
    auxiliary.wrapS = resolvedRim.wrapS = layerMask.wrapS;
    auxiliary.wrapT = resolvedRim.wrapT = layerMask.wrapT;
    auxiliary.minF = resolvedRim.minF = layerMask.minF;
    auxiliary.magF = resolvedRim.magF = layerMask.magF;
    const std::size_t pixelCount =
        static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    auxiliary.rgba.resize(pixelCount * 4u);
    resolvedRim.rgba.resize(pixelCount * 4u);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const float u =
                (static_cast<float>(x) + 0.5f) /
                static_cast<float>(width);
            const float v =
                (static_cast<float>(y) + 0.5f) /
                static_cast<float>(height);
            const glm::vec2 sourceUv = transformedMaterialUv(
                material,
                "UVScaleOffset",
                u,
                v);
            const glm::vec4 mask = sampleTexture(
                layerMask,
                sourceUv.x,
                sourceUv.y,
                glm::vec4(0.0f));
            glm::vec3 shadow(baseShadow);
            float specular = baseSpecular;
            for (std::size_t layer = 0u;
                 layer < layerShadows.size();
                 ++layer) {
                const float weight = glm::clamp(
                    mask[static_cast<glm::length_t>(layer)] *
                        std::max(0.0f, layerScales[layer]),
                    0.0f,
                    1.0f);
                shadow = glm::mix(
                    shadow,
                    glm::vec3(layerShadows[layer]),
                    weight);
                specular = glm::mix(
                    specular,
                    layerSpecular[layer],
                    weight);
            }
            const float specularCoverage = sampleTexture(
                specularMask,
                sourceUv.x,
                sourceUv.y,
                glm::vec4(1.0f)).r;
            // Pack a source-qualified tongue marker into the otherwise spare
            // upper half of the specular channel. Gastly's baked Z-A body
            // albedo cleanly separates the peach tongue from the purple body,
            // dark-red mouth, and white teeth. Keeping this in the auxiliary
            // map also preserves the marker when lower graphics tiers drop
            // the optional normal, AO, and rim maps.
            const glm::vec3 bakedBase = glm::vec3(sampleTexture(
                baseTexture,
                u,
                v,
                glm::vec4(0.0f)));
            const float redDominance = glm::clamp(
                (bakedBase.r - std::max(bakedBase.g, bakedBase.b) - 0.12f) /
                    0.20f,
                0.0f,
                1.0f);
            const float tongueColorFloor = glm::smoothstep(
                0.26f,
                0.40f,
                std::min(bakedBase.g, bakedBase.b));
            const float tongueWhiteRejection = 1.0f - glm::smoothstep(
                0.78f,
                0.92f,
                std::min(bakedBase.g, bakedBase.b));
            const float tongueMask =
                redDominance * tongueColorFloor * tongueWhiteRejection;
            const float rim = sampleTexture(
                rimMask,
                sourceUv.x,
                sourceUv.y,
                glm::vec4(1.0f)).r;
            const std::size_t offset =
                (static_cast<std::size_t>(y) *
                     static_cast<std::size_t>(width) +
                 static_cast<std::size_t>(x)) * 4u;
            auxiliary.rgba[offset + 0u] =
                toByte(glm::clamp(shadow.r, 0.0f, 1.0f));
            auxiliary.rgba[offset + 1u] =
                toByte(glm::clamp(shadow.g, 0.0f, 1.0f));
            auxiliary.rgba[offset + 2u] =
                toByte(glm::clamp(shadow.b, 0.0f, 1.0f));
            const float sourceSpecular = glm::clamp(
                specularCoverage * std::max(0.0f, specular),
                0.0f,
                0.45f);
            auxiliary.rgba[offset + 3u] = toByte(
                sourceSpecular + 0.5f * tongueMask);
            const std::uint8_t rimByte =
                toByte(glm::clamp(rim, 0.0f, 1.0f));
            resolvedRim.rgba[offset + 0u] = rimByte;
            resolvedRim.rgba[offset + 1u] = rimByte;
            resolvedRim.rgba[offset + 2u] = rimByte;
            resolvedRim.rgba[offset + 3u] = 255u;
        }
    }
    shadowSpecTexture = std::move(auxiliary);
    rimMaskTexture = std::move(resolvedRim);
    return true;
}

bool bakeEyeHighlightEmission(
    const fs::path& root,
    const json& material,
    CachedTextureRgba& emissiveTexture,
    bool& outBaked,
    std::string* outError) {
    CachedTextureRgba highlightMask;
    if (!loadTextureByRole(
            root,
            material,
            "HighlightMaskMap",
            highlightMask,
            outError)) {
        return false;
    }
    if (!shaderOptionEnabled(material, "EnableHighlight")) {
        return true;
    }

    glm::vec4 highlightColor(1.0f);
    float highlightIntensity = 0.0f;
    if (!vec4Parameter(
            material,
            "EmissionColorLayer5",
            highlightColor) ||
        !floatParameter(
            material,
            "EmissionIntensityLayer5",
            highlightIntensity) ||
        highlightIntensity <= 0.0f) {
        return true;
    }

    // PLA exposes a literal layer-5 mask. Scarlet/Violet's NormalMap1 is a
    // different representation and is resolved into EyeFinal color by
    // bakeScarletEyeFinalColor below; treating it as a specular normal here
    // produces the familiar pinprick/crescent eye artifacts.
    if (!highlightMask.hasPixels()) return true;

    // HighlightMaskMap is layer 5 of the eye shader, not a replacement for
    // the LayerMaskMap-driven emission in layers 1-4. Preserve the already
    // baked iris response and add the authored glint on top.
    const CachedTextureRgba& highlightSource = highlightMask;
    CachedTextureRgba baked;
    baked.width = std::max(
        highlightSource.width,
        emissiveTexture.hasPixels() ? emissiveTexture.width : 0);
    baked.height = std::max(
        highlightSource.height,
        emissiveTexture.hasPixels() ? emissiveTexture.height : 0);
    baked.wrapS = emissiveTexture.hasPixels()
        ? emissiveTexture.wrapS
        : highlightSource.wrapS;
    baked.wrapT = emissiveTexture.hasPixels()
        ? emissiveTexture.wrapT
        : highlightSource.wrapT;
    baked.minF = emissiveTexture.hasPixels()
        ? emissiveTexture.minF
        : highlightSource.minF;
    baked.magF = emissiveTexture.hasPixels()
        ? emissiveTexture.magF
        : highlightSource.magF;
    baked.rgba.assign(
        static_cast<std::size_t>(baked.width) *
            static_cast<std::size_t>(baked.height) * 4u,
        255u);
    for (int y = 0; y < baked.height; ++y) {
        for (int x = 0; x < baked.width; ++x) {
            const std::size_t offset =
                (static_cast<std::size_t>(y) *
                     static_cast<std::size_t>(baked.width) +
                 static_cast<std::size_t>(x)) * 4u;
            const float u =
                (static_cast<float>(x) + 0.5f) /
                static_cast<float>(baked.width);
            const float v =
                (static_cast<float>(y) + 0.5f) /
                static_cast<float>(baked.height);
            const glm::vec4 encodedPrevious = sampleTexture(
                emissiveTexture,
                u,
                v,
                glm::vec4(0.0f));
            const glm::vec3 previous(
                srgbToLinear(encodedPrevious.r),
                srgbToLinear(encodedPrevious.g),
                srgbToLinear(encodedPrevious.b));
            const glm::vec2 highlightUv = transformedMaterialUv(
                material,
                "UVScaleOffset1",
                u,
                v,
                false);
            const float weight = sampleTexture(
                highlightMask,
                highlightUv.x,
                highlightUv.y,
                glm::vec4(0.0f)).r;
            const glm::vec3 emission = glm::clamp(
                previous +
                    glm::max(glm::vec3(highlightColor), glm::vec3(0.0f)) *
                        std::max(highlightIntensity, 0.0f) * weight,
                glm::vec3(0.0f),
                glm::vec3(1.0f));
            baked.rgba[offset + 0u] =
                toByte(linearToSrgb(emission.r));
            baked.rgba[offset + 1u] =
                toByte(linearToSrgb(emission.g));
            baked.rgba[offset + 2u] =
                toByte(linearToSrgb(emission.b));
        }
    }
    emissiveTexture = std::move(baked);
    outBaked = true;
    return true;
}

bool bakeNativeChanseyJewelEmission(
    const CachedTextureRgba& baseTexture,
    const CachedTextureRgba& metallicRoughnessTexture,
    CachedTextureRgba& emissiveTexture,
    bool& outBaked) {
    if (!baseTexture.hasPixels() ||
        !metallicRoughnessTexture.hasPixels()) {
        return true;
    }

    CachedTextureRgba baked;
    baked.width = baseTexture.width;
    baked.height = baseTexture.height;
    baked.wrapS = baseTexture.wrapS;
    baked.wrapT = baseTexture.wrapT;
    baked.minF = baseTexture.minF;
    baked.magF = baseTexture.magF;
    baked.rgba.assign(
        static_cast<std::size_t>(baked.width) *
            static_cast<std::size_t>(baked.height) * 4u,
        0u);
    bool hasJewelCoverage = false;
    for (int y = 0; y < baked.height; ++y) {
        for (int x = 0; x < baked.width; ++x) {
            const float u =
                (static_cast<float>(x) + 0.5f) /
                static_cast<float>(baked.width);
            const float v =
                (static_cast<float>(y) + 0.5f) /
                static_cast<float>(baked.height);
            const glm::vec4 encodedBase = sampleTexture(
                baseTexture,
                u,
                v,
                glm::vec4(0.0f));
            const float minimumChannel = std::min({
                encodedBase.r,
                encodedBase.g,
                encodedBase.b});
            const float maximumChannel = std::max({
                encodedBase.r,
                encodedBase.g,
                encodedBase.b});
            const float paleCoverage = glm::smoothstep(
                0.72f,
                0.86f,
                minimumChannel);
            const float neutralCoverage = 1.0f - glm::smoothstep(
                0.045f,
                0.14f,
                maximumChannel - minimumChannel);
            const float roughness = sampleTexture(
                metallicRoughnessTexture,
                u,
                v,
                glm::vec4(1.0f)).g;
            const float glossyCoverage = 1.0f - glm::smoothstep(
                0.36f,
                0.68f,
                roughness);
            const float coverage = glm::clamp(
                paleCoverage * neutralCoverage * glossyCoverage,
                0.0f,
                1.0f);
            hasJewelCoverage = hasJewelCoverage || coverage > 0.001f;
            const glm::vec3 baseLinear(
                srgbToLinear(encodedBase.r),
                srgbToLinear(encodedBase.g),
                srgbToLinear(encodedBase.b));
            // This is the missing low-frequency jewel/SSS response only. The
            // ordinary PBR lobe remains live and supplies the source's glossy
            // highlight and normal detail on top.
            const glm::vec3 emission = glm::clamp(
                baseLinear * (0.22f * coverage),
                glm::vec3(0.0f),
                glm::vec3(1.0f));
            const std::size_t offset =
                (static_cast<std::size_t>(y) *
                     static_cast<std::size_t>(baked.width) +
                 static_cast<std::size_t>(x)) * 4u;
            baked.rgba[offset + 0u] =
                toByte(linearToSrgb(emission.r));
            baked.rgba[offset + 1u] =
                toByte(linearToSrgb(emission.g));
            baked.rgba[offset + 2u] =
                toByte(linearToSrgb(emission.b));
            baked.rgba[offset + 3u] = 255u;
        }
    }
    if (!hasJewelCoverage) return true;
    emissiveTexture = std::move(baked);
    outBaked = true;
    return true;
}

bool bakeIkCharacterEyePackedInputs(
    const fs::path& root,
    const json& material,
    CachedTextureRgba& normalTexture,
    CachedTextureRgba& emissiveTexture,
    std::string* outError) {
    CachedTextureRgba parallaxMap;
    CachedTextureRgba eyelidShadowMap;
    if (!loadTextureByRole(
            root,
            material,
            "ParallaxMap",
            parallaxMap,
            outError) ||
        !loadTextureByRole(
            root,
            material,
            "EyelidShadowMaskMap",
            eyelidShadowMap,
            outError)) {
        return false;
    }
    if (!parallaxMap.hasPixels()) {
        return fail(
            outError,
            "Native Z-A IkCharacter eye is missing its ParallaxMap pixels.");
    }

    const auto packAlpha = [&material](
                               CachedTextureRgba& carrier,
                               const CachedTextureRgba& source,
                               std::string_view uvParameter,
                               const glm::vec4& carrierFallback,
                               float sourceFallback) {
        CachedTextureRgba packed;
        packed.width = std::max(
            carrier.hasPixels() ? carrier.width : 0,
            source.hasPixels() ? source.width : 0);
        packed.height = std::max(
            carrier.hasPixels() ? carrier.height : 0,
            source.hasPixels() ? source.height : 0);
        if (packed.width <= 0 || packed.height <= 0) return;
        const CachedTextureRgba& samplerState = carrier.hasPixels()
            ? carrier
            : source;
        packed.wrapS = samplerState.wrapS;
        packed.wrapT = samplerState.wrapT;
        packed.minF = samplerState.minF;
        packed.magF = samplerState.magF;
        packed.rgba.resize(
            static_cast<std::size_t>(packed.width) *
            static_cast<std::size_t>(packed.height) * 4u);
        for (int y = 0; y < packed.height; ++y) {
            for (int x = 0; x < packed.width; ++x) {
                const float u =
                    (static_cast<float>(x) + 0.5f) /
                    static_cast<float>(packed.width);
                const float v =
                    (static_cast<float>(y) + 0.5f) /
                    static_cast<float>(packed.height);
                const glm::vec4 previous = sampleTexture(
                    carrier,
                    u,
                    v,
                    carrierFallback);
                const glm::vec2 sourceUv = transformedMaterialUv(
                    material,
                    uvParameter,
                    u,
                    v,
                    false);
                const float alpha = sampleTexture(
                    source,
                    sourceUv.x,
                    sourceUv.y,
                    glm::vec4(sourceFallback)).r;
                const std::size_t offset =
                    (static_cast<std::size_t>(y) *
                         static_cast<std::size_t>(packed.width) +
                     static_cast<std::size_t>(x)) * 4u;
                packed.rgba[offset + 0u] = toByte(previous.r);
                packed.rgba[offset + 1u] = toByte(previous.g);
                packed.rgba[offset + 2u] = toByte(previous.b);
                packed.rgba[offset + 3u] = toByte(alpha);
            }
        }
        carrier = std::move(packed);
    };

    // The runtime material ABI has six texture slots. Normal alpha is unused
    // by tangent-space decoding and therefore losslessly carries the eyelid
    // mask. Emissive RGB already carries the authored layer-5 highlight, while
    // its alpha carries the parallax height map. Both are sampled live after
    // the view-dependent eye UV is resolved by mode 35.
    packAlpha(
        normalTexture,
        eyelidShadowMap,
        "UVScaleOffset2",
        glm::vec4(0.5f, 0.5f, 1.0f, 0.0f),
        0.0f);
    packAlpha(
        emissiveTexture,
        parallaxMap,
        "UVScaleOffset",
        glm::vec4(0.0f),
        0.0f);
    return true;
}

void bakeNativeKangaskhanBabyEyeBase(
    const json& material,
    CachedTextureRgba& baseTexture) {
    if (!baseTexture.hasPixels()) return;
    glm::vec4 eyeColor(1.0f);
    if (!vec4Parameter(material, "BaseColorLayer1", eyeColor)) return;
    const glm::vec3 tint = glm::max(glm::vec3(eyeColor), glm::vec3(0.0f));
    for (std::size_t offset = 0u;
         offset + 3u < baseTexture.rgba.size();
         offset += 4u) {
        const glm::vec3 baseLinear(
            srgbToLinear(
                static_cast<float>(baseTexture.rgba[offset + 0u]) / 255.0f),
            srgbToLinear(
                static_cast<float>(baseTexture.rgba[offset + 1u]) / 255.0f),
            srgbToLinear(
                static_cast<float>(baseTexture.rgba[offset + 2u]) / 255.0f));
        const glm::vec3 resolved = glm::clamp(
            baseLinear * tint,
            glm::vec3(0.0f),
            glm::vec3(1.0f));
        baseTexture.rgba[offset + 0u] =
            toByte(linearToSrgb(resolved.r));
        baseTexture.rgba[offset + 1u] =
            toByte(linearToSrgb(resolved.g));
        baseTexture.rgba[offset + 2u] =
            toByte(linearToSrgb(resolved.b));
        baseTexture.rgba[offset + 3u] = 255u;
    }
}

void bakeNativeZaStaryuFamilyJewelBase(
    const json& material,
    CachedTextureRgba& baseTexture) {
    if (!baseTexture.hasPixels()) return;
    glm::vec4 baseColor(1.0f);
    if (!vec4Parameter(material, "BaseColor", baseColor)) return;
    const glm::vec3 tint = glm::max(glm::vec3(baseColor), glm::vec3(0.0f));
    for (std::size_t offset = 0u;
         offset + 3u < baseTexture.rgba.size();
         offset += 4u) {
        const glm::vec3 encodedBase(
            static_cast<float>(baseTexture.rgba[offset + 0u]) / 255.0f,
            static_cast<float>(baseTexture.rgba[offset + 1u]) / 255.0f,
            static_cast<float>(baseTexture.rgba[offset + 2u]) / 255.0f);
        const glm::vec3 baseLinear(
            srgbToLinear(encodedBase.r),
            srgbToLinear(encodedBase.g),
            srgbToLinear(encodedBase.b));
        const glm::vec3 resolved = glm::clamp(
            baseLinear * tint,
            glm::vec3(0.0f),
            glm::vec3(1.0f));
        baseTexture.rgba[offset + 0u] =
            toByte(linearToSrgb(resolved.r));
        baseTexture.rgba[offset + 1u] =
            toByte(linearToSrgb(resolved.g));
        baseTexture.rgba[offset + 2u] =
            toByte(linearToSrgb(resolved.b));
    }
}

bool nativeScarletEyeHighlightRadii(
    const CachedTextureRgba& highlightNormal,
    glm::vec3& outBackground,
    glm::vec2& outRadii) {
    if (!highlightNormal.hasPixels()) return false;

    const auto sourcePixel = [&](int x, int y) {
        const std::size_t offset =
            (static_cast<std::size_t>(y) *
                 static_cast<std::size_t>(highlightNormal.width) +
             static_cast<std::size_t>(x)) * 4u;
        return glm::vec3(
            static_cast<float>(highlightNormal.rgba[offset + 0u]) / 255.0f,
            static_cast<float>(highlightNormal.rgba[offset + 1u]) / 255.0f,
            static_cast<float>(highlightNormal.rgba[offset + 2u]) / 255.0f);
    };
    outBackground =
        (sourcePixel(0, 0) +
         sourcePixel(highlightNormal.width - 1, 0) +
         sourcePixel(0, highlightNormal.height - 1) +
         sourcePixel(
             highlightNormal.width - 1,
             highlightNormal.height - 1)) * 0.25f;

    constexpr float kNormalSupportThreshold = 8.0f / 255.0f;
    int minimumX = highlightNormal.width;
    int minimumY = highlightNormal.height;
    int maximumX = -1;
    int maximumY = -1;
    for (int y = 0; y < highlightNormal.height; ++y) {
        for (int x = 0; x < highlightNormal.width; ++x) {
            const glm::vec3 delta = glm::abs(
                sourcePixel(x, y) - outBackground);
            if (std::max(delta.r, std::max(delta.g, delta.b)) <=
                kNormalSupportThreshold) {
                continue;
            }
            minimumX = std::min(minimumX, x);
            minimumY = std::min(minimumY, y);
            maximumX = std::max(maximumX, x);
            maximumY = std::max(maximumY, y);
        }
    }
    if (maximumX < minimumX || maximumY < minimumY) return false;

    const glm::vec2 supportHalfExtent(
        static_cast<float>(maximumX - minimumX + 1) /
            (2.0f * static_cast<float>(highlightNormal.width)),
        static_cast<float>(maximumY - minimumY + 1) /
            (2.0f * static_cast<float>(highlightNormal.height)));
    // Pikachu's known-good same-source EyeFinal bake has a 0.131-UV
    // catchlight inside a NormalMap1 sphere whose radius is 253/512 UV.
    // NormalMap1 scales that sphere to the authored iris/pupil footprint for
    // every eye. Preserve the measured ratio instead of applying Pikachu's
    // absolute radius to narrow pupils such as Drowzee's and Hypno's.
    constexpr float kHighlightToNormalSupport =
        0.131f / (253.0f / 512.0f);
    outRadii = glm::min(
        supportHalfExtent * kHighlightToNormalSupport,
        glm::vec2(0.131f));
    const glm::vec2 minimumRadii(
        0.5f / static_cast<float>(highlightNormal.width),
        0.5f / static_cast<float>(highlightNormal.height));
    outRadii = glm::max(outRadii, minimumRadii);
    return true;
}

bool bakeScarletEyeFinalColor(
    const fs::path& root,
    const json& material,
    const glm::vec2& highlightCenter,
    CachedTextureRgba& baseTexture,
    std::string* outError) {
    if (!nativeScarletEyeClearCoat(material) ||
        nativeScarletClearCoatAccessory(material) ||
        !shaderOptionEnabled(material, "EnableHighlight") ||
        !baseTexture.hasPixels()) {
        return true;
    }

    CachedTextureRgba highlightNormal;
    if (!loadTextureByRole(
            root,
            material,
            "NormalMap1",
            highlightNormal,
            outError)) {
        return false;
    }
    if (!highlightNormal.hasPixels()) return true;

    glm::vec4 highlightColor(1.0f);
    float highlightIntensity = 0.0f;
    if (!vec4Parameter(
            material,
            "EmissionColorLayer5",
            highlightColor) ||
        !floatParameter(
            material,
            "EmissionIntensityLayer5",
            highlightIntensity) ||
        highlightIntensity <= 0.0f) {
        return true;
    }

    glm::vec3 highlightNormalBackground(0.5f, 0.5f, 1.0f);
    glm::vec2 highlightRadii(0.0f);
    if (!nativeScarletEyeHighlightRadii(
            highlightNormal,
            highlightNormalBackground,
            highlightRadii)) {
        return true;
    }

    // The same-source Pikachu GLB resolves EyeClearCoat to an EyeFinal color
    // texture containing a stable catchlight rather than a live
    // camera-relative specular lobe. Its position follows the authored
    // pointlight bone, while NormalMap1 supplies the per-eye footprint.
    constexpr float kGlbHighlightSrgb = 251.0f / 255.0f;
    const glm::vec3 resolvedHighlight =
        glm::clamp(glm::vec3(highlightColor), 0.0f, 1.0f) *
        srgbToLinear(kGlbHighlightSrgb);

    CachedTextureRgba baked;
    baked.width = std::max(baseTexture.width, highlightNormal.width);
    baked.height = std::max(baseTexture.height, highlightNormal.height);
    baked.wrapS = baseTexture.wrapS;
    baked.wrapT = baseTexture.wrapT;
    baked.minF = baseTexture.minF;
    baked.magF = baseTexture.magF;
    baked.rgba.resize(
        static_cast<std::size_t>(baked.width) *
        static_cast<std::size_t>(baked.height) * 4u);
    const float antialiasWidth = std::min(
        0.75f /
            (static_cast<float>(std::max(baked.width, baked.height)) *
             std::max(std::min(highlightRadii.x, highlightRadii.y), 1e-6f)),
        0.05f);
    constexpr float kNormalSupportLow = 4.0f / 255.0f;
    constexpr float kNormalSupportHigh = 12.0f / 255.0f;
    for (int y = 0; y < baked.height; ++y) {
        for (int x = 0; x < baked.width; ++x) {
            const float u =
                (static_cast<float>(x) + 0.5f) /
                static_cast<float>(baked.width);
            const float v =
                (static_cast<float>(y) + 0.5f) /
                static_cast<float>(baked.height);
            const glm::vec4 encodedBase = sampleTexture(
                baseTexture,
                u,
                v,
                glm::vec4(1.0f));
            const glm::vec3 baseLinear(
                srgbToLinear(encodedBase.r),
                srgbToLinear(encodedBase.g),
                srgbToLinear(encodedBase.b));
            const glm::vec3 encodedHighlightNormal = glm::vec3(
                sampleTexture(
                    highlightNormal,
                    u,
                    v,
                    glm::vec4(highlightNormalBackground, 1.0f)));
            const glm::vec3 normalDelta = glm::abs(
                encodedHighlightNormal - highlightNormalBackground);
            const float normalSupport = glm::smoothstep(
                kNormalSupportLow,
                kNormalSupportHigh,
                std::max(
                    normalDelta.r,
                    std::max(normalDelta.g, normalDelta.b)));
            const float distance = glm::length(
                (glm::vec2(u, v) - highlightCenter) / highlightRadii);
            const float weight = normalSupport * (1.0f - glm::smoothstep(
                1.0f - antialiasWidth,
                1.0f + antialiasWidth,
                distance));
            const glm::vec3 color = glm::mix(
                baseLinear,
                resolvedHighlight,
                weight);
            const std::size_t offset =
                (static_cast<std::size_t>(y) *
                     static_cast<std::size_t>(baked.width) +
                 static_cast<std::size_t>(x)) * 4u;
            baked.rgba[offset + 0u] = toByte(linearToSrgb(color.r));
            baked.rgba[offset + 1u] = toByte(linearToSrgb(color.g));
            baked.rgba[offset + 2u] = toByte(linearToSrgb(color.b));
            baked.rgba[offset + 3u] = toByte(encodedBase.a);
        }
    }
    baseTexture = std::move(baked);
    return true;
}

struct ClosestPointOnTriangle {
    glm::vec3 point{0.0f};
    glm::vec3 barycentric{1.0f, 0.0f, 0.0f};
};

ClosestPointOnTriangle closestPointOnTriangle(
    const glm::vec3& point,
    const glm::vec3& a,
    const glm::vec3& b,
    const glm::vec3& c) {
    // Christer Ericson, Real-Time Collision Detection, section 5.1.5.
    const glm::vec3 ab = b - a;
    const glm::vec3 ac = c - a;
    const glm::vec3 ap = point - a;
    const float d1 = glm::dot(ab, ap);
    const float d2 = glm::dot(ac, ap);
    if (d1 <= 0.0f && d2 <= 0.0f) {
        return {a, glm::vec3(1.0f, 0.0f, 0.0f)};
    }

    const glm::vec3 bp = point - b;
    const float d3 = glm::dot(ab, bp);
    const float d4 = glm::dot(ac, bp);
    if (d3 >= 0.0f && d4 <= d3) {
        return {b, glm::vec3(0.0f, 1.0f, 0.0f)};
    }

    const float vc = d1 * d4 - d3 * d2;
    if (vc <= 0.0f && d1 >= 0.0f && d3 <= 0.0f) {
        const float v = d1 / (d1 - d3);
        return {a + v * ab, glm::vec3(1.0f - v, v, 0.0f)};
    }

    const glm::vec3 cp = point - c;
    const float d5 = glm::dot(ab, cp);
    const float d6 = glm::dot(ac, cp);
    if (d6 >= 0.0f && d5 <= d6) {
        return {c, glm::vec3(0.0f, 0.0f, 1.0f)};
    }

    const float vb = d5 * d2 - d1 * d6;
    if (vb <= 0.0f && d2 >= 0.0f && d6 <= 0.0f) {
        const float w = d2 / (d2 - d6);
        return {a + w * ac, glm::vec3(1.0f - w, 0.0f, w)};
    }

    const float va = d3 * d6 - d5 * d4;
    if (va <= 0.0f && d4 - d3 >= 0.0f && d5 - d6 >= 0.0f) {
        const float w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
        return {
            b + w * (c - b),
            glm::vec3(0.0f, 1.0f - w, w)};
    }

    const float denominator = 1.0f / (va + vb + vc);
    const float v = vb * denominator;
    const float w = vc * denominator;
    return {
        a + ab * v + ac * w,
        glm::vec3(1.0f - v - w, v, w)};
}

bool nativeEyePointLightPosition(
    const json& material,
    const json& bones,
    const engine::render::model_types::SkinData& skin,
    glm::vec3& outPosition) {
    float packedIndex = 0.0f;
    if (!shaderOptionNumber(
            material,
            "PointLightIndex",
            packedIndex)) {
        return false;
    }
    const int pointLightIndex = static_cast<int>(std::lround(packedIndex));
    if (pointLightIndex < 0 || pointLightIndex > 9) return false;
    const std::string boneName =
        "pointlight" + std::to_string(pointLightIndex);
    for (std::size_t boneIndex = 0u;
         boneIndex < bones.size() && boneIndex < skin.inverseBind.size();
         ++boneIndex) {
        if (bones[boneIndex].value("name", std::string{}) != boneName) {
            continue;
        }
        const glm::mat4 bind = glm::inverse(skin.inverseBind[boneIndex]);
        const glm::vec4 homogeneous = bind * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
        if (!std::isfinite(homogeneous.w) ||
            std::abs(homogeneous.w) < 1e-6f) {
            return false;
        }
        outPosition = glm::vec3(homogeneous) / homogeneous.w;
        return std::isfinite(outPosition.x) &&
            std::isfinite(outPosition.y) &&
            std::isfinite(outPosition.z);
    }
    return false;
}

bool nativeEyeHighlightCenter(
    const std::vector<MeshVertex>& vertices,
    const std::vector<std::uint32_t>& indices,
    std::size_t indexOffset,
    std::size_t indexCount,
    const glm::vec3& pointLightPosition,
    glm::vec2& outCenter) {
    float closestDistanceSquared = std::numeric_limits<float>::max();
    glm::vec2 closestUv(0.5f);
    bool found = false;
    const std::size_t end = std::min(
        indices.size(),
        indexOffset + indexCount);
    for (std::size_t index = indexOffset;
         index + 2u < end;
         index += 3u) {
        const std::uint32_t ia = indices[index + 0u];
        const std::uint32_t ib = indices[index + 1u];
        const std::uint32_t ic = indices[index + 2u];
        if (ia >= vertices.size() ||
            ib >= vertices.size() ||
            ic >= vertices.size()) {
            continue;
        }
        const ClosestPointOnTriangle closest = closestPointOnTriangle(
            pointLightPosition,
            vertices[ia].position,
            vertices[ib].position,
            vertices[ic].position);
        const glm::vec3 pointDelta =
            pointLightPosition - closest.point;
        const float distanceSquared = glm::dot(pointDelta, pointDelta);
        if (!std::isfinite(distanceSquared) ||
            distanceSquared >= closestDistanceSquared) {
            continue;
        }
        closestDistanceSquared = distanceSquared;
        closestUv =
            vertices[ia].uv * closest.barycentric.x +
            vertices[ib].uv * closest.barycentric.y +
            vertices[ic].uv * closest.barycentric.z;
        found = true;
    }
    if (!found ||
        !std::isfinite(closestUv.x) ||
        !std::isfinite(closestUv.y)) {
        return false;
    }

    // A specular catchlight lies on the half vector between the front-facing
    // eye normal and its point light. The per-axis projection below is
    // calibrated from Pikachu's authored pointlight surface projection to
    // the same-source GLB EyeFinal disk (0.64, 0.36). Applying it to each
    // model's own mesh/light pair keeps Pichu and Raichu's glints on their
    // visible eyes even though their UV projections differ from Pikachu's.
    constexpr glm::vec2 kGlbHalfVectorProjection(0.625f, 0.52f);
    outCenter = glm::clamp(
        glm::vec2(0.5f) +
            (closestUv - glm::vec2(0.5f)) *
                kGlbHalfVectorProjection,
        glm::vec2(0.0f),
        glm::vec2(1.0f));
    return true;
}

bool bakeLayeredNormal(
    const fs::path& root,
    const json& material,
    CachedTextureRgba& normalTexture,
    std::string* outError) {
    if (!normalTexture.hasPixels() ||
        nativeScarletEyeClearCoat(material)) {
        // EyeClearCoat NormalMap1 builds EyeFinal's catchlight footprint. Do
        // not blend it through the ordinary layer-mask normal path: the
        // current bounded runtime reconstruction uses the eye shell normal.
        // c8[96] is now proven as a point-light position/enable field, but its
        // bound value and the projected/shadow/environment inputs remain
        // unavailable.
        return true;
    }
    CachedTextureRgba layerNormal;
    CachedTextureRgba layerMask;
    if (!loadTextureByRole(
            root,
            material,
            "NormalMap1",
            layerNormal,
            outError) ||
        !loadTextureByRole(
            root,
            material,
            "LayerMaskMap",
            layerMask,
            outError)) {
        return false;
    }
    if (!layerNormal.hasPixels() || !layerMask.hasPixels()) return true;

    CachedTextureRgba baked;
    baked.width = std::max(
        normalTexture.width,
        std::max(layerNormal.width, layerMask.width));
    baked.height = std::max(
        normalTexture.height,
        std::max(layerNormal.height, layerMask.height));
    baked.wrapS = normalTexture.wrapS;
    baked.wrapT = normalTexture.wrapT;
    baked.minF = normalTexture.minF;
    baked.magF = normalTexture.magF;
    const std::size_t pixelCount =
        static_cast<std::size_t>(baked.width) *
        static_cast<std::size_t>(baked.height);
    baked.rgba.resize(pixelCount * 4u);
    for (int y = 0; y < baked.height; ++y) {
        for (int x = 0; x < baked.width; ++x) {
            const float u =
                (static_cast<float>(x) + 0.5f) /
                static_cast<float>(baked.width);
            const float v =
                (static_cast<float>(y) + 0.5f) /
                static_cast<float>(baked.height);
            const glm::vec4 baseSample = sampleTexture(
                normalTexture,
                u,
                v,
                glm::vec4(0.5f, 0.5f, 1.0f, 1.0f));
            const glm::vec4 layerSample = sampleTexture(
                layerNormal,
                u,
                v,
                baseSample);
            const glm::vec4 mask = sampleTexture(
                layerMask,
                u,
                v,
                glm::vec4(0.0f));
            glm::vec3 baseNormal = glm::vec3(baseSample) * 2.0f - 1.0f;
            glm::vec3 detailNormal = glm::vec3(layerSample) * 2.0f - 1.0f;
            if (glm::dot(baseNormal, baseNormal) < 1e-8f) {
                baseNormal = glm::vec3(0.0f, 0.0f, 1.0f);
            } else {
                baseNormal = glm::normalize(baseNormal);
            }
            if (glm::dot(detailNormal, detailNormal) < 1e-8f) {
                detailNormal = baseNormal;
            } else {
                detailNormal = glm::normalize(detailNormal);
            }
            const glm::vec3 normal = glm::normalize(glm::mix(
                baseNormal,
                detailNormal,
                glm::clamp(mask.g, 0.0f, 1.0f)));
            const glm::vec3 encoded = normal * 0.5f + 0.5f;
            const std::size_t offset =
                (static_cast<std::size_t>(y) *
                     static_cast<std::size_t>(baked.width) +
                 static_cast<std::size_t>(x)) * 4u;
            baked.rgba[offset + 0u] = toByte(encoded.r);
            baked.rgba[offset + 1u] = toByte(encoded.g);
            baked.rgba[offset + 2u] = toByte(encoded.b);
            baked.rgba[offset + 3u] = 255u;
        }
    }
    normalTexture = std::move(baked);
    return true;
}

bool loadMetallicRoughness(
    const fs::path& root,
    const json& material,
    CachedTextureRgba& out,
    std::string* outError) {
    CachedTextureRgba roughness;
    CachedTextureRgba metallic;
    if (!loadTexture(
            root,
            material,
            "roughness_texture",
            roughness,
            outError) ||
        !loadTexture(
            root,
            material,
            "metallic_texture",
            metallic,
            outError)) {
        return false;
    }
    if (!roughness.hasPixels() &&
        !loadSupplementalScarletRoughness(
            root,
            material,
            roughness,
            outError)) {
        return false;
    }
    if (!roughness.hasPixels() && !metallic.hasPixels()) {
        out = CachedTextureRgba{};
        return true;
    }
    out.width = roughness.hasPixels() ? roughness.width : metallic.width;
    out.height = roughness.hasPixels() ? roughness.height : metallic.height;
    out.wrapS = roughness.hasPixels() ? roughness.wrapS : metallic.wrapS;
    out.wrapT = roughness.hasPixels() ? roughness.wrapT : metallic.wrapT;
    out.minF = roughness.hasPixels() ? roughness.minF : metallic.minF;
    out.magF = roughness.hasPixels() ? roughness.magF : metallic.magF;
    const std::size_t pixelCount =
        static_cast<std::size_t>(out.width) *
        static_cast<std::size_t>(out.height);
    out.rgba.assign(pixelCount * 4u, 255u);
    const auto sample = [](const CachedTextureRgba& texture,
                           int x,
                           int y,
                           int width,
                           int height,
                           unsigned char fallback) {
        if (!texture.hasPixels()) return fallback;
        const int sx = std::clamp(
            x * texture.width / std::max(width, 1),
            0,
            texture.width - 1);
        const int sy = std::clamp(
            y * texture.height / std::max(height, 1),
            0,
            texture.height - 1);
        return texture.rgba[
            (static_cast<std::size_t>(sy) *
                 static_cast<std::size_t>(texture.width) +
             static_cast<std::size_t>(sx)) * 4u];
    };
    for (int y = 0; y < out.height; ++y) {
        for (int x = 0; x < out.width; ++x) {
            const std::size_t offset =
                (static_cast<std::size_t>(y) *
                     static_cast<std::size_t>(out.width) +
                 static_cast<std::size_t>(x)) * 4u;
            out.rgba[offset + 0u] = 255u;
            out.rgba[offset + 1u] =
                sample(roughness, x, y, out.width, out.height, 255u);
            out.rgba[offset + 2u] =
                sample(metallic, x, y, out.width, out.height, 0u);
            out.rgba[offset + 3u] = 255u;
        }
    }
    return true;
}

bool bakeIkCharacterSpecularStrength(
    const fs::path& root,
    const json& material,
    CachedTextureRgba& metallicRoughnessTexture,
    bool& outBaked,
    std::string* outError) {
    outBaked = false;
    CachedTextureRgba specularMask;
    if (!loadTextureByRole(
            root,
            material,
            "SpecularMaskMap",
            specularMask,
            outError)) {
        return false;
    }
    if (!specularMask.hasPixels()) return true;

    CachedTextureRgba baked;
    baked.width = std::max(
        specularMask.width,
        metallicRoughnessTexture.hasPixels()
            ? metallicRoughnessTexture.width
            : 0);
    baked.height = std::max(
        specularMask.height,
        metallicRoughnessTexture.hasPixels()
            ? metallicRoughnessTexture.height
            : 0);
    baked.wrapS = specularMask.wrapS;
    baked.wrapT = specularMask.wrapT;
    baked.minF = specularMask.minF;
    baked.magF = specularMask.magF;
    const std::size_t pixelCount =
        static_cast<std::size_t>(baked.width) *
        static_cast<std::size_t>(baked.height);
    baked.rgba.resize(pixelCount * 4u);
    for (int y = 0; y < baked.height; ++y) {
        for (int x = 0; x < baked.width; ++x) {
            const float u =
                (static_cast<float>(x) + 0.5f) /
                static_cast<float>(baked.width);
            const float v =
                (static_cast<float>(y) + 0.5f) /
                static_cast<float>(baked.height);
            const glm::vec4 encodedMetalRough = sampleTexture(
                metallicRoughnessTexture,
                u,
                v,
                glm::vec4(1.0f, 1.0f, 0.0f, 1.0f));
            const glm::vec2 sourceUv = transformedMaterialUv(
                material,
                "UVScaleOffset",
                u,
                v,
                false);
            const float specularStrength = sampleTexture(
                specularMask,
                sourceUv.x,
                sourceUv.y,
                glm::vec4(1.0f)).r;
            const std::size_t offset =
                (static_cast<std::size_t>(y) *
                     static_cast<std::size_t>(baked.width) +
                 static_cast<std::size_t>(x)) * 4u;
            baked.rgba[offset + 0u] = toByte(encodedMetalRough.r);
            baked.rgba[offset + 1u] = toByte(encodedMetalRough.g);
            baked.rgba[offset + 2u] = toByte(encodedMetalRough.b);
            baked.rgba[offset + 3u] = toByte(specularStrength);
        }
    }
    metallicRoughnessTexture = std::move(baked);
    outBaked = true;
    return true;
}

bool bakeIkCharacterLightingAuxiliary(
    const fs::path& root,
    const json& material,
    CachedTextureRgba& shadowSpecTexture,
    CachedTextureRgba& surfaceControlTexture,
    CachedTextureRgba& rimResponseTexture,
    bool& outBaked,
    std::string* outError) {
    outBaked = false;
    CachedTextureRgba layerMask;
    CachedTextureRgba specularMask;
    CachedTextureRgba rimMask;
    if (!loadTextureByRole(
            root,
            material,
            "LayerMaskMap",
            layerMask,
            outError) ||
        !loadTextureByRole(
            root,
            material,
            "SpecularMaskMap",
            specularMask,
            outError) ||
        !loadTextureByRole(
            root,
            material,
            "RimLightMaskMap",
            rimMask,
            outError)) {
        return false;
    }
    if (!layerMask.hasPixels() ||
        !specularMask.hasPixels() ||
        !rimMask.hasPixels()) {
        return true;
    }

    glm::vec4 baseShadow(1.0f);
    (void)vec4Parameter(material, "ShadowingColor", baseShadow);
    float baseSpecular = 0.0f;
    float baseSpecularOffset = 0.0f;
    float baseSpecularContrast = 0.0f;
    float baseMetallic = 0.0f;
    float baseEmissionIntensity = 0.0f;
    glm::vec4 baseEmissionColor(1.0f);
    (void)floatParameter(material, "SpecularIntensity", baseSpecular);
    (void)floatParameter(material, "SpecularOffset", baseSpecularOffset);
    (void)floatParameter(material, "SpecularContrast", baseSpecularContrast);
    (void)floatParameter(material, "Metallic", baseMetallic);
    (void)floatParameter(
        material,
        "EmissionIntensity",
        baseEmissionIntensity);
    (void)vec4Parameter(material, "EmissionColor", baseEmissionColor);
    std::array<glm::vec4, 4u> layerShadows{};
    std::array<float, 4u> layerSpecular{};
    std::array<float, 4u> layerSpecularOffsets{};
    std::array<float, 4u> layerSpecularContrasts{};
    std::array<float, 4u> layerMetallic{};
    std::array<float, 4u> layerEmissionIntensities{};
    std::array<glm::vec4, 4u> layerEmissionColors{};
    std::array<float, 4u> layerScales{1.0f, 1.0f, 1.0f, 1.0f};
    for (std::size_t layer = 0u; layer < layerShadows.size(); ++layer) {
        layerShadows[layer] = baseShadow;
        layerSpecular[layer] = baseSpecular;
        layerSpecularOffsets[layer] = baseSpecularOffset;
        layerSpecularContrasts[layer] = baseSpecularContrast;
        layerMetallic[layer] = baseMetallic;
        layerEmissionIntensities[layer] = baseEmissionIntensity;
        layerEmissionColors[layer] = baseEmissionColor;
        const std::string suffix = std::to_string(layer + 1u);
        (void)vec4Parameter(
            material,
            "ShadowingColorLayer" + suffix,
            layerShadows[layer]);
        (void)floatParameter(
            material,
            "SpecularLayer" + suffix + "Intensity",
            layerSpecular[layer]);
        (void)floatParameter(
            material,
            "SpecularLayer" + suffix + "Offset",
            layerSpecularOffsets[layer]);
        (void)floatParameter(
            material,
            "SpecularLayer" + suffix + "Contrast",
            layerSpecularContrasts[layer]);
        (void)floatParameter(
            material,
            "MetallicLayer" + suffix,
            layerMetallic[layer]);
        (void)floatParameter(
            material,
            "EmissionIntensityLayer" + suffix,
            layerEmissionIntensities[layer]);
        (void)vec4Parameter(
            material,
            "EmissionColorLayer" + suffix,
            layerEmissionColors[layer]);
        (void)floatParameter(
            material,
            "LayerMaskScale" + suffix,
            layerScales[layer]);
    }
    float rimIntensity = 0.0f;
    float backRimIntensity = 0.0f;
    (void)floatParameter(material, "RimLightIntensity", rimIntensity);
    (void)floatParameter(
        material,
        "BackRimLightIntensity",
        backRimIntensity);

    const int width = std::max({
        layerMask.width,
        specularMask.width,
        surfaceControlTexture.hasPixels()
            ? surfaceControlTexture.width
            : 0,
        rimMask.width});
    const int height = std::max({
        layerMask.height,
        specularMask.height,
        surfaceControlTexture.hasPixels()
            ? surfaceControlTexture.height
            : 0,
        rimMask.height});
    CachedTextureRgba bakedShadowSpec;
    CachedTextureRgba bakedSurfaceControl;
    CachedTextureRgba bakedRim;
    bakedShadowSpec.width = bakedSurfaceControl.width =
        bakedRim.width = width;
    bakedShadowSpec.height = bakedSurfaceControl.height =
        bakedRim.height = height;
    bakedShadowSpec.wrapS = bakedSurfaceControl.wrapS =
        bakedRim.wrapS = layerMask.wrapS;
    bakedShadowSpec.wrapT = bakedSurfaceControl.wrapT =
        bakedRim.wrapT = layerMask.wrapT;
    bakedShadowSpec.minF = bakedSurfaceControl.minF =
        bakedRim.minF = layerMask.minF;
    bakedShadowSpec.magF = bakedSurfaceControl.magF =
        bakedRim.magF = layerMask.magF;
    const std::size_t pixelCount =
        static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    bakedShadowSpec.rgba.resize(pixelCount * 4u);
    bakedSurfaceControl.rgba.resize(pixelCount * 4u);
    bakedRim.rgba.resize(pixelCount * 4u);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const float u =
                (static_cast<float>(x) + 0.5f) / static_cast<float>(width);
            const float v =
                (static_cast<float>(y) + 0.5f) / static_cast<float>(height);
            const glm::vec2 sourceUv = transformedMaterialUv(
                material,
                "UVScaleOffset",
                u,
                v);
            const glm::vec4 mask = sampleTexture(
                layerMask,
                sourceUv.x,
                sourceUv.y,
                glm::vec4(0.0f));
            glm::vec3 shadow(baseShadow);
            float specular = baseSpecular;
            float specularOffset = baseSpecularOffset;
            float specularContrast = baseSpecularContrast;
            float metallic = baseMetallic;
            glm::vec3 emission =
                glm::max(glm::vec3(baseEmissionColor), glm::vec3(0.0f)) *
                std::max(baseEmissionIntensity, 0.0f);
            for (std::size_t layer = 0u;
                 layer < layerShadows.size();
                 ++layer) {
                const float weight = glm::clamp(
                    mask[static_cast<glm::length_t>(layer)] *
                        std::max(0.0f, layerScales[layer]),
                    0.0f,
                    1.0f);
                shadow = glm::mix(
                    shadow,
                    glm::vec3(layerShadows[layer]),
                    weight);
                specular = glm::mix(
                    specular,
                    layerSpecular[layer],
                    weight);
                specularOffset = glm::mix(
                    specularOffset,
                    layerSpecularOffsets[layer],
                    weight);
                specularContrast = glm::mix(
                    specularContrast,
                    layerSpecularContrasts[layer],
                    weight);
                metallic = glm::mix(
                    metallic,
                    layerMetallic[layer],
                    weight);
                const glm::vec3 layerEmission =
                    glm::max(
                        glm::vec3(layerEmissionColors[layer]),
                        glm::vec3(0.0f)) *
                    std::max(layerEmissionIntensities[layer], 0.0f);
                emission = glm::mix(emission, layerEmission, weight);
            }
            const float sourceSpecular = glm::clamp(
                sampleTexture(
                    specularMask,
                    sourceUv.x,
                    sourceUv.y,
                    glm::vec4(0.0f)).r *
                    std::max(0.0f, specular),
                0.0f,
                1.0f);
            const float rim = glm::clamp(
                sampleTexture(
                    rimMask,
                    sourceUv.x,
                    sourceUv.y,
                    glm::vec4(0.0f)).r,
                0.0f,
                1.0f);
            const std::size_t offset =
                (static_cast<std::size_t>(y) *
                     static_cast<std::size_t>(width) +
                 static_cast<std::size_t>(x)) * 4u;
            bakedShadowSpec.rgba[offset + 0u] =
                toByte(glm::clamp(shadow.r, 0.0f, 1.0f));
            bakedShadowSpec.rgba[offset + 1u] =
                toByte(glm::clamp(shadow.g, 0.0f, 1.0f));
            bakedShadowSpec.rgba[offset + 2u] =
                toByte(glm::clamp(shadow.b, 0.0f, 1.0f));
            bakedShadowSpec.rgba[offset + 3u] = toByte(sourceSpecular);
            const float sourceOcclusion = surfaceControlTexture.hasPixels()
                ? sampleTexture(
                      surfaceControlTexture,
                      sourceUv.x,
                      sourceUv.y,
                      glm::vec4(1.0f)).r
                : 1.0f;
            // IkCharacter is not roughness/metallic PBR. Its native fragment
            // program consumes AO plus layer-resolved metallic and specular
            // shaping controls directly. Preserve those controls in the
            // otherwise-unused GBA channels of the AO payload:
            //   R = AO, G = metallic, B = signed offset [-0.5, 1],
            //   A = contrast [0, 5].
            bakedSurfaceControl.rgba[offset + 0u] =
                toByte(glm::clamp(sourceOcclusion, 0.0f, 1.0f));
            bakedSurfaceControl.rgba[offset + 1u] =
                toByte(glm::clamp(metallic, 0.0f, 1.0f));
            bakedSurfaceControl.rgba[offset + 2u] = toByte(glm::clamp(
                (specularOffset + 0.5f) / 1.5f,
                0.0f,
                1.0f));
            bakedSurfaceControl.rgba[offset + 3u] = toByte(glm::clamp(
                specularContrast / 5.0f,
                0.0f,
                1.0f));
            // Preserve the authored, pre-composite rim scalars without a
            // viewer exposure baked into the asset. This packed texture is
            // uploaded through the legacy emissive/sRGB slot, so encode its
            // linear controls to sRGB and let the runtime apply the explicitly
            // provisional presentation scale after hardware decode.
            bakedRim.rgba[offset + 0u] = toByte(linearToSrgb(
                rim * std::max(0.0f, rimIntensity)));
            bakedRim.rgba[offset + 1u] = toByte(linearToSrgb(
                rim * std::max(0.0f, backRimIntensity)));
            // The selected Kanto Z-A body corpus authors emission only on
            // Staryu's white layer 3. Blue therefore losslessly carries its
            // scalar final-combine term while R/G retain front/back rim.
            // Luminance remains a bounded fallback if a future source record
            // introduces chromatic body emission before the material ABI
            // gains a dedicated RGB auxiliary texture.
            const float emissionLuminance = glm::dot(
                glm::clamp(emission, glm::vec3(0.0f), glm::vec3(1.0f)),
                glm::vec3(0.2126f, 0.7152f, 0.0722f));
            bakedRim.rgba[offset + 2u] =
                toByte(linearToSrgb(emissionLuminance));
            // Alpha remains neutral. Every selected Kanto Z-A IkCharacter
            // material disables EnableHairSpecular, so injecting an SV
            // roughness atlas or a species-name feather lobe here would invent
            // a branch that the selected source programs do not execute.
            bakedRim.rgba[offset + 3u] = 255u;
        }
    }
    shadowSpecTexture = std::move(bakedShadowSpec);
    surfaceControlTexture = std::move(bakedSurfaceControl);
    rimResponseTexture = std::move(bakedRim);
    outBaked = true;
    return true;
}

bool bakeLayeredMetallicRoughness(
    const fs::path& root,
    const json& material,
    float baseMetallicFactor,
    float baseRoughnessFactor,
    CachedTextureRgba& metalRoughTexture,
    bool preserveSourceAtlas,
    bool& outBaked,
    std::string* outError) {
    outBaked = false;
    CachedTextureRgba layerMask;
    if (!loadTextureByRole(
            root,
            material,
            "LayerMaskMap",
            layerMask,
            outError)) {
        return false;
    }
    if (!layerMask.hasPixels()) return true;

    std::array<float, 4u> layerMetallic{};
    std::array<float, 4u> layerRoughness{};
    std::array<float, 4u> layerScales{1.0f, 1.0f, 1.0f, 1.0f};
    std::array<bool, 4u> hasMetallic{};
    std::array<bool, 4u> hasRoughness{};
    bool anyLayerParameter = false;
    for (std::size_t layer = 0u; layer < layerMetallic.size(); ++layer) {
        const std::string suffix = std::to_string(layer + 1u);
        (void)floatParameter(
            material,
            "LayerMaskScale" + suffix,
            layerScales[layer]);
        hasMetallic[layer] = floatParameter(
            material,
            "MetallicLayer" + suffix,
            layerMetallic[layer]);
        hasRoughness[layer] = floatParameter(
            material,
            "RoughnessLayer" + suffix,
            layerRoughness[layer]);
        anyLayerParameter = anyLayerParameter ||
            hasMetallic[layer] || hasRoughness[layer];
    }
    if (!anyLayerParameter) return true;

    CachedTextureRgba baked;
    baked.width = std::max(
        layerMask.width,
        metalRoughTexture.hasPixels() ? metalRoughTexture.width : 0);
    baked.height = std::max(
        layerMask.height,
        metalRoughTexture.hasPixels() ? metalRoughTexture.height : 0);
    baked.wrapS = layerMask.wrapS;
    baked.wrapT = layerMask.wrapT;
    baked.minF = layerMask.minF;
    baked.magF = layerMask.magF;
    const std::size_t pixelCount =
        static_cast<std::size_t>(baked.width) *
        static_cast<std::size_t>(baked.height);
    baked.rgba.assign(pixelCount * 4u, 255u);
    for (int y = 0; y < baked.height; ++y) {
        for (int x = 0; x < baked.width; ++x) {
            const float u =
                (static_cast<float>(x) + 0.5f) /
                static_cast<float>(baked.width);
            const float v =
                (static_cast<float>(y) + 0.5f) /
                static_cast<float>(baked.height);
            const glm::vec2 sourceUv = transformedMaterialUv(
                material,
                "UVScaleOffset",
                u,
                v,
                preserveSourceAtlas);
            const glm::vec4 encodedBase = sampleTexture(
                metalRoughTexture,
                sourceUv.x,
                sourceUv.y,
                glm::vec4(1.0f, 1.0f, 0.0f, 1.0f));
            const glm::vec4 mask = sampleTexture(
                layerMask,
                sourceUv.x,
                sourceUv.y,
                glm::vec4(0.0f));
            float metallic = metalRoughTexture.hasPixels()
                ? encodedBase.b * baseMetallicFactor
                : baseMetallicFactor;
            float roughness = metalRoughTexture.hasPixels()
                ? encodedBase.g * baseRoughnessFactor
                : baseRoughnessFactor;
            for (std::size_t layer = 0u;
                 layer < layerMetallic.size();
                 ++layer) {
                const float weight = glm::clamp(
                    mask[static_cast<glm::length_t>(layer)] *
                        std::max(0.0f, layerScales[layer]),
                    0.0f,
                    1.0f);
                if (hasMetallic[layer]) {
                    metallic = glm::mix(
                        metallic,
                        glm::clamp(layerMetallic[layer], 0.0f, 1.0f),
                        weight);
                }
                if (hasRoughness[layer]) {
                    roughness = glm::mix(
                        roughness,
                        glm::clamp(layerRoughness[layer], 0.02f, 1.0f),
                        weight);
                }
            }
            const std::size_t offset =
                (static_cast<std::size_t>(y) *
                     static_cast<std::size_t>(baked.width) +
                 static_cast<std::size_t>(x)) * 4u;
            baked.rgba[offset + 0u] = 255u;
            baked.rgba[offset + 1u] = toByte(roughness);
            baked.rgba[offset + 2u] = toByte(metallic);
            baked.rgba[offset + 3u] = 255u;
        }
    }
    metalRoughTexture = std::move(baked);
    outBaked = true;
    return true;
}

void preserveNativeEyeAsDielectric(
    CachedTextureRgba& metalRoughTexture,
    float& metallicFactor) {
    // Game Freak's Eye/EyeClearCoat metallic layer is an input to its
    // dedicated iris/highlight response.  Feeding that channel into glTF's
    // metallic workflow suppresses the authored dark pupil and replaces it
    // with the neutral environment reflection.  Keep the baked per-pixel
    // roughness, but interpret the eye surface itself as a dielectric; the
    // separate mode-28 clear-coat pass still supplies the authored coat.
    metallicFactor = 0.0f;
    if (!metalRoughTexture.hasPixels()) return;
    for (std::size_t offset = 2u;
         offset < metalRoughTexture.rgba.size();
         offset += 4u) {
        metalRoughTexture.rgba[offset] = 0u;
    }
}

bool bakeLayeredEmission(
    const fs::path& root,
    const json& material,
    CachedTextureRgba& emissiveTexture,
    bool preserveSourceAtlas,
    bool& outBaked,
    std::string* outError) {
    outBaked = false;
    CachedTextureRgba layerMask;
    if (!loadTextureByRole(
            root,
            material,
            "LayerMaskMap",
            layerMask,
            outError)) {
        return false;
    }
    if (!layerMask.hasPixels()) return true;

    std::array<glm::vec4, 4u> layerColors{};
    std::array<float, 4u> layerIntensities{};
    std::array<bool, 4u> hasColor{};
    std::array<bool, 4u> hasIntensity{};
    bool anyLayerEmission = false;
    for (std::size_t layer = 0u; layer < layerColors.size(); ++layer) {
        const std::string suffix = std::to_string(layer + 1u);
        hasColor[layer] = vec4Parameter(
            material,
            "EmissionColorLayer" + suffix,
            layerColors[layer]);
        hasIntensity[layer] = floatParameter(
            material,
            "EmissionIntensityLayer" + suffix,
            layerIntensities[layer]);
        anyLayerEmission = anyLayerEmission ||
            (hasColor[layer] && hasIntensity[layer] &&
             layerIntensities[layer] > 0.0f);
    }
    if (!anyLayerEmission) return true;

    CachedTextureRgba baked;
    baked.width = layerMask.width;
    baked.height = layerMask.height;
    baked.wrapS = layerMask.wrapS;
    baked.wrapT = layerMask.wrapT;
    baked.minF = layerMask.minF;
    baked.magF = layerMask.magF;
    const std::size_t pixelCount =
        static_cast<std::size_t>(baked.width) *
        static_cast<std::size_t>(baked.height);
    baked.rgba.assign(pixelCount * 4u, 255u);
    for (int y = 0; y < baked.height; ++y) {
        for (int x = 0; x < baked.width; ++x) {
            const float u =
                (static_cast<float>(x) + 0.5f) /
                static_cast<float>(baked.width);
            const float v =
                (static_cast<float>(y) + 0.5f) /
                static_cast<float>(baked.height);
            const glm::vec2 sourceUv = transformedMaterialUv(
                material,
                "UVScaleOffset",
                u,
                v,
                preserveSourceAtlas);
            const glm::vec4 mask = sampleTexture(
                layerMask,
                sourceUv.x,
                sourceUv.y,
                glm::vec4(0.0f));
            glm::vec3 emission(0.0f);
            for (std::size_t layer = 0u;
                 layer < layerColors.size();
                 ++layer) {
                if (!hasColor[layer] || !hasIntensity[layer]) continue;
                const float weight = glm::clamp(
                    mask[static_cast<glm::length_t>(layer)],
                    0.0f,
                    1.0f);
                // Eye mask channels can overlap: Scarlet's alpha/fourth
                // layer is the pupil laid over the green iris layer. The
                // native shader resolves those channels in order; adding
                // them made the blue iris emission leak back into the black
                // pupil and reduced it to a thin boundary line.
                const glm::vec3 layerEmission =
                    glm::max(
                        glm::vec3(layerColors[layer]),
                        glm::vec3(0.0f)) *
                    std::max(layerIntensities[layer], 0.0f);
                emission = glm::mix(
                    emission,
                    layerEmission,
                    weight);
            }
            emission = glm::clamp(emission, glm::vec3(0.0f), glm::vec3(1.0f));
            const std::size_t offset =
                (static_cast<std::size_t>(y) *
                     static_cast<std::size_t>(baked.width) +
                 static_cast<std::size_t>(x)) * 4u;
            baked.rgba[offset + 0u] = toByte(linearToSrgb(emission.r));
            baked.rgba[offset + 1u] = toByte(linearToSrgb(emission.g));
            baked.rgba[offset + 2u] = toByte(linearToSrgb(emission.b));
        }
    }
    emissiveTexture = std::move(baked);
    outBaked = true;
    return true;
}

bool addAnimationSampler(
    const json& view,
    std::size_t components,
    float valueScale,
    engine::render::model_types::ChannelPath path,
    int targetNode,
    std::uint32_t frameRate,
    const std::vector<std::uint8_t>& payload,
    engine::render::model_types::AnimationClip& clip,
    std::string* outError) {
    std::vector<float> values;
    if (!decodeView(
            view,
            "float32",
            components,
            payload,
            values,
            outError)) {
        return false;
    }
    const std::size_t count = values.size() / components;
    engine::render::model_types::AnimationSampler sampler;
    sampler.interpolation = "LINEAR";
    sampler.isVec4 = components == 4u;
    sampler.inputs.reserve(count);
    sampler.outputs.reserve(count);
    for (std::size_t index = 0u; index < count; ++index) {
        sampler.inputs.push_back(
            static_cast<float>(index) /
            static_cast<float>(std::max(1u, frameRate)));
        const std::size_t offset = index * components;
        sampler.outputs.push_back(glm::vec4(
            values[offset + 0u] * valueScale,
            values[offset + 1u] * valueScale,
            values[offset + 2u] * valueScale,
            components == 4u ? values[offset + 3u] : 0.0f));
    }
    const int samplerIndex = static_cast<int>(clip.samplers.size());
    clip.samplers.push_back(std::move(sampler));
    clip.channels.push_back(
        engine::render::model_types::AnimationChannel{
            samplerIndex,
            targetNode,
            path});
    return true;
}

} // namespace

namespace tools::phlosion_native_model_ir {

bool load(
    const std::string& manifestPath,
    MeshData& out,
    std::string* outError) {
    out = MeshData{};
    try {
        json document;
        if (!readJson(manifestPath, document, outError)) return false;
        if (document.value("schema", std::string{}) != kSchema ||
            document.value("schema_version", 0u) != 1u) {
            return fail(outError, "Unsupported native model IR schema.");
        }
        const auto& coordinateSystem = document.at("coordinate_system");
        if (coordinateSystem.value(
                "texcoords_0",
                std::string{}) != "gamefreak_native") {
            return fail(
                outError,
                "Native model IR must preserve Game Freak UV coordinates.");
        }
        const float unitScaleToMeters = coordinateSystem.value(
            "unit_scale_to_meters",
            1.0f);
        if (!std::isfinite(unitScaleToMeters) ||
            unitScaleToMeters <= 0.0f ||
            unitScaleToMeters > 1000.0f) {
            return fail(
                outError,
                "Native model IR unit scale must be finite and positive.");
        }
        const fs::path manifest = fs::path(manifestPath);
        const fs::path root = manifest.parent_path();
        fs::path payloadPath;
        if (!resolveContainedFile(
                root,
                document.at("payload").at("file").get<std::string>(),
                payloadPath,
                outError)) {
            return false;
        }
        std::vector<std::uint8_t> payload;
        if (!readBytes(payloadPath, payload, outError)) return false;
        if (payload.size() !=
            document.at("payload").at("byte_length").get<std::size_t>()) {
            return fail(outError, "Native model IR payload length changed.");
        }

        const auto& model = document.at("model");
        const bool nativeGolduckModel =
            model.value("name", std::string{}) == "pm0055_00_00";
        const std::string nativeModelName =
            model.value("name", std::string{});
        const std::string nativeSourceProfile = document.contains("source")
            ? document.at("source").value("profile", std::string{})
            : std::string{};
        const bool nativeZaSource =
            nativeSourceProfile.starts_with("pokemon-legends-za");
        const bool nativeScarletSource =
            nativeSourceProfile.starts_with("pokemon-scarlet");
        const bool nativeLegacyEeveeFamilySoftCoat =
            nativeModelName.starts_with("pm0134_") ||
            nativeModelName.starts_with("pm0135_") ||
            nativeModelName.starts_with("pm0136_");
        const auto& submeshes = model.at("submeshes");
        const auto& materials = document.at("materials");
        const auto& bones = document.at("skeleton").at("bones");
        const auto& animationRecords = document.at("animations");
        if (model.at("vertex_count").get<std::size_t>() > kMaxVertices ||
            model.at("index_count").get<std::size_t>() > kMaxIndices ||
            bones.size() > kMaxBones ||
            animationRecords.size() > kMaxAnimations ||
            submeshes.size() > kMaxSubmeshes ||
            materials.empty()) {
            return fail(outError, "Native model IR exceeds safety limits.");
        }

        const std::size_t boneCount = bones.size();
        const std::size_t submeshCount = submeshes.size();
        const std::size_t nodeCount = 1u + boneCount + submeshCount;
        out.nodesDefault.assign(
            nodeCount,
            engine::render::model_types::NodeTRS{});
        out.nodeNames.assign(nodeCount, std::string{});
        out.nodeChildren.assign(nodeCount, {});
        out.nodeParent.assign(nodeCount, -1);
        out.nodeMesh.assign(nodeCount, -1);
        out.nodeSkin.assign(nodeCount, -1);
        out.sceneRoots = {0};
        out.nodeNames[0] = model.value("name", "NativeModel");

        engine::render::model_types::SkinData skin;
        skin.joints.reserve(boneCount);
        skin.inverseBind.reserve(boneCount);
        for (std::size_t index = 0u; index < boneCount; ++index) {
            const auto& record = bones[index];
            const int node = static_cast<int>(index + 1u);
            const int parentBone = record.value("parent", -1);
            if (parentBone < -1 ||
                parentBone >= static_cast<int>(boneCount) ||
                parentBone == static_cast<int>(index)) {
                return fail(
                    outError,
                    "Native model IR bone parent is invalid.");
            }
            const int parentNode =
                parentBone >= 0 ? parentBone + 1 : 0;
            out.nodeNames[static_cast<std::size_t>(node)] =
                record.at("name").get<std::string>();
            out.nodesDefault[static_cast<std::size_t>(node)].t =
                vec3(record.at("translation")) * unitScaleToMeters;
            out.nodesDefault[static_cast<std::size_t>(node)].r =
                quat(record.at("rotation"));
            out.nodesDefault[static_cast<std::size_t>(node)].s =
                vec3(record.at("scale"));
            out.nodesDefault[static_cast<std::size_t>(node)]
                .segmentScaleCompensate =
                record.value("segment_scale_compensate", false);
            out.nodeParent[static_cast<std::size_t>(node)] = parentNode;
            out.nodeChildren[static_cast<std::size_t>(parentNode)].push_back(node);
            skin.joints.push_back(node);
            std::vector<float> matrixValues;
            if (!decodeView(
                    record.at("inverse_bind"),
                    "float32",
                    16u,
                    payload,
                    matrixValues,
                    outError)) {
                return false;
            }
            glm::mat4 inverseBind = glm::make_mat4(matrixValues.data());
            inverseBind[3].x *= unitScaleToMeters;
            inverseBind[3].y *= unitScaleToMeters;
            inverseBind[3].z *= unitScaleToMeters;
            skin.inverseBind.push_back(inverseBind);
        }
        for (std::size_t index = 0u; index < boneCount; ++index) {
            int cursor = static_cast<int>(index);
            for (std::size_t depth = 0u; depth <= boneCount; ++depth) {
                if (cursor < 0) break;
                if (depth == boneCount) {
                    return fail(
                        outError,
                        "Native model IR skeleton contains a parent cycle.");
                }
                cursor = bones[static_cast<std::size_t>(cursor)]
                             .value("parent", -1);
            }
        }
        out.skins.push_back(std::move(skin));

        bool initializedBounds = false;
        std::vector<std::uint8_t> vertexColorEnabled;
        std::vector<std::uint8_t> submeshDoubleSided(
            submeshCount,
            1u);
        std::vector<std::size_t> materialUseCounts(materials.size(), 0u);
        std::vector<bool> materialUsesEyeAShell(
            materials.size(),
            false);
        std::vector<bool> materialUsesEyeBShell(
            materials.size(),
            false);
        std::vector<std::string> materialClipBoundEyeUvParameter(
            materials.size());
        for (std::size_t materialIndex = 0u;
             materialIndex < materials.size();
             ++materialIndex) {
            materialClipBoundEyeUvParameter[materialIndex] =
                nativeClipBoundEyeUvParameter(
                    animationRecords,
                    materials[materialIndex]);
        }
        for (const auto& record : submeshes) {
            const std::size_t materialIndex =
                record.at("material").get<std::size_t>();
            if (materialIndex >= materialUseCounts.size()) {
                return fail(
                    outError,
                    "Native model IR material index is invalid.");
            }
            ++materialUseCounts[materialIndex];
            const std::string submeshName =
                record.value("name", std::string{});
            materialUsesEyeAShell[materialIndex] =
                materialUsesEyeAShell[materialIndex] ||
                submeshName.find("_eye_a_") != std::string::npos;
            materialUsesEyeBShell[materialIndex] =
                materialUsesEyeBShell[materialIndex] ||
                submeshName.find("_eye_b_") != std::string::npos;
        }
        out.meshIndexToNode.assign(submeshCount, -1);
        for (std::size_t submeshIndex = 0u;
             submeshIndex < submeshCount;
             ++submeshIndex) {
            const auto& record = submeshes[submeshIndex];
            const std::size_t materialIndex =
                record.at("material").get<std::size_t>();
            if (materialIndex >= materials.size()) {
                return fail(
                    outError,
                    "Native model IR material index is invalid.");
            }
            const auto& material = materials[materialIndex];
            const std::string submeshName =
                record.value("name", std::string{});
            const bool clipBoundEyeUv =
                !materialClipBoundEyeUvParameter[materialIndex].empty();
            const std::size_t vertexCount = record.at("vertex_count").get<std::size_t>();
            const std::size_t indexCount = record.at("index_count").get<std::size_t>();
            std::vector<float> positions;
            std::vector<float> normals;
            std::vector<float> texcoords;
            std::vector<float> colors;
            std::vector<float> tangents;
            std::vector<std::uint16_t> joints;
            std::vector<float> weights;
            std::vector<std::uint32_t> indices;
            if (!decodeView(record.at("positions"), "float32", 3u, payload, positions, outError) ||
                !decodeView(record.at("normals"), "float32", 3u, payload, normals, outError) ||
                !decodeView(record.at("texcoords_0"), "float32", 2u, payload, texcoords, outError) ||
                !decodeView(record.at("colors_0"), "float32", 4u, payload, colors, outError) ||
                !decodeView(record.at("tangents"), "float32", 4u, payload, tangents, outError) ||
                !decodeView(record.at("joints_0"), "uint16", 4u, payload, joints, outError) ||
                !decodeView(record.at("weights_0"), "float32", 4u, payload, weights, outError) ||
                !decodeView(record.at("indices"), "uint32", 1u, payload, indices, outError)) {
                return false;
            }
            if (positions.size() != vertexCount * 3u ||
                normals.size() != vertexCount * 3u ||
                texcoords.size() != vertexCount * 2u ||
                colors.size() != vertexCount * 4u ||
                tangents.size() != vertexCount * 4u ||
                joints.size() != vertexCount * 4u ||
                weights.size() != vertexCount * 4u ||
                indices.size() != indexCount) {
                return fail(outError, "Native model IR submesh counts changed.");
            }

            const bool negativeUnitEyeTile =
                nativeNegativeUnitEyeTile(material, texcoords);

            const std::size_t baseVertex = out.vertices.size();
            const std::size_t indexOffset = out.indices.size();
            out.vertices.reserve(out.vertices.size() + vertexCount);
            for (std::size_t index = 0u; index < vertexCount; ++index) {
                const std::size_t p3 = index * 3u;
                const std::size_t p2 = index * 2u;
                const std::size_t p4 = index * 4u;
                MeshVertex vertex;
                vertex.position = glm::vec3(
                    positions[p3 + 0u],
                    positions[p3 + 1u],
                    positions[p3 + 2u]) * unitScaleToMeters;
                const glm::vec3 normal(
                    normals[p3 + 0u],
                    normals[p3 + 1u],
                    normals[p3 + 2u]);
                vertex.normal = glm::dot(normal, normal) > 1e-12f
                    ? glm::normalize(normal)
                    : glm::vec3(0.0f, 1.0f, 0.0f);
                vertex.uv = glm::vec2(
                    negativeUnitEyeTile
                        ? gameFreakNegativeUnitUToRuntime(
                              texcoords[p2 + 0u])
                        : texcoords[p2 + 0u],
                    gameFreakNativeVToRuntime(
                        texcoords[p2 + 1u]));
                vertex.color = glm::vec4(
                    colors[p4 + 0u],
                    colors[p4 + 1u],
                    colors[p4 + 2u],
                    colors[p4 + 3u]);
                const glm::vec3 tangent =
                    gameFreakNativeTangentToRuntime(glm::vec3(
                    tangents[p4 + 0u],
                    tangents[p4 + 1u],
                    tangents[p4 + 2u]));
                vertex.tangent = glm::dot(tangent, tangent) > 1e-12f
                    ? glm::vec4(
                          glm::normalize(tangent),
                          tangents[p4 + 3u] < 0.0f ? -1.0f : 1.0f)
                    : glm::vec4(0.0f, 0.0f, 0.0f, 0.0f);
                vertex.j0 = joints[p4 + 0u];
                vertex.j1 = joints[p4 + 1u];
                vertex.j2 = joints[p4 + 2u];
                vertex.j3 = joints[p4 + 3u];
                glm::vec4 normalizedWeights(
                    weights[p4 + 0u],
                    weights[p4 + 1u],
                    weights[p4 + 2u],
                    weights[p4 + 3u]);
                const float weightSum = normalizedWeights.x + normalizedWeights.y +
                    normalizedWeights.z + normalizedWeights.w;
                normalizedWeights = weightSum > 0.0001f
                    ? normalizedWeights / weightSum
                    : glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);
                vertex.w0 = normalizedWeights.x;
                vertex.w1 = normalizedWeights.y;
                vertex.w2 = normalizedWeights.z;
                vertex.w3 = normalizedWeights.w;
                if (!initializedBounds) {
                    out.boundsMin = vertex.position;
                    out.boundsMax = vertex.position;
                    initializedBounds = true;
                } else {
                    out.boundsMin = glm::min(out.boundsMin, vertex.position);
                    out.boundsMax = glm::max(out.boundsMax, vertex.position);
                }
                out.vertices.push_back(vertex);
            }
            out.indices.reserve(out.indices.size() + indexCount);
            for (const std::uint32_t index : indices) {
                if (index >= vertexCount) {
                    return fail(outError, "Native model IR index is out of range.");
                }
                out.indices.push_back(
                    static_cast<std::uint32_t>(baseVertex) + index);
            }

            const int meshNode =
                static_cast<int>(1u + boneCount + submeshIndex);
            out.nodeNames[static_cast<std::size_t>(meshNode)] =
                record.value("name", "Submesh");
            out.nodeMesh[static_cast<std::size_t>(meshNode)] =
                static_cast<int>(submeshIndex);
            out.nodeSkin[static_cast<std::size_t>(meshNode)] =
                record.value("has_skinning", false) ? 0 : -1;
            out.nodeParent[static_cast<std::size_t>(meshNode)] = 0;
            out.nodeChildren[0].push_back(meshNode);
            out.meshIndexToNode[submeshIndex] = meshNode;

            const bool nativeUnlitDisplaced =
                nativeLayeredUnlitDisplaced(material);
            const bool nativeLitDisplaced =
                nativeGastlyDisplacedSmoke(material) ||
                nativeSssEffectDisplaced(material);
            const bool nativeScarletGastlySmoke =
                nativeScarletGastlyDisplacedSmoke(material);
            const bool nativeEye = nativeEyeClearCoat(material);
            const bool nativeScarletEye =
                nativeScarletEyeClearCoat(material);
            const bool nativePlainEye =
                material.value("shader_family", std::string{}) == "Eye";
            const bool nativePlaFlatAnimatedEye =
                nativePlaFlatAnimatedEyeAtlas(material);
            const bool nativeChanseyJewel =
                nativeChanseyJewelBody(material);
            const bool nativeStaryuFamilyJewel =
                nativeZaStaryuFamilyJewel(material);
            const bool nativeIkCharacterEyeMaterial =
                nativeIkCharacterEye(material);
            const bool nativeKangaskhanBabyEyeMaterial =
                nativeKangaskhanBabyEye(material);
            const bool nativeLayeredEyeMaterial =
                nativePlainEye && materialUseCounts[materialIndex] > 1u &&
                materialUsesEyeAShell[materialIndex] &&
                materialUsesEyeBShell[materialIndex];
            const bool nativeLayeredEyePupil =
                nativeLayeredEyeMaterial &&
                submeshName.find("_eye_a_") != std::string::npos;
            const bool nativeTransparentLayer =
                material.value("shader_family", std::string{}) ==
                "Transparent";
            const bool nativeTransparentEyeLens =
                nativeTransparentLayer &&
                submeshName.find("_eye_b_") != std::string::npos &&
                shaderOptionEquals(
                    material,
                    "RefractionMode",
                    "Thin");
            const bool nativeTransparentEyeGlint =
                nativeTransparentLayer &&
                submeshName.find("_eye_c_") != std::string::npos;
            const bool nativeScarletAccessory =
                nativeScarletClearCoatAccessory(material);
            const bool nativeGastlyFace =
                nativeGastlyFaceOverlay(material);
            const bool nativeGastlyEye =
                nativeGastlyEyeOverlay(material);
            const bool nativeScarletGastlyFace =
                nativeScarletGastlyFaceDepthOverlay(material);
            const bool nativeScarletGastlyEye =
                nativeScarletGastlyEyeDepthOverlay(material);
            if (nativeScarletGastlyEye) {
                // The exported generic runtime metadata marks every native
                // Pokemon surface double-sided, but Gastly's separate front
                // eye shells must retain their source back-face rejection.
                // Otherwise their slightly stronger face-before-smoke depth
                // bias pulls them through the opaque head from a rear view.
                submeshDoubleSided[submeshIndex] = 0u;
            }
            const bool nativeSupplementalScarletRoughness =
                !supplementalScarletRoughnessFilename(material).empty();
            const bool nativeScarletSss =
                nativeScarletSource &&
                material.value("shader_family", std::string{}) == "SSS" &&
                hasTextureRole(material, "RoughnessMap") &&
                hasTextureRole(material, "SSSMaskMap");
            const bool nativeFresnelEffect =
                (nativeScarletSource || nativeZaSource) &&
                material.value("shader_family", std::string{}) ==
                    "FresnelEffect" &&
                shaderOptionEnabled(material, "EnableFresnelTexture") &&
                shaderOptionEnabled(material, "EnableLocalIBL") &&
                hasTextureRole(material, "BaseColorMap1") &&
                hasTextureRole(material, "LocalSpecularProbe");
            const bool nativeScarletSssFibre =
                nativeScarletSss && nativeModelName.starts_with("pm0133_");
            const bool nativeIkCharacterLightingCandidate =
                (nativeZaSource || nativeLegacyEeveeFamilySoftCoat) &&
                !nativeSupplementalScarletRoughness &&
                material.value("shader_family", std::string{}) ==
                    "IkCharacter" &&
                !shaderOptionEnabled(material, "EnableEyeOptions") &&
                !nativeUnlitDisplaced &&
                !nativeGastlyFace &&
                !nativeGastlyEye &&
                hasTextureRole(material, "LayerMaskMap") &&
                hasTextureRole(material, "SpecularMaskMap") &&
                hasTextureRole(material, "RimLightMaskMap");
            const bool nativeIkCharacterEyeLightingCandidate =
                nativeZaSource &&
                nativeIkCharacterEyeMaterial &&
                hasTextureRole(material, "LayerMaskMap") &&
                hasTextureRole(material, "SpecularMaskMap") &&
                hasTextureRole(material, "RimLightMaskMap") &&
                hasTextureRole(material, "ParallaxMap") &&
                hasTextureRole(material, "LocalReflectionMap");
            const bool nativeIkCharacterSpecularStrengthCandidate =
                material.value("shader_family", std::string{}) ==
                    "IkCharacter" &&
                !shaderOptionEnabled(material, "EnableEyeOptions") &&
                !nativeUnlitDisplaced &&
                !nativeGastlyFace &&
                !nativeGastlyEye &&
                !nativeIkCharacterLightingCandidate &&
                hasTextureRole(material, "SpecularMaskMap");
            bool nativeIkCharacterLighting = false;
            bool nativeIkCharacterEyeLighting = false;
            bool nativeIkCharacterSpecularStrength = false;
            const bool nativeScarletEyeSurface =
                nativeScarletEye && !nativeScarletAccessory;
            const bool nativeEyeSurface =
                nativePlainEye || nativeTransparentEyeLens ||
                nativeScarletEyeSurface;
            const bool nativeLgpeLayered = nativeLgpeLayeredColor(material);
            const auto advanceEyeLayerTowardViewer =
                [&](float extentScale) {
                    if (vertexCount == 0u) return;
                    glm::vec3 layerMinimum(
                        std::numeric_limits<float>::max());
                    glm::vec3 layerMaximum(
                        std::numeric_limits<float>::lowest());
                    for (std::size_t vertexIndex = baseVertex;
                         vertexIndex < out.vertices.size();
                         ++vertexIndex) {
                        layerMinimum = glm::min(
                            layerMinimum,
                            out.vertices[vertexIndex].position);
                        layerMaximum = glm::max(
                            layerMaximum,
                            out.vertices[vertexIndex].position);
                    }
                    const glm::vec3 layerExtent =
                        glm::max(
                            layerMaximum - layerMinimum,
                            glm::vec3(0.0f));
                    const float surfaceOffset =
                        extentScale * std::max(
                            layerExtent.x,
                            std::max(layerExtent.y, layerExtent.z));
                    for (std::size_t vertexIndex = baseVertex;
                         vertexIndex < out.vertices.size();
                         ++vertexIndex) {
                        out.vertices[vertexIndex].position.z += surfaceOffset;
                    }
                };
            if (nativeLayeredEyePupil) {
                // PLA's opaque Eye program optically composites the smaller
                // eye_a pupil with the enclosing eye_b iris. The runtime has
                // no source depth-compositing pass, so flatten that optical
                // stack: retain both authored colors and sizes, but advance
                // eye_a far enough along the model's forward axis for its
                // convex face to remain visible above the opaque iris.
                advanceEyeLayerTowardViewer(0.62f);
            }
            if (nativeTransparentEyeGlint) {
                // eye_c occupies the iris surface in the source optical pass.
                // A tiny forward separation prevents the opaque eye_b depth
                // write from rejecting its sparse emissive catchlight.
                advanceEyeLayerTowardViewer(0.008f);
            }
            vertexColorEnabled.insert(
                vertexColorEnabled.end(),
                vertexCount,
                shaderOptionEnabled(
                    material,
                    "EnableVertexColor")
                    ? 1u
                    : 0u);
            const auto& translation = material.at("runtime_translation");
            CachedTextureRgba baseTexture;
            CachedTextureRgba normalTexture;
            CachedTextureRgba metalRoughTexture;
            CachedTextureRgba occlusionTexture;
            CachedTextureRgba emissiveTexture;
            CachedTextureRgba environmentTexture;
            float sourceNormalScale =
                translation.value("normal_scale", 1.0f);
            if (nativeScarletEye) {
                // Every selected Scarlet/Violet EyeClearCoat permutation
                // enables NormalMap1, and all four exact compiled programs
                // sample it through tcb_1E. Its proven role is the source
                // highlight-normal input: Forge preserves the texture and
                // uses its authored support to resolve EyeFinal's stable
                // catchlight. Applying it as Phlosion's generic base normal
                // without the projected/shadow/environment source inputs
                // causes full-eye banding, so the runtime coat currently uses
                // the shell normal. c8[96] is proven as a point-light
                // position/enable field; NormalHeight1 remains retained for
                // diagnostics and the eventual complete scene bridge.
                (void)floatParameter(
                    material,
                    "NormalHeight1",
                    sourceNormalScale);
            }
            float sourceMetallicFactor =
                translation.value("metallic_factor", 0.0f);
            float sourceRoughnessFactor =
                nativeSupplementalScarletRoughness
                    ? 1.0f
                    : translation.value("roughness_factor", 1.0f);
            bool layeredMetalRoughBaked = false;
            bool layeredEmissionBaked = false;
            glm::vec2 scarletEyeHighlightCenter(0.64f, 0.36f);
            if (nativeScarletEye && !out.skins.empty()) {
                glm::vec3 pointLightPosition(0.0f);
                if (nativeEyePointLightPosition(
                        material,
                        bones,
                        out.skins.front(),
                        pointLightPosition)) {
                    (void)nativeEyeHighlightCenter(
                        out.vertices,
                        out.indices,
                        indexOffset,
                        indexCount,
                        pointLightPosition,
                        scarletEyeHighlightCenter);
                }
            }
            if (!loadTexture(root, material, "base_color_texture", baseTexture, outError) ||
                !loadTexture(root, material, "normal_texture", normalTexture, outError) ||
                (nativeScarletEye &&
                 !loadTextureByRole(
                     root,
                     material,
                     "NormalMap1",
                     normalTexture,
                     outError)) ||
                (nativeUnlitDisplaced &&
                 !loadTextureByRole(
                     root,
                     material,
                     "DisplacementMap",
                     normalTexture,
                     outError)) ||
                !loadMetallicRoughness(root, material, metalRoughTexture, outError) ||
                (nativeFresnelEffect &&
                 !loadTextureByRole(
                     root,
                     material,
                     "NormalMap1",
                     metalRoughTexture,
                     outError)) ||
                (nativeUnlitDisplaced &&
                 !loadTextureByRole(
                     root,
                     material,
                     "LayerMaskMap",
                     metalRoughTexture,
                     outError)) ||
                (!nativeUnlitDisplaced && !nativeScarletEye &&
                 !nativeFresnelEffect &&
                 !nativeIkCharacterLightingCandidate &&
                 !nativeIkCharacterEyeLightingCandidate &&
                 !bakeLayeredMetallicRoughness(
                     root,
                     material,
                     sourceMetallicFactor,
                     sourceRoughnessFactor,
                     metalRoughTexture,
                     clipBoundEyeUv,
                     layeredMetalRoughBaked,
                     outError)) ||
                (nativeIkCharacterSpecularStrengthCandidate &&
                 !bakeIkCharacterSpecularStrength(
                     root,
                     material,
                     metalRoughTexture,
                     nativeIkCharacterSpecularStrength,
                     outError)) ||
                !loadTexture(root, material, "occlusion_texture", occlusionTexture, outError) ||
                (nativeIkCharacterEyeLightingCandidate &&
                 !loadTextureByRole(
                     root,
                     material,
                     "OcclusionMap",
                     occlusionTexture,
                     outError)) ||
                !loadTexture(root, material, "emissive_texture", emissiveTexture, outError) ||
                (nativeFresnelEffect &&
                 !loadTextureByRole(
                     root,
                     material,
                     "BaseColorMap1",
                     emissiveTexture,
                     outError)) ||
                (nativeFresnelEffect &&
                 !loadTextureByRole(
                     root,
                     material,
                     "LocalSpecularProbe",
                     environmentTexture,
                     outError)) ||
                ((nativeIkCharacterLightingCandidate ||
                  nativeIkCharacterEyeLightingCandidate) &&
                 !loadTextureByRole(
                     root,
                     material,
                     "LocalReflectionMap",
                     environmentTexture,
                     outError)) ||
                (nativeScarletSss &&
                 !loadTextureByRole(
                     root,
                     material,
                     "SSSMaskMap",
                     emissiveTexture,
                     outError)) ||
                (nativeIkCharacterLightingCandidate &&
                 !bakeIkCharacterLightingAuxiliary(
                     root,
                     material,
                     metalRoughTexture,
                     occlusionTexture,
                     emissiveTexture,
                     nativeIkCharacterLighting,
                     outError)) ||
                (nativeIkCharacterEyeLightingCandidate &&
                 !bakeIkCharacterLightingAuxiliary(
                     root,
                     material,
                     metalRoughTexture,
                     occlusionTexture,
                     emissiveTexture,
                     nativeIkCharacterEyeLighting,
                     outError)) ||
                ((nativePlainEye || nativeTransparentLayer ||
                  nativeScarletGastlyEye) &&
                 !bakeLayeredEmission(
                    root,
                    material,
                    emissiveTexture,
                    clipBoundEyeUv,
                    layeredEmissionBaked,
                    outError)) ||
                (nativePlainEye && !bakeEyeHighlightEmission(
                    root,
                    material,
                    emissiveTexture,
                    layeredEmissionBaked,
                    outError)) ||
                (nativeLgpeLayered && !bakeLgpeLayeredColor(
                    root,
                    material,
                    baseTexture,
                    outError)) ||
                ((!nativeUnlitDisplaced || nativeLitDisplaced) &&
                 !nativeScarletAccessory &&
                 !bakeLayeredBaseColor(
                     root,
                     material,
                     baseTexture,
                     clipBoundEyeUv || nativeLitDisplaced,
                     nullptr,
                     outError)) ||
                (nativeScarletEye && !bakeScarletEyeFinalColor(
                    root,
                    material,
                    scarletEyeHighlightCenter,
                    baseTexture,
                    outError)) ||
                (!nativeUnlitDisplaced && !bakeLayeredNormal(
                    root,
                    material,
                    normalTexture,
                    outError))) {
                return false;
            }
            if (nativeKangaskhanBabyEyeMaterial) {
                // The child's eye map is deliberately white and its layer
                // mask deliberately empty. Z-A supplies the actual dark eye
                // color as BaseColorLayer1 instead of selecting it through a
                // mask channel.
                bakeNativeKangaskhanBabyEyeBase(material, baseTexture);
            }
            if (nativeStaryuFamilyJewel) {
                bakeNativeZaStaryuFamilyJewelBase(material, baseTexture);
            }
            if (nativeIkCharacterEyeMaterial &&
                !bakeEyeHighlightEmission(
                    root,
                    material,
                    emissiveTexture,
                    layeredEmissionBaked,
                    outError)) {
                return false;
            }
            if (nativeIkCharacterEyeLighting &&
                !bakeIkCharacterEyePackedInputs(
                    root,
                    material,
                    normalTexture,
                    emissiveTexture,
                    outError)) {
                return false;
            }
            if (nativeGastlyFace &&
                !bakeNativeGastlyFaceAuxiliary(
                    root,
                    material,
                    baseTexture,
                    metalRoughTexture,
                    emissiveTexture,
                    outError)) {
                return false;
            }
            if (nativeChanseyJewel &&
                !bakeNativeChanseyJewelEmission(
                    baseTexture,
                    metalRoughTexture,
                    emissiveTexture,
                    layeredEmissionBaked)) {
                return false;
            }
            if (nativeTransparentLayer && layeredEmissionBaked) {
                // PLA's separate Transparent eye shell carries the authored
                // catchlight in its layer emission. Its exported base map is
                // opaque and shared with the body, so use the resolved glint
                // itself as coverage instead of drawing that entire shell.
                setTextureAlphaFromEmission(
                    baseTexture,
                    emissiveTexture);
                if (nativeTransparentEyeLens) {
                    // Venomoth's eye_b is a full thin-refractive lens rather
                    // than a sparse glint. The runtime does not yet have a
                    // scene-color refraction buffer, so preserve the native
                    // forward-viewer approximation: a 35%-covered dielectric
                    // clear-coat lens over the opaque eye_a compound-eye core.
                    setTextureAlpha(baseTexture, 0.35f);
                }
            }
            if (nativeEye || nativeTransparentEyeLens) {
                preserveNativeEyeAsDielectric(
                    metalRoughTexture,
                    sourceMetallicFactor);
            }
            if (nativeScarletEye) {
                // Same-source GLBs resolve the stable catchlight into
                // EyeFinal. Keep that base-color result while mode 28/30 now
                // restores every proven authored coat/highlight constant.
                // Clear the generic metal/rough slot so it cannot double-light
                // the dedicated native pass. Gastly retains its qualified
                // shell emission above the dark face.
                metalRoughTexture = CachedTextureRgba{};
                sourceMetallicFactor = 0.0f;
                if (!nativeScarletGastlyEye) {
                    if (nativeScarletAccessory) {
                        sourceRoughnessFactor = 0.45f;
                    } else {
                        sourceRoughnessFactor = 0.5f;
                    }
                    emissiveTexture = CachedTextureRgba{};
                    layeredEmissionBaked = false;
                }
                layeredMetalRoughBaked = false;
            }
            if (nativeLitDisplaced) {
                // Lit Game Freak smoke is displaced like the native Unlit
                // effects. Z-A supplies zero EmissionIntensity because it
                // lights the already-layered source color, while Scarlet's
                // NonDirectional material supplies one. In both cases the
                // runtime transport must preserve the baked source palette.
                // The runtime displaced path is emissive-only and exposes two
                // of the source's four mask colors. Bake all four authored
                // layers into the base atlas, then feed a zero selector map so
                // that path preserves the complete source color while still
                // evaluating its displacement map and UV animation.
                metalRoughTexture = CachedTextureRgba{};
                metalRoughTexture.width = 1;
                metalRoughTexture.height = 1;
                metalRoughTexture.wrapS = 10497;
                metalRoughTexture.wrapT = 10497;
                metalRoughTexture.minF = 9729;
                metalRoughTexture.magF = 9729;
                metalRoughTexture.rgba = {0u, 0u, 0u, 255u};
            }
            if (nativePlaFlatAnimatedEye && clipBoundEyeUv) {
                // PLA supplies these qualified eyes as flat expression atlases
                // on convex shells. Generic PBR relights the shell into a
                // radial silver coin even with its projected normal disabled.
                // Carry the complete animated atlas as emission over a black
                // carrier: pupil texels remain black, sclera texels retain
                // their authored value, and the existing per-clip selector
                // still addresses every expression cell.
                CachedTextureRgba resolvedEyeAtlas =
                    std::move(baseTexture);
                baseTexture = CachedTextureRgba{};
                // The shared world shader derives clamp-to-edge texel bounds
                // from the base texture. Keep this black carrier at the eye
                // atlas dimensions so the animated emissive lookup retains
                // its full UV domain instead of collapsing to the center of
                // a 1x1 base map.
                baseTexture.width = resolvedEyeAtlas.width;
                baseTexture.height = resolvedEyeAtlas.height;
                baseTexture.wrapS = resolvedEyeAtlas.wrapS;
                baseTexture.wrapT = resolvedEyeAtlas.wrapT;
                baseTexture.minF = resolvedEyeAtlas.minF;
                baseTexture.magF = resolvedEyeAtlas.magF;
                baseTexture.rgba.resize(
                    static_cast<std::size_t>(baseTexture.width) *
                        static_cast<std::size_t>(baseTexture.height) * 4u,
                    0u);
                for (std::size_t pixel = 3u;
                     pixel < baseTexture.rgba.size();
                     pixel += 4u) {
                    baseTexture.rgba[pixel] = 255u;
                }
                // A black dielectric still contributes the renderer's 4%
                // Fresnel response, which washes the atlas's black pupil to
                // gray. Treat the black carrier as a fully rough black metal:
                // both its diffuse and specular lobes become zero, leaving
                // the authored atlas as the sole visible eye response.
                metalRoughTexture = CachedTextureRgba{};
                metalRoughTexture.width = 1;
                metalRoughTexture.height = 1;
                metalRoughTexture.wrapS = 33071;
                metalRoughTexture.wrapT = 33071;
                metalRoughTexture.minF = 9729;
                metalRoughTexture.magF = 9729;
                metalRoughTexture.rgba = {255u, 255u, 255u, 255u};
                layeredMetalRoughBaked = true;
                emissiveTexture = std::move(resolvedEyeAtlas);
                layeredEmissionBaked = true;
            }
            if (!nativeUnlitDisplaced && !clipBoundEyeUv &&
                !nativeIkCharacterLighting &&
                !nativeIkCharacterEyeLighting) {
                // IkCharacter's albedo, layer, and AO families share
                // UVScaleOffset. Normal maps intentionally use the separate
                // UVScaleOffsetNormal parameter. Layered albedo and material
                // properties were sampled with the base transform above. Do
                // the same for standalone AO; mode 32 already baked its AO
                // and surface controls through that transform. Mode 35 packs
                // the same controls plus its live eye inputs.
                bakeStaticUvTransform(
                    material,
                    "UVScaleOffset",
                    occlusionTexture);
            }
            out.submeshBaseColors.push_back(glm::vec4(1.0f));
            out.submeshMeshIndex.push_back(static_cast<int>(submeshIndex));
            out.submeshIndexOffset.push_back(static_cast<std::uint32_t>(indexOffset));
            out.submeshIndexCount.push_back(
                static_cast<std::uint32_t>(indexCount));
            out.submeshBaseTextures.push_back(std::move(baseTexture));
            out.submeshNormalTextures.push_back(std::move(normalTexture));
            out.submeshMetallicRoughnessTextures.push_back(std::move(metalRoughTexture));
            out.submeshOcclusionTextures.push_back(std::move(occlusionTexture));
            out.submeshEmissiveTextures.push_back(std::move(emissiveTexture));
            out.submeshEnvironmentTextures.push_back(
                std::move(environmentTexture));
            const std::string alphaMode = nativeSssEffectDisplaced(material)
                ? "blend"
                : nativeLgpeLayered
                    ? "opaque"
                    : (nativeTransparentLayer && layeredEmissionBaked)
                        ? "blend"
                        : translation.value("alpha_mode", "opaque");
            out.submeshAlphaMode.push_back(
                alphaMode == "blend" ? 2u : alphaMode == "mask" ? 1u : 0u);
            out.submeshAlphaCutoff.push_back(translation.value("alpha_cutoff", 0.5f));
            float nativeHalfLambertBias = 0.0f;
            float nativeShadowStrength = 0.7f;
            float nativeRimOffset = 0.0f;
            float nativeRimContrast = 1.0f;
            float nativeReflectionsBlur = 0.0f;
            float nativeDiffusionLevels = 0.0f;
            float nativeShadowingGiGain = 0.5f;
            float nativeShadowingBias = 1.0f;
            float nativeShadowingShift = -0.5f;
            float nativeShadowingContrast = 0.0f;
            float nativeHueShiftBias = 0.6f;
            float nativeMidAreaShift = 0.1f;
            float nativeMidAreaContrast = 0.1f;
            float nativeMidAreaHueOffset = 0.0f;
            float nativeDarkAreaShift = -0.1f;
            float nativeDarkAreaContrast = 0.1f;
            float nativeDarkAreaHueOffset = 360.0f;
            float nativeHueShiftAreaValue = 0.0f;
            float nativeOcclusionStrength =
                translation.value("occlusion_strength", 1.0f);
            float nativeEyeParallaxHeight = 0.0f;
            float nativeEyeParallaxIor = 1.0f;
            glm::vec4 nativeEyeEyelidColor(1.0f);
            const bool nativeIkCharacterSurface =
                nativeIkCharacterLighting || nativeIkCharacterEyeLighting;
            (void)floatParameter(
                material,
                "HalfLambertBias",
                nativeHalfLambertBias);
            (void)floatParameter(
                material,
                "ShadowStrength",
                nativeShadowStrength);
            (void)floatParameter(
                material,
                "RimLightOffset",
                nativeRimOffset);
            (void)floatParameter(
                material,
                "RimLightContrast",
                nativeRimContrast);
            (void)floatParameter(
                material,
                "ReflectionsBlur",
                nativeReflectionsBlur);
            (void)floatParameter(
                material,
                "DiffusionLevels",
                nativeDiffusionLevels);
            if (nativeIkCharacterSurface) {
                (void)floatParameter(
                    material,
                    "ShadowingGIGain",
                    nativeShadowingGiGain);
                (void)floatParameter(
                    material,
                    "OcclusionStrength",
                    nativeOcclusionStrength);
                (void)floatParameter(
                    material,
                    "ShadowingBias",
                    nativeShadowingBias);
                (void)floatParameter(
                    material,
                    "ShadowingShift",
                    nativeShadowingShift);
                (void)floatParameter(
                    material,
                    "ShadowingContrast",
                    nativeShadowingContrast);
                (void)floatParameter(
                    material,
                    "HueShiftBias",
                    nativeHueShiftBias);
                (void)floatParameter(
                    material,
                    "MidAreaShift",
                    nativeMidAreaShift);
                (void)floatParameter(
                    material,
                    "MidAreaContrast",
                    nativeMidAreaContrast);
                (void)floatParameter(
                    material,
                    "MidAreaHueOffset",
                    nativeMidAreaHueOffset);
                (void)floatParameter(
                    material,
                    "DarkAreaShift",
                    nativeDarkAreaShift);
                (void)floatParameter(
                    material,
                    "DarkAreaContrast",
                    nativeDarkAreaContrast);
                (void)floatParameter(
                    material,
                    "DarkAreaHueOffset",
                    nativeDarkAreaHueOffset);
                (void)floatParameter(
                    material,
                    "HueShiftAreaValue",
                    nativeHueShiftAreaValue);
            }
            if (nativeIkCharacterEyeLighting) {
                (void)floatParameter(
                    material,
                    "ParallaxHeight",
                    nativeEyeParallaxHeight);
                (void)floatParameter(
                    material,
                    "ParallaxIOR",
                    nativeEyeParallaxIor);
                (void)vec4Parameter(
                    material,
                    "BaseColorLayer6",
                    nativeEyeEyelidColor);
            }
            out.submeshNormalScale.push_back(
                nativePlaFlatAnimatedEye
                    ? 0.0f
                    : nativeGolduckModel && nativeScarletAccessory &&
                         material.value("name", std::string{}) == "body_c"
                    ? 0.0f
                    : sourceNormalScale);
            out.submeshMetallicFactor.push_back(
                nativeIkCharacterSurface
                    ? glm::clamp(nativeHalfLambertBias, 0.0f, 1.0f)
                    : layeredMetalRoughBaked ? 1.0f : sourceMetallicFactor);
            out.submeshRoughnessFactor.push_back(
                nativeIkCharacterSurface
                    ? glm::clamp(nativeShadowStrength, 0.0f, 1.0f)
                    : layeredMetalRoughBaked ? 1.0f : sourceRoughnessFactor);
            out.submeshOcclusionStrength.push_back(
                nativeIkCharacterSurface
                    ? std::max(nativeOcclusionStrength, 0.0f)
                    : glm::clamp(nativeOcclusionStrength, 0.0f, 1.0f));
            out.submeshEmissiveFactors.push_back(
                nativeScarletSss
                    ? [&]() {
                          glm::vec4 subsurfaceColor(0.2f);
                          (void)vec4Parameter(
                              material,
                              "SubsurfaceColor",
                              subsurfaceColor);
                          return glm::max(
                              glm::vec3(subsurfaceColor),
                              glm::vec3(0.0f));
                      }()
                    : nativeIkCharacterEyeLighting
                    ? glm::max(
                          glm::vec3(nativeEyeEyelidColor),
                          glm::vec3(0.0f))
                    : nativeIkCharacterLighting
                    ? glm::vec3(
                          std::max(0.0f, nativeRimOffset),
                          std::max(1.0f, nativeRimContrast),
                          1.0f)
                    : layeredEmissionBaked
                    ? glm::vec3(1.0f)
                    : glm::vec3(0.0f));
            float displacementHeight = 0.0f;
            float emissionIntensity = 1.0f;
            float specularIntensity = 0.04f;
            glm::vec4 displacementUvTransform(1.0f, 1.0f, 0.0f, 0.0f);
            glm::vec4 layeredBaseColor1(1.0f);
            glm::vec4 layeredBaseColor2(1.0f);
            glm::vec4 eyeUvTransform(1.0f, 1.0f, 0.0f, 0.0f);
            const bool hasExactContinuousMaterialTrack =
                nativeUnlitDisplaced &&
                appendNativeContinuousMaterialTracks(
                    animationRecords,
                    material,
                    submeshIndex,
                    submeshName,
                    out);
            const glm::vec2 continuousUvLoopRates =
                nativeUnlitDisplaced
                    ? nativeContinuousUvLoopRates(
                          animationRecords,
                          material,
                          submeshName)
                    : glm::vec2(0.0f);
            (void)floatParameter(
                material,
                "DisplacementHeight",
                displacementHeight);
            (void)floatParameter(
                material,
                "EmissionIntensity",
                emissionIntensity);
            if (nativeIkCharacterSpecularStrength) {
                (void)floatParameter(
                    material,
                    "SpecularIntensity",
                    specularIntensity);
            }
            (void)vec4Parameter(
                material,
                "UVScaleOffset3",
                displacementUvTransform);
            (void)vec4Parameter(
                material,
                "BaseColorLayer1",
                layeredBaseColor1);
            (void)vec4Parameter(
                material,
                "BaseColorLayer2",
                layeredBaseColor2);
            if (!clipBoundEyeUv ||
                !vec4Parameter(
                    material,
                    materialClipBoundEyeUvParameter[materialIndex],
                    eyeUvTransform)) {
                (void)vec4Parameter(
                    material,
                    "UVScaleOffset",
                    eyeUvTransform);
            }
            normalizePlaMagnetEyeAtlasScale(
                material,
                eyeUvTransform);
            if (clipBoundEyeUv && nativeScarletSource) {
                eyeUvTransform =
                    nativeScarletEyeUvToRuntime(eyeUvTransform);
            }
            float clearCoatRoughness = 0.2f;
            float clearCoatMetallic = 0.0f;
            float highlightRoughness = 0.51f;
            float highlightMetallic = 1.0f;
            float highlightEmissionIntensity = 0.0f;
            glm::vec4 highlightEmissionColor(1.0f);
            // PLA's Eye family carries its white glint in HighlightMaskMap
            // and layer 5. It does not author the clear-coat parameters used
            // by Scarlet's separate EyeClearCoat family, so defaulting the
            // missing coat to opaque made the eye silver from oblique views.
            glm::vec4 clearCoatBaseColor = nativePlainEye
                ? glm::vec4(0.0f)
                : glm::vec4(1.0f);
            (void)floatParameter(
                material,
                "RoughnessClearCoat",
                clearCoatRoughness);
            (void)floatParameter(
                material,
                "MetallicClearCoat",
                clearCoatMetallic);
            (void)floatParameter(
                material,
                "RoughnessHighlight",
                highlightRoughness);
            (void)floatParameter(
                material,
                "MetallicHighlight",
                highlightMetallic);
            (void)floatParameter(
                material,
                "EmissionIntensityLayer5",
                highlightEmissionIntensity);
            (void)vec4Parameter(
                material,
                "BaseColorClearCoat",
                clearCoatBaseColor);
            (void)vec4Parameter(
                material,
                "EmissionColorLayer5",
                highlightEmissionColor);
            const glm::vec3 packedHighlightEmission = glm::max(
                glm::vec3(highlightEmissionColor) *
                    std::max(0.0f, highlightEmissionIntensity),
                glm::vec3(0.0f));
            glm::vec4 fresnelBaseColor(1.0f);
            glm::vec4 fresnelLayerColor(1.0f);
            float fresnelLocalProbeIntensity = 0.0f;
            float fresnelAlphaMin = 0.0f;
            float fresnelAlphaMax = 1.0f;
            float fresnelAngleBias = 0.0f;
            float fresnelBaseSaturation = 1.0f;
            float fresnelLayerScale = 1.0f;
            float fresnelNormalHeight1 = 0.0f;
            if (nativeFresnelEffect) {
                (void)vec4Parameter(
                    material,
                    "BaseColor",
                    fresnelBaseColor);
                (void)vec4Parameter(
                    material,
                    "BaseColorLayer1",
                    fresnelLayerColor);
                (void)floatParameter(
                    material,
                    "LocalSpecularProbeIntensity",
                    fresnelLocalProbeIntensity);
                (void)floatParameter(
                    material,
                    "FresnelAlphaMin",
                    fresnelAlphaMin);
                (void)floatParameter(
                    material,
                    "FresnelAlphaMax",
                    fresnelAlphaMax);
                (void)floatParameter(
                    material,
                    "FresnelAngleBias",
                    fresnelAngleBias);
                (void)floatParameter(
                    material,
                    "BaseColorMapSaturation",
                    fresnelBaseSaturation);
                (void)floatParameter(
                    material,
                    "LayerMaskScale1",
                    fresnelLayerScale);
                (void)floatParameter(
                    material,
                    "NormalHeight1",
                    fresnelNormalHeight1);
            }
            if (nativeTransparentEyeLens &&
                !floatParameter(
                    material,
                    "RoughnessClearCoat",
                    clearCoatRoughness)) {
                clearCoatRoughness = sourceRoughnessFactor;
            }
            out.submeshMaterialModes.push_back(
                nativeFresnelEffect
                    ? game::runtime::render_model::
                          kNativeFresnelEffectMaterialMode
                : nativeScarletSss
                    ? game::runtime::render_model::
                          kNativeSssMaterialMode
                    : nativeIkCharacterEyeLighting
                    ? game::runtime::render_model::
                          kNativeIkCharacterEyeMaterialMode
                    : nativeIkCharacterLighting
                    ? game::runtime::render_model::
                          kNativeIkCharacterMaterialMode
                    : nativeUnlitDisplaced
                    ? game::runtime::render_model::
                          kNativeLayeredUnlitMaterialMode
                    : (nativeGastlyFace || nativeGastlyEye)
                        ? game::runtime::render_model::
                              kNativeFacialOverlayMaterialMode
                    : clipBoundEyeUv
                        ? (nativeEyeSurface && !nativePlaFlatAnimatedEye
                               ? game::runtime::render_model::
                                     kNativeAnimatedEyeClearCoatMaterialMode
                               : game::runtime::render_model::
                                     kNativeAnimatedEyeMaterialMode)
                    : nativeEyeSurface
                        ? game::runtime::render_model::
                              kNativeEyeClearCoatMaterialMode
                        : 2u);
            out.submeshMaterialFlags.push_back(
                nativeScarletSss
                    ? (nativeScarletSssFibre
                           ? game::runtime::render_model::
                                 kNativeSssSurfaceFibre
                           : game::runtime::render_model::
                                 kNativeSssSurfaceDefault)
                : nativeIkCharacterSurface
                    ? 0.0f
                    : nativeUnlitDisplaced
                    // Lit native smoke uses the displaced material transport,
                    // but unlike authored Unlit flame it receives the native
                    // half-Lambert/rim response. Flag 3 preserves exact UV
                    // controller sampling while selecting that response in
                    // every backend. Scarlet's NonDirectional smoke uses the
                    // same animated and lit response, but its source mesh has
                    // zero vertex alpha and instead remains opaque. Flag 3.25
                    // distinguishes that coverage contract without changing
                    // the baked regular/shiny palette.
                    ? (nativeLitDisplaced
                           ? (nativeScarletGastlySmoke ? 3.25f : 3.0f)
                           : (hasExactContinuousMaterialTrack ? 2.0f : 1.0f))
                    : nativeGastlyFace
                        ? 4.0f
                    : nativeScarletGastlyEye
                        ? static_cast<float>(
                              game::runtime::render_model::
                                  kNativeFrontFacingOnlyMaterialFlagBit)
                        : nativeIkCharacterSpecularStrength
                            ? game::runtime::render_model::
                                  kNativeSpecularStrengthMaterialFlag
                            : 0.0f);
            out.submeshMaterialParams0.push_back(
                nativeFresnelEffect
                    ? fresnelBaseColor
                : nativeIkCharacterEyeLighting
                    ? glm::vec4(
                          std::max(0.0f, nativeReflectionsBlur),
                          std::max(0.0f, nativeEyeParallaxHeight),
                          std::max(1.0f, nativeEyeParallaxIor),
                          glm::clamp(nativeShadowingGiGain, 0.0f, 1.0f))
                : nativeIkCharacterLighting
                    // The decompiled Z-A IkCharacter program supplies no
                    // roughness parameter. It samples a local reflection cube
                    // with authored ReflectionsBlur and carries a separate
                    // diffusion control. Keep those source values verbatim;
                    // z is reserved and stays neutral because every selected
                    // Kanto Z-A material disables EnableHairSpecular; w carries
                    // Z-A's ShadowingGIGain. The latter controls how strongly
                    // AO steers the authored ambient/shadow-color response; it
                    // is not a generic albedo multiplier.
                    ? glm::vec4(
                          std::max(0.0f, nativeReflectionsBlur),
                          std::max(0.0f, nativeDiffusionLevels),
                          game::runtime::render_model::
                              kNativeIkCharacterSurfaceDefault,
                          glm::clamp(nativeShadowingGiGain, 0.0f, 1.0f))
                    : nativeUnlitDisplaced
                    ? glm::vec4(
                          std::max(0.0f, displacementHeight),
                          nativeLitDisplaced
                              ? 1.0f
                              : std::max(0.0f, emissionIntensity),
                          continuousUvLoopRates.x,
                          continuousUvLoopRates.y)
                    : (nativeGastlyFace || nativeGastlyEye)
                        ? glm::vec4(
                              nativeGastlyEye ? 0.022f : 0.020f,
                              0.0f,
                              0.0f,
                              0.0f)
                    : nativeEyeSurface
                        ? glm::vec4(
                              glm::clamp(clearCoatRoughness, 0.02f, 1.0f),
                              glm::clamp(highlightRoughness, 0.02f, 1.0f),
                              glm::clamp(highlightMetallic, 0.0f, 1.0f),
                              shaderOptionEnabled(material, "EnableHighlight")
                                  ? 1.0f
                                  : 0.0f)
                        : nativeIkCharacterSpecularStrength
                            ? glm::vec4(
                                  glm::clamp(specularIntensity, 0.0f, 1.0f),
                                  0.0f,
                                  0.0f,
                                  0.0f)
                            : glm::vec4(0.0f));
            out.submeshMaterialParams1.push_back(
                nativeFresnelEffect
                    ? fresnelLayerColor
                : nativeIkCharacterSurface
                    // Z-A's color-process block is distinct from AO and the
                    // layer-resolved shadow-color texture. Preserve its
                    // authored lighting-domain controls verbatim so all three
                    // backends can shape the same source response.
                    ? glm::vec4(
                          nativeShadowingBias,
                          nativeShadowingShift,
                          nativeShadowingContrast,
                          nativeHueShiftBias)
                    : nativeUnlitDisplaced
                    ? displacementUvTransform
                    : nativeEyeSurface
                        // A negative metallic value remains the internal
                        // marker for PLA's plain Eye family, which has no
                        // clear-coat lobe. Scarlet EyeClearCoat keeps the
                        // complete authored RGB/F0 inputs; BaseColorClearCoat
                        // alpha is a source output-alpha term, not coat
                        // coverage.
                        ? glm::vec4(
                              glm::vec3(clearCoatBaseColor),
                              nativePlainEye
                                  ? -1.0f
                                  : glm::clamp(
                                        clearCoatMetallic,
                                        0.0f,
                                        1.0f))
                        : glm::vec4(0.0f));
            out.submeshMaterialParams2.push_back(
                nativeFresnelEffect
                    ? glm::vec4(
                          std::max(0.0f, fresnelLocalProbeIntensity),
                          glm::clamp(fresnelAlphaMin, 0.0f, 1.0f),
                          glm::clamp(fresnelAlphaMax, 0.0f, 1.0f),
                          glm::clamp(fresnelAngleBias, 0.0f, 1.0f))
                : nativeIkCharacterSurface
                    ? glm::vec4(
                          nativeMidAreaShift,
                          nativeMidAreaContrast,
                          nativeMidAreaHueOffset / 360.0f,
                          nativeDarkAreaShift)
                    : nativeUnlitDisplaced
                    ? layeredBaseColor1
                    : clipBoundEyeUv
                        ? eyeUvTransform
                        : glm::vec4(0.0f));
            out.submeshMaterialParams3.push_back(
                nativeFresnelEffect
                    ? glm::vec4(
                          std::max(0.0f, fresnelBaseSaturation),
                          std::max(0.0f, fresnelLayerScale),
                          0.0f,
                          std::max(0.0f, fresnelNormalHeight1))
                : nativeIkCharacterSurface
                    // z remains reserved for the runtime texture-detail LOD
                    // bias. The color-process block needs the other lanes only.
                    ? glm::vec4(
                          nativeDarkAreaContrast,
                          nativeDarkAreaHueOffset / 360.0f,
                          0.0f,
                          nativeHueShiftAreaValue)
                    : nativeUnlitDisplaced
                    ? layeredBaseColor2
                    : (nativeScarletGastlyFace || nativeScarletGastlyEye)
                        ? glm::vec4(
                              nativeScarletGastlyEye ? 0.022f : 0.020f,
                              nativeScarletGastlyEye
                                  ? packedHighlightEmission.x
                                  : 0.0f,
                              nativeScarletGastlyEye
                                  ? packedHighlightEmission.y
                                  : 0.0f,
                              nativeScarletGastlyEye
                                  ? packedHighlightEmission.z
                                  : 0.0f)
                    : nativeEyeSurface
                        // x remains reserved for the qualified Gastly
                        // face/smoke depth ordering above. YZW carry the
                        // authored layer-5 emission already multiplied by its
                        // intensity, leaving the runtime shader to isolate the
                        // still-unknown scene-light contribution.
                        ? glm::vec4(
                              0.0f,
                              packedHighlightEmission.x,
                              packedHighlightEmission.y,
                              packedHighlightEmission.z)
                    : glm::vec4(0.0f));
        }

        // Preserve native COLOR_0 values in each MeshVertex, but only feed
        // them into base-color shading when the source material explicitly
        // enables that channel. Scarlet's SSS Pokemon materials use COLOR_0
        // as auxiliary evidence; multiplying it into the albedo washed out
        // and spatially distorted the authored body texture.
        out.hasVertexColor = false;
        for (std::size_t vertexIndex = 0u;
             vertexIndex < out.vertices.size() &&
             vertexIndex < vertexColorEnabled.size();
             ++vertexIndex) {
            if (vertexColorEnabled[vertexIndex] == 0u) continue;
            if (glm::any(glm::greaterThan(
                    glm::abs(
                        glm::vec3(out.vertices[vertexIndex].color) -
                        glm::vec3(1.0f)),
                    glm::vec3(0.001f)))) {
                out.hasVertexColor = true;
                break;
            }
        }
        out.vertexBaseColors.reserve(out.vertices.size());
        for (std::size_t vertexIndex = 0u;
             vertexIndex < out.vertices.size();
             ++vertexIndex) {
            const MeshVertex& vertex = out.vertices[vertexIndex];
            out.vertexBaseColors.push_back(
                vertexIndex < vertexColorEnabled.size() &&
                        vertexColorEnabled[vertexIndex] != 0u
                    ? glm::clamp(
                          glm::vec3(vertex.color),
                          0.0f,
                          1.0f)
                    : glm::vec3(1.0f));
        }
        out.hasVertexBaseColor = out.hasVertexColor;
        const std::size_t triangleCount = out.indices.size() / 3u;
        out.triangleSubmesh.assign(triangleCount, 0u);
        out.triangleBaseColors.assign(triangleCount, glm::vec3(1.0f));
        out.triangleOpacity.assign(triangleCount, 1.0f);
        out.triangleDoubleSided.assign(triangleCount, 1u);
        out.triangleNodeIndex.assign(triangleCount, -1);
        out.triangleSkinIndex.assign(triangleCount, -1);
        for (std::size_t submeshIndex = 0u;
             submeshIndex < submeshCount;
             ++submeshIndex) {
            const std::size_t first =
                out.submeshIndexOffset[submeshIndex] / 3u;
            const std::size_t count =
                out.submeshIndexCount[submeshIndex] / 3u;
            for (std::size_t triangle = first;
                 triangle < std::min(triangleCount, first + count);
                 ++triangle) {
                out.triangleSubmesh[triangle] =
                    static_cast<std::uint16_t>(submeshIndex);
                out.triangleDoubleSided[triangle] =
                    submeshDoubleSided[submeshIndex];
                out.triangleNodeIndex[triangle] =
                    out.meshIndexToNode[submeshIndex];
                out.triangleSkinIndex[triangle] =
                    out.nodeSkin[static_cast<std::size_t>(
                        out.meshIndexToNode[submeshIndex])];
            }
        }

        const glm::vec3 extent = out.boundsMax - out.boundsMin;
        float longest = std::max({extent.x, extent.y, extent.z});
        if (longest < 1e-6f) longest = 1.0f;
        out.modelScaleFactor = 0.8f / longest;
        buildGlobals(out);

        out.animations.reserve(animationRecords.size());
        out.animationMeshVisibility.reserve(animationRecords.size());
        out.animationMaterialParameters.reserve(animationRecords.size());
        for (const auto& animation : animationRecords) {
            // Material-only loop01 controllers are harvested into
            // continuousMaterialAnimations above and must not appear as
            // standalone body clips: selecting one would expose raw bind-pose
            // parts such as Gastly's tongue. Some SV controllers also own an
            // intermittent mesh-visibility lifecycle and matching skeletal
            // motion, however. Weezing's 28201 loop is the canonical case: it
            // emits its side smoke puffs over otherwise smoke-free idle clips.
            // Retain those lifecycle-bearing controllers for the runtime
            // overlay path instead of silently discarding their geometry
            // animation.
            if (nativeContinuousMaterialController(animation) &&
                !nativeContinuousVisibilityController(animation)) {
                continue;
            }
            engine::render::model_types::AnimationClip clip;
            clip.name = animation.at("name").get<std::string>();
            clip.durationSec = animation.at("duration_seconds").get<float>();
            const std::uint32_t frameRate =
                animation.at("frame_rate").get<std::uint32_t>();
            if (frameRate == 0u) {
                return fail(
                    outError,
                    "Native model IR animation frame rate is zero.");
            }
            for (const auto& track : animation.at("tracks")) {
                const int targetBone = track.at("bone").get<int>();
                if (targetBone < 0 ||
                    targetBone >= static_cast<int>(boneCount)) {
                    return fail(
                        outError,
                        "Native model IR animation targets an invalid bone.");
                }
                const int targetNode = targetBone + 1;
                if (track.contains("translation") && !track.at("translation").is_null() &&
                    !addAnimationSampler(
                        track.at("translation"),
                        3u,
                        unitScaleToMeters,
                        engine::render::model_types::ChannelPath::Translation,
                        targetNode,
                        frameRate,
                        payload,
                        clip,
                        outError)) {
                    return false;
                }
                if (track.contains("rotation") && !track.at("rotation").is_null() &&
                    !addAnimationSampler(
                        track.at("rotation"),
                        4u,
                        1.0f,
                        engine::render::model_types::ChannelPath::Rotation,
                        targetNode,
                        frameRate,
                        payload,
                        clip,
                        outError)) {
                    return false;
                }
                if (track.contains("scale") && !track.at("scale").is_null() &&
                    !addAnimationSampler(
                        track.at("scale"),
                        3u,
                        1.0f,
                        engine::render::model_types::ChannelPath::Scale,
                        targetNode,
                        frameRate,
                        payload,
                        clip,
                        outError)) {
                    return false;
                }
            }
            std::vector<
                game::runtime::render_model::MeshVisibilityTrack>
                visibilityTracks;
            if (animation.contains("mesh_visibility") &&
                animation.at("mesh_visibility").is_array()) {
                for (const auto& visibility :
                     animation.at("mesh_visibility")) {
                    const std::string meshName =
                        visibility.at("mesh").get<std::string>();
                    auto keyFrames =
                        visibility.at("key_frames")
                            .get<std::vector<int>>();
                    auto values =
                        visibility.at("values")
                            .get<std::vector<bool>>();
                    nativeKoffingIdleSmokeVisibility(
                        animation,
                        meshName,
                        keyFrames,
                        values);
                    if (keyFrames.empty() ||
                        keyFrames.size() != values.size()) {
                        return fail(
                            outError,
                            "Native mesh visibility key/value counts are invalid.");
                    }
                    if (!std::is_sorted(
                            keyFrames.begin(),
                            keyFrames.end()) ||
                        keyFrames.front() < 0) {
                        return fail(
                            outError,
                            "Native mesh visibility keys are invalid.");
                    }
                    // Tracks that never hide a mesh are explicit source
                    // evidence but do not need a runtime channel.
                    if (std::all_of(
                            values.begin(),
                            values.end(),
                            [](bool value) { return value; })) {
                        continue;
                    }
                    bool matched = false;
                    for (std::size_t submeshIndex = 0u;
                         submeshIndex < submeshCount;
                         ++submeshIndex) {
                        const std::string submeshName =
                            submeshes[submeshIndex]
                                .value("name", std::string{});
                        const bool exact = submeshName == meshName;
                        const bool materialQualified =
                            submeshName.size() > meshName.size() &&
                            submeshName.compare(
                                0u,
                                meshName.size(),
                                meshName) == 0 &&
                            submeshName[meshName.size()] == ':';
                        if (!exact && !materialQualified) {
                            continue;
                        }
                        game::runtime::render_model::
                            MeshVisibilityTrack runtimeTrack;
                        runtimeTrack.nodeIndex = static_cast<int>(
                            1u + boneCount + submeshIndex);
                        runtimeTrack.sourceFrameRate =
                            static_cast<float>(frameRate);
                        runtimeTrack.inputs.reserve(
                            keyFrames.size());
                        runtimeTrack.values.reserve(
                            values.size());
                        for (std::size_t keyIndex = 0u;
                             keyIndex < keyFrames.size();
                             ++keyIndex) {
                            runtimeTrack.inputs.push_back(
                                static_cast<float>(
                                    keyFrames[keyIndex]) /
                                static_cast<float>(frameRate));
                            runtimeTrack.values.push_back(
                                values[keyIndex] ? 1u : 0u);
                        }
                        visibilityTracks.push_back(
                            std::move(runtimeTrack));
                        matched = true;
                    }
                    if (!matched) {
                        return fail(
                            outError,
                            "Native mesh visibility target does not match a submesh: " +
                                meshName);
                    }
                }
            }
            out.animations.push_back(std::move(clip));
            out.animationMeshVisibility.push_back(
                std::move(visibilityTracks));
            std::string materialTrackError;
            auto materialTracks = nativeClipBoundMaterialTracks(
                animation,
                materials,
                submeshes,
                materialClipBoundEyeUvParameter,
                nativeScarletSource,
                &materialTrackError);
            if (!materialTrackError.empty()) {
                return fail(outError, std::move(materialTrackError));
            }
            out.animationMaterialParameters.push_back(
                std::move(materialTracks));
        }
        out.assetCacheIdentity =
            "native-ir:" + fs::path(manifestPath).generic_string();
        return !out.vertices.empty() && !out.indices.empty();
    } catch (const std::exception& exception) {
        return fail(
            outError,
            "Could not decode native model IR: " +
                std::string(exception.what()));
    }
}

} // namespace tools::phlosion_native_model_ir
