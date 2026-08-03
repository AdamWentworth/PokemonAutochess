#include "PhlosionNativeModelIr.h"

#include "engine/render/ModelAnimationTypes.h"

#include <nlohmann/json.hpp>
#include <stb_image.h>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <array>
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
#include <utility>
#include <vector>

namespace {

namespace fs = std::filesystem;
using game::runtime::render_model::CachedTextureRgba;
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
        const glm::mat4 global =
            parent * trs(out.nodesDefault[static_cast<std::size_t>(nodeIndex)]);
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
    const auto pixel = [&](int px, int py) {
        px = std::clamp(px, 0, texture.width - 1);
        py = std::clamp(py, 0, texture.height - 1);
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

bool bakeLayeredBaseColor(
    const fs::path& root,
    const json& material,
    CachedTextureRgba& baseTexture,
    std::string* outError) {
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
    std::array<bool, 4u> hasLayer{};
    bool anyLayer = false;
    for (std::size_t layer = 0u; layer < layerColors.size(); ++layer) {
        hasLayer[layer] = vec4Parameter(
            material,
            "BaseColorLayer" + std::to_string(layer + 1u),
            layerColors[layer]);
        anyLayer = anyLayer || hasLayer[layer];
    }
    if (!anyLayer) return true;

    CachedTextureRgba baked;
    baked.width = layerMask.width;
    baked.height = layerMask.height;
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
            const glm::vec4 encodedBase = sampleTexture(
                baseTexture,
                u,
                v,
                glm::vec4(1.0f));
            const glm::vec4 mask = sampleTexture(
                layerMask,
                u,
                v,
                glm::vec4(0.0f));
            glm::vec3 color(
                srgbToLinear(encodedBase.r),
                srgbToLinear(encodedBase.g),
                srgbToLinear(encodedBase.b));
            for (std::size_t layer = 0u;
                 layer < layerColors.size();
                 ++layer) {
                if (!hasLayer[layer]) continue;
                color = glm::mix(
                    color,
                    glm::vec3(layerColors[layer]),
                    glm::clamp(
                        mask[static_cast<glm::length_t>(layer)],
                        0.0f,
                        1.0f));
            }
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

bool bakeLayeredNormal(
    const fs::path& root,
    const json& material,
    CachedTextureRgba& normalTexture,
    std::string* outError) {
    if (!normalTexture.hasPixels()) return true;
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

bool addAnimationSampler(
    const json& view,
    std::size_t components,
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
            values[offset + 0u],
            values[offset + 1u],
            values[offset + 2u],
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
        if (document.at("coordinate_system").value(
                "texcoords_0",
                std::string{}) != "gamefreak_native") {
            return fail(
                outError,
                "Native model IR must preserve Game Freak UV coordinates.");
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
                vec3(record.at("translation"));
            out.nodesDefault[static_cast<std::size_t>(node)].r =
                quat(record.at("rotation"));
            out.nodesDefault[static_cast<std::size_t>(node)].s =
                vec3(record.at("scale"));
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
            skin.inverseBind.push_back(
                glm::make_mat4(matrixValues.data()));
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
        const auto shaderOptionEnabled = [](
            const nlohmann::json& material,
            std::string_view name) {
            if (!material.contains("shader_options") ||
                !material.at("shader_options").is_object()) {
                return false;
            }
            const auto& options = material.at("shader_options");
            const auto option = options.find(std::string(name));
            if (option == options.end()) return false;
            if (option->is_boolean()) return option->get<bool>();
            const std::string text = option->is_string()
                ? option->get<std::string>()
                : option->dump();
            return text == "1" || text == "true" ||
                   text == "True";
        };
        out.meshIndexToNode.assign(submeshCount, -1);
        for (std::size_t submeshIndex = 0u;
             submeshIndex < submeshCount;
             ++submeshIndex) {
            const auto& record = submeshes[submeshIndex];
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
                    positions[p3 + 2u]);
                const glm::vec3 normal(
                    normals[p3 + 0u],
                    normals[p3 + 1u],
                    normals[p3 + 2u]);
                vertex.normal = glm::dot(normal, normal) > 1e-12f
                    ? glm::normalize(normal)
                    : glm::vec3(0.0f, 1.0f, 0.0f);
                vertex.uv = glm::vec2(
                    texcoords[p2 + 0u],
                    gameFreakNativeVToRuntime(
                        texcoords[p2 + 1u]));
                vertex.color = glm::vec4(
                    colors[p4 + 0u],
                    colors[p4 + 1u],
                    colors[p4 + 2u],
                    colors[p4 + 3u]);
                const glm::vec3 tangent(
                    tangents[p4 + 0u],
                    tangents[p4 + 1u],
                    tangents[p4 + 2u]);
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

            const std::size_t materialIndex =
                record.at("material").get<std::size_t>();
            if (materialIndex >= materials.size()) {
                return fail(outError, "Native model IR material index is invalid.");
            }
            const auto& material = materials[materialIndex];
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
            if (!loadTexture(root, material, "base_color_texture", baseTexture, outError) ||
                !loadTexture(root, material, "normal_texture", normalTexture, outError) ||
                !loadMetallicRoughness(root, material, metalRoughTexture, outError) ||
                !loadTexture(root, material, "occlusion_texture", occlusionTexture, outError) ||
                !loadTexture(root, material, "emissive_texture", emissiveTexture, outError) ||
                !bakeLayeredBaseColor(
                    root,
                    material,
                    baseTexture,
                    outError) ||
                !bakeLayeredNormal(
                    root,
                    material,
                    normalTexture,
                    outError)) {
                return false;
            }
            out.submeshBaseColors.push_back(glm::vec4(1.0f));
            out.submeshMeshIndex.push_back(static_cast<int>(submeshIndex));
            out.submeshIndexOffset.push_back(static_cast<std::uint32_t>(indexOffset));
            out.submeshIndexCount.push_back(static_cast<std::uint32_t>(indexCount));
            out.submeshBaseTextures.push_back(std::move(baseTexture));
            out.submeshNormalTextures.push_back(std::move(normalTexture));
            out.submeshMetallicRoughnessTextures.push_back(std::move(metalRoughTexture));
            out.submeshOcclusionTextures.push_back(std::move(occlusionTexture));
            out.submeshEmissiveTextures.push_back(std::move(emissiveTexture));
            const std::string alphaMode = translation.value("alpha_mode", "opaque");
            out.submeshAlphaMode.push_back(
                alphaMode == "blend" ? 2u : alphaMode == "mask" ? 1u : 0u);
            out.submeshAlphaCutoff.push_back(translation.value("alpha_cutoff", 0.5f));
            out.submeshNormalScale.push_back(translation.value("normal_scale", 1.0f));
            out.submeshMetallicFactor.push_back(translation.value("metallic_factor", 0.0f));
            out.submeshRoughnessFactor.push_back(translation.value("roughness_factor", 1.0f));
            out.submeshOcclusionStrength.push_back(translation.value("occlusion_strength", 1.0f));
            out.submeshEmissiveFactors.push_back(glm::vec3(0.0f));
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
        for (const auto& animation : animationRecords) {
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
                    const auto keyFrames =
                        visibility.at("key_frames")
                            .get<std::vector<int>>();
                    const auto values =
                        visibility.at("values")
                            .get<std::vector<bool>>();
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
