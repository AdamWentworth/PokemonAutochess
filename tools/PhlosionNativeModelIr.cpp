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

bool nativeLayeredUnlitDisplaced(const json& material) {
    if (material.value("shader_family", std::string{}) != "Unlit" ||
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

bool nativeScarletEyeClearCoat(const json& material) {
    return material.value("shader_family", std::string{}) ==
        "EyeClearCoat";
}

bool nativeLgpeLayeredColor(const json& material) {
    return material.value("shader_family", std::string{}) ==
               "PokeDefaultShader" &&
           shaderOptionEnabled(material, "Layer1Enable") &&
           hasTextureRole(material, "Col0Tex") &&
           hasTextureRole(material, "LyCol0Tex");
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

glm::vec2 nativeContinuousUvLoopRates(
    const json& animationRecords,
    const json& material) {
    const std::string materialName =
        material.value("name", std::string{});
    for (const auto& animation : animationRecords) {
        const std::string animationName =
            animation.value("name", std::string{});
        // Scarlet's animation controller enables the numbered loop01 channel
        // continuously and layers it over the selected body animation. For
        // Charmander this channel owns the two fire UV tracks; treating it as
        // an ordinary mutually-exclusive skeletal clip freezes the flame.
        if (!animation.value("loop", false) ||
            animationName.find("_08201_loop01_loop") ==
                std::string::npos) {
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
            if (track.value("material", std::string{}) != materialName) {
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
    MeshData& out) {
    const std::string materialName =
        material.value("name", std::string{});
    constexpr std::array<const char*, 4u> kComponents{
        "x", "y", "z", "w"};

    for (const auto& animation : animationRecords) {
        const std::string animationName =
            animation.value("name", std::string{});
        if (!animation.value("loop", false) ||
            animationName.find("_08201_loop01_loop") ==
                std::string::npos) {
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
                materialName) {
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
    float v) {
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
    // PLA uses two Standard variants behind the same exported shader family
    // and EnableLerpBaseColorEmission option. Ponyta's ordinary 1:1 atlas
    // treats red as authored BaseColorMap coverage, while Paras's mirrored
    // two-wide atlas stores its orange body in literal Layer1. Preserve the
    // source distinction instead of dropping Layer1 for every material that
    // happens to expose the shared option.
    const bool redChannelSelectsBaseColor =
        lerpBaseColorEmission &&
        std::abs(baseUvScaleOffset.x - 1.0f) < 1e-6f;
    // Scarlet/Violet SSS materials keep fur/skin markings and fine detail in
    // BaseColorMap, then use the material layers as tints. Replacing the
    // sampled albedo with a flat layer color turns Pikachu's red cheeks white
    // (its green mask selects a literal white multiplier). Z-A advertises the
    // same operation with BaseColorMultiply; Scarlet's SSS family implies it.
    const bool multiplyBaseColor =
        shaderFamily == "SSS" ||
        shaderOptionEnabled(material, "BaseColorMultiply");
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
                v);
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
            float layerWeightSum = 0.0f;
            std::array<float, 4u> layerWeights{};
            for (std::size_t layer = 0u;
                 layer < layerColors.size();
                 ++layer) {
                if (!hasLayer[layer]) continue;
                // ha_standard variations 259/265 differ only by
                // EnableLerpBaseColorEmission. In that Standard/Unlit path
                // the red channel is the authored base-color selector, not
                // an ordinary Layer1 tint. PLA Ponyta uses red over its
                // entire pale coat, then green/blue islands for the
                // separately colored hooves and small details. Z-A's
                // IkCharacter materials also expose the option, but their red
                // channel is a real Layer1 selector (for example Kakuna's
                // yellow body), so do not apply this exception to them.
                if (redChannelSelectsBaseColor && layer == 0u) continue;
                layerWeights[layer] = glm::clamp(
                    mask[static_cast<glm::length_t>(layer)] *
                        std::max(0.0f, layerScales[layer]),
                    0.0f,
                    1.0f);
                layerWeightSum += layerWeights[layer];
            }
            // This is the exact over-compositing sequence emitted by
            // Scarlet's Unlit variation 48: the base starts with the
            // unclaimed mask coverage, each RGBA mask layer is composited in
            // order, then the premultiplied result is divided by its final
            // coverage.  A fully opaque base followed by ordinary lerps is
            // not equivalent in the red/green overlap of Charmander's fire.
            float coverage = glm::clamp(
                1.0f - layerWeightSum,
                0.0f,
                1.0f);
            glm::vec3 color = baseColor * coverage;
            for (std::size_t layer = 0u;
                 layer < layerColors.size();
                 ++layer) {
                if (!hasLayer[layer]) continue;
                glm::vec3 resolvedLayerColor(
                    layerColors[layer]);
                if (multiplyBaseColor && baseTexture.hasPixels()) {
                    // Z-A IkCharacter materials retain line work, subtle
                    // shading, and other authored detail in BaseColorMap.
                    // Their material-layer colors tint that map; replacing it
                    // with a flat layer color erased Beedrill's wing veins.
                    resolvedLayerColor *= baseColor;
                }
                color = glm::mix(
                    color,
                    resolvedLayerColor,
                    layerWeights[layer]);
                coverage += layerWeights[layer] * (1.0f - coverage);
            }
            color /= std::max(coverage, 1e-6f);
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
            const float weight = sampleTexture(
                highlightMask,
                u,
                v,
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

bool bakeScarletEyeFinalColor(
    const fs::path& root,
    const json& material,
    const glm::vec2& highlightCenter,
    CachedTextureRgba& baseTexture,
    std::string* outError) {
    if (!nativeScarletEyeClearCoat(material) ||
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

    // The same-source Pikachu GLB resolves EyeClearCoat to an EyeFinal color
    // texture containing a stable circular catchlight, rather than a live
    // camera-relative specular lobe. Its 0.131 radius is retained here while
    // the center is derived from each eye's authored pointlight bone below.
    constexpr float kHighlightRadius = 0.131f;
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
        0.75f / static_cast<float>(std::max(baked.width, baked.height)),
        kHighlightRadius * 0.05f);
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
            const float distance = glm::length(
                glm::vec2(u, v) - highlightCenter);
            const float weight = 1.0f - glm::smoothstep(
                kHighlightRadius - antialiasWidth,
                kHighlightRadius + antialiasWidth,
                distance);
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
        // EyeClearCoat NormalMap1 builds EyeFinal's catchlight. The known-good
        // GLB keeps NormalMap as the sole runtime surface normal.
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

bool bakeLayeredMetallicRoughness(
    const fs::path& root,
    const json& material,
    float baseMetallicFactor,
    float baseRoughnessFactor,
    CachedTextureRgba& metalRoughTexture,
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
                v);
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
                v);
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
        std::vector<std::size_t> materialUseCounts(materials.size(), 0u);
        std::vector<bool> materialUsesEyeAShell(
            materials.size(),
            false);
        std::vector<bool> materialUsesEyeBShell(
            materials.size(),
            false);
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
                    positions[p3 + 2u]) * unitScaleToMeters;
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
            const bool nativeUnlitDisplaced =
                nativeLayeredUnlitDisplaced(material);
            const bool nativeEye = nativeEyeClearCoat(material);
            const bool nativeScarletEye =
                nativeScarletEyeClearCoat(material);
            const bool nativePlainEye =
                material.value("shader_family", std::string{}) == "Eye";
            const std::string submeshName =
                record.value("name", std::string{});
            // Paras and related PLA models construct each eye from an inner
            // pupil and outer iris shell that intentionally share one Eye
            // material. A single-shell Eye (Golbat, Parasect) stays opaque;
            // only a material referenced by both eye_a and eye_b is layered.
            // Venomoth has separate left/right eye_a meshes sharing a material,
            // so treating every repeated Eye material as translucent washed
            // out its dark compound-eye centers.
            const bool nativeLayeredEyeMaterial =
                nativePlainEye && materialUseCounts[materialIndex] > 1u &&
                materialUsesEyeAShell[materialIndex] &&
                materialUsesEyeBShell[materialIndex];
            const bool nativeLayeredEyeIris =
                nativeLayeredEyeMaterial &&
                submeshName.find("_eye_b_") != std::string::npos;
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
            const bool nativeEyeSurface =
                nativePlainEye || nativeTransparentEyeLens;
            const bool nativeLgpeLayered = nativeLgpeLayeredColor(material);
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
            float sourceMetallicFactor =
                translation.value("metallic_factor", 0.0f);
            float sourceRoughnessFactor =
                translation.value("roughness_factor", 1.0f);
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
                (nativeUnlitDisplaced &&
                 !loadTextureByRole(
                     root,
                     material,
                     "DisplacementMap",
                     normalTexture,
                     outError)) ||
                !loadMetallicRoughness(root, material, metalRoughTexture, outError) ||
                (nativeUnlitDisplaced &&
                 !loadTextureByRole(
                     root,
                     material,
                     "LayerMaskMap",
                     metalRoughTexture,
                     outError)) ||
                (!nativeUnlitDisplaced && !nativeScarletEye &&
                 !bakeLayeredMetallicRoughness(
                     root,
                     material,
                     sourceMetallicFactor,
                     sourceRoughnessFactor,
                     metalRoughTexture,
                     layeredMetalRoughBaked,
                     outError)) ||
                !loadTexture(root, material, "occlusion_texture", occlusionTexture, outError) ||
                !loadTexture(root, material, "emissive_texture", emissiveTexture, outError) ||
                ((nativePlainEye || nativeTransparentLayer) &&
                 !bakeLayeredEmission(
                    root,
                    material,
                    emissiveTexture,
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
                (!nativeUnlitDisplaced &&
                 !bakeLayeredBaseColor(
                     root,
                     material,
                     baseTexture,
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
            if (nativeLayeredEyeIris) {
                // PLA Paras keeps eye_a as the opaque pupil behind a larger
                // eye_b iris. Preserve that optical stack instead of moving
                // the pupil geometry to the surface. Alpha carries only
                // optical attenuation; mode 28's layered-iris marker supplies
                // the pale reflected/tinted contribution independently via
                // the premultiplied transmission blend path.
                setTextureAlpha(baseTexture, 0.38f);
            }
            if (nativeEye || nativeTransparentEyeLens) {
                preserveNativeEyeAsDielectric(
                    metalRoughTexture,
                    sourceMetallicFactor);
            }
            if (nativeScarletEye) {
                // The same-source GLB uses ordinary dielectric PBR at 0.5
                // roughness and no MR/emissive texture for EyeFinal.
                metalRoughTexture = CachedTextureRgba{};
                emissiveTexture = CachedTextureRgba{};
                sourceMetallicFactor = 0.0f;
                sourceRoughnessFactor = 0.5f;
                layeredMetalRoughBaked = false;
                layeredEmissionBaked = false;
            }
            if (!nativeUnlitDisplaced) {
                // IkCharacter's albedo, layer, and AO families share
                // UVScaleOffset. Normal maps intentionally use the separate
                // UVScaleOffsetNormal parameter. Layered albedo and material
                // properties were sampled with the base transform above; do
                // the same for the standalone AO payload before cooking.
                bakeStaticUvTransform(
                    material,
                    "UVScaleOffset",
                    occlusionTexture);
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
            const std::string alphaMode = nativeLgpeLayered
                ? "opaque"
                : ((nativeTransparentLayer && layeredEmissionBaked) ||
                   nativeLayeredEyeIris)
                    ? "blend"
                    : translation.value("alpha_mode", "opaque");
            out.submeshAlphaMode.push_back(
                alphaMode == "blend" ? 2u : alphaMode == "mask" ? 1u : 0u);
            out.submeshAlphaCutoff.push_back(translation.value("alpha_cutoff", 0.5f));
            out.submeshNormalScale.push_back(translation.value("normal_scale", 1.0f));
            out.submeshMetallicFactor.push_back(
                layeredMetalRoughBaked ? 1.0f : sourceMetallicFactor);
            out.submeshRoughnessFactor.push_back(
                layeredMetalRoughBaked ? 1.0f : sourceRoughnessFactor);
            out.submeshOcclusionStrength.push_back(translation.value("occlusion_strength", 1.0f));
            out.submeshEmissiveFactors.push_back(
                layeredEmissionBaked
                    ? glm::vec3(1.0f)
                    : glm::vec3(0.0f));
            float displacementHeight = 0.0f;
            float emissionIntensity = 1.0f;
            glm::vec4 displacementUvTransform(1.0f, 1.0f, 0.0f, 0.0f);
            glm::vec4 layeredBaseColor1(1.0f);
            glm::vec4 layeredBaseColor2(1.0f);
            const bool hasExactContinuousMaterialTrack =
                nativeUnlitDisplaced &&
                appendNativeContinuousMaterialTracks(
                    animationRecords,
                    material,
                    submeshIndex,
                    out);
            const glm::vec2 continuousUvLoopRates =
                nativeUnlitDisplaced
                    ? nativeContinuousUvLoopRates(
                          animationRecords,
                          material)
                    : glm::vec2(0.0f);
            (void)floatParameter(
                material,
                "DisplacementHeight",
                displacementHeight);
            (void)floatParameter(
                material,
                "EmissionIntensity",
                emissionIntensity);
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
            float clearCoatRoughness = 0.2f;
            float highlightRoughness = 0.51f;
            float highlightMetallic = 1.0f;
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
                "RoughnessHighlight",
                highlightRoughness);
            (void)floatParameter(
                material,
                "MetallicHighlight",
                highlightMetallic);
            (void)vec4Parameter(
                material,
                "BaseColorClearCoat",
                clearCoatBaseColor);
            if (nativeTransparentEyeLens &&
                !floatParameter(
                    material,
                    "RoughnessClearCoat",
                    clearCoatRoughness)) {
                clearCoatRoughness = sourceRoughnessFactor;
            }
            out.submeshMaterialModes.push_back(
                nativeUnlitDisplaced
                    ? game::runtime::render_model::
                          kNativeLayeredUnlitMaterialMode
                    : nativeEyeSurface
                        ? game::runtime::render_model::
                              kNativeEyeClearCoatMaterialMode
                        : 2u);
            out.submeshMaterialFlags.push_back(
                nativeUnlitDisplaced
                    ? (hasExactContinuousMaterialTrack ? 2.0f : 1.0f)
                    : 0.0f);
            out.submeshMaterialParams0.push_back(
                nativeUnlitDisplaced
                    ? glm::vec4(
                          std::max(0.0f, displacementHeight),
                          std::max(0.0f, emissionIntensity),
                          continuousUvLoopRates.x,
                          continuousUvLoopRates.y)
                    : nativeEyeSurface
                        ? glm::vec4(
                              glm::clamp(clearCoatRoughness, 0.02f, 1.0f),
                              glm::clamp(highlightRoughness, 0.02f, 1.0f),
                              glm::clamp(highlightMetallic, 0.0f, 1.0f),
                              shaderOptionEnabled(material, "EnableHighlight")
                                  ? 1.0f
                                  : 0.0f)
                        : glm::vec4(0.0f));
            out.submeshMaterialParams1.push_back(
                nativeUnlitDisplaced
                    ? displacementUvTransform
                    : nativeEyeSurface
                        // A negative coverage is an internal marker for PLA's
                        // plain Eye family. Scarlet EyeClearCoat is resolved
                        // to GLB-compatible EyeFinal color before this point.
                        ? glm::vec4(
                              glm::vec3(clearCoatBaseColor),
                              nativePlainEye
                                  ? (nativeLayeredEyeIris
                                         ? -2.0f
                                         : -1.0f)
                                  : glm::clamp(
                                        clearCoatBaseColor.a,
                                        0.0f,
                                        1.0f))
                        : glm::vec4(0.0f));
            out.submeshMaterialParams2.push_back(
                nativeUnlitDisplaced
                    ? layeredBaseColor1
                    : glm::vec4(0.0f));
            out.submeshMaterialParams3.push_back(
                nativeUnlitDisplaced
                    ? layeredBaseColor2
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
