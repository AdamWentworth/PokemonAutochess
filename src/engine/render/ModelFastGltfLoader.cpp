// src/engine/render/ModelFastGltfLoader.cpp
// Extracted from ModelFastGltfLoad.inl to keep Model.cpp small and testable.

#include "Model.h"
#include "ModelStartupLog.h"
#include "FastGLTFLoader.h"
#include "ModelFastGltfLoaderHelpers.h"

#include <fastgltf/tools.hpp>
#include <fastgltf/glm_element_traits.hpp>

#include <nlohmann/json.hpp>

#include <stb_image.h>
#include <stb_image_write.h>

#include <glad/glad.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

extern bool isMipmapMinFilter(GLint minF);

namespace {
template <typename T, typename = void>
struct fg_has_has_value : std::false_type {};

template <typename T>
struct fg_has_has_value<T, std::void_t<decltype(std::declval<const T&>().has_value())>>
    : std::true_type {};

template <typename T, typename = void>
struct fg_has_value_fn : std::false_type {};

template <typename T>
struct fg_has_value_fn<T, std::void_t<decltype(std::declval<const T&>().value())>>
    : std::true_type {};

template <typename T, typename = void>
struct fg_has_get : std::false_type {};

template <typename T>
struct fg_has_get<T, std::void_t<decltype(std::declval<const T&>().get())>>
    : std::true_type {};

template <typename T, typename = void>
struct fg_has_value_member : std::false_type {};

template <typename T>
struct fg_has_value_member<T, std::void_t<decltype((std::declval<const T&>().value))>>
    : std::true_type {};

template <typename T, typename = void>
struct fg_is_deref : std::false_type {};

template <typename T>
struct fg_is_deref<T, std::void_t<decltype(*std::declval<const T&>())>>
    : std::true_type {};

template <typename Opt>
bool fgOptHas(const Opt& o) {
    if constexpr (std::is_integral_v<std::decay_t<Opt>> || std::is_enum_v<std::decay_t<Opt>>) {
        return true;
    } else if constexpr (fg_has_has_value<Opt>::value) {
        return o.has_value();
    } else {
        return static_cast<bool>(o);
    }
}

template <typename Opt>
std::size_t fgOptGet(const Opt& o) {
    if constexpr (std::is_integral_v<std::decay_t<Opt>> || std::is_enum_v<std::decay_t<Opt>>) {
        return static_cast<std::size_t>(o);
    } else if constexpr (fg_has_get<Opt>::value) {
        return static_cast<std::size_t>(o.get());
    } else if constexpr (fg_has_value_fn<Opt>::value) {
        return static_cast<std::size_t>(o.value());
    } else if constexpr (fg_has_value_member<Opt>::value) {
        return static_cast<std::size_t>(o.value);
    } else if constexpr (fg_is_deref<Opt>::value) {
        return static_cast<std::size_t>(*o);
    } else {
        static_assert(!sizeof(Opt), "fgOptGet: unsupported optional type");
    }
}
} // namespace

void Model::loadGLTFFast(const std::string& filepath) {
using pac_model_types::AnimationClip;
using pac_model_types::AnimationSampler;
using pac_model_types::AnimationChannel;
using pac_model_types::ChannelPath;

// ------------------------------------------------------------
// FastGLTF load (full path)
// ------------------------------------------------------------
    auto fg = pac::fastgltf_loader::tryLoad(filepath);
    if (!fg.has_value()) {
        std::cerr << "[gltf][FASTGLTF] FAILED to parse: " << filepath << "\n";
        return;
    }

    const fastgltf::Asset& asset = fg->asset;

    const bool dbgThisModel = pac::model_fastgltf::envTruthy("PAC_GLTF_DEBUG") || pac::model_fastgltf::ciContains(filepath, "0019_rattata") || pac::model_fastgltf::ciContains(filepath, "rattata");
    if (dbgThisModel) {
        std::cerr << "[gltf][DEBUG] Extra logging ENABLED for: " << filepath << "\n";
        std::cerr << "[gltf][DEBUG] Env toggles: PAC_GLTF_DUMP_TEXTURES=1 will write debug PNGs; PAC_GLTF_RESPECT_TEXCOORD=1 will respect material texCoord indices.\n";
    }

    // Reset model state
    nodesDefault.clear();
    nodeChildren.clear();
    nodeMesh.clear();
    nodeSkin.clear();
    sceneRoots.clear();
    skins.clear();
    animations.clear();
    submeshes.clear();

    fastgltf::DefaultBufferDataAdapter adapter{};

    // ---- Nodes + scene roots ----
    nodesDefault.resize(asset.nodes.size());
    nodeChildren.resize(asset.nodes.size());
    nodeMesh.assign(asset.nodes.size(), -1);
    nodeSkin.assign(asset.nodes.size(), -1);

    if (!asset.scenes.empty()) {
        size_t sceneIndex = 0;
        if (asset.defaultScene.has_value()) sceneIndex = asset.defaultScene.value();
        if (sceneIndex >= asset.scenes.size()) sceneIndex = 0;

        sceneRoots.clear();
        for (auto n : asset.scenes[sceneIndex].nodeIndices) {
            sceneRoots.push_back((int)n);
        }
    }

    for (size_t i = 0; i < asset.nodes.size(); ++i) {
        const auto& n = asset.nodes[i];

        nodeChildren[i].clear();
        nodeChildren[i].reserve(n.children.size());
        for (auto c : n.children) nodeChildren[i].push_back((int)c);

        if (n.meshIndex.has_value()) nodeMesh[i] = (int)n.meshIndex.value();
        if (n.skinIndex.has_value()) nodeSkin[i] = (int)n.skinIndex.value();

        NodeTRS trs;
        trs.hasMatrix = false;

        if (const auto* t = std::get_if<fastgltf::TRS>(&n.transform)) {
            trs.t = glm::vec3(t->translation[0], t->translation[1], t->translation[2]);
            trs.r = glm::normalize(glm::quat(t->rotation[3], t->rotation[0], t->rotation[1], t->rotation[2]));
            trs.s = glm::vec3(t->scale[0], t->scale[1], t->scale[2]);
        } else if (const auto* m = std::get_if<fastgltf::math::fmat4x4>(&n.transform)) {
            trs.hasMatrix = true;
            trs.matrix = glm::make_mat4(m->data());
        }

        nodesDefault[i] = trs;
    }

    // ---- Skins ----
    skins.resize(asset.skins.size());
    for (size_t si = 0; si < asset.skins.size(); ++si) {
        const auto& s = asset.skins[si];
        SkinData out;
        out.joints.reserve(s.joints.size());
        for (auto j : s.joints) out.joints.push_back((int)j);

        if (s.inverseBindMatrices.has_value()) {
            const size_t accIndex = s.inverseBindMatrices.value();
            if (accIndex < asset.accessors.size()) {
                std::vector<glm::mat4> mats;
                pac::model_fastgltf::readMat4(asset, asset.accessors[accIndex], mats, adapter);
                out.inverseBind = std::move(mats);
            }
        }

        if (out.inverseBind.size() != out.joints.size()) {
            out.inverseBind.assign(out.joints.size(), glm::mat4(1.0f));
        }

        skins[si] = std::move(out);
    }

    // ---- Animations ----
    animations.reserve(asset.animations.size());
    for (const auto& anim : asset.animations) {
        AnimationClip clip;
        clip.name = std::string(anim.name.begin(), anim.name.end());
        clip.durationSec = 0.0f;

        clip.samplers.resize(anim.samplers.size());

        for (size_t si = 0; si < anim.samplers.size(); ++si) {
            const auto& s = anim.samplers[si];
            AnimationSampler samp;

            switch (s.interpolation) {
                case fastgltf::AnimationInterpolation::Step:        samp.interpolation = "STEP"; break;
                case fastgltf::AnimationInterpolation::CubicSpline: samp.interpolation = "CUBICSPLINE"; break;
                case fastgltf::AnimationInterpolation::Linear:
                default:                                           samp.interpolation = "LINEAR"; break;
            }

            if (s.inputAccessor < asset.accessors.size()) {
                pac::model_fastgltf::readScalarFloat(asset, asset.accessors[s.inputAccessor], samp.inputs, adapter);
                if (!samp.inputs.empty()) {
                    clip.durationSec = (std::max)(clip.durationSec, samp.inputs.back());
                }
            }

            if (s.outputAccessor < asset.accessors.size()) {
                const auto& outAcc = asset.accessors[s.outputAccessor];
                std::vector<glm::vec4> raw;

                if (outAcc.type == fastgltf::AccessorType::Vec3) {
                    pac::model_fastgltf::readVec3AsVec4(asset, outAcc, raw, adapter);
                    samp.isVec4 = false;
                } else {
                    pac::model_fastgltf::readVec4(asset, outAcc, raw, adapter);
                    samp.isVec4 = true;
                }

                if (samp.interpolation == "CUBICSPLINE" && !samp.inputs.empty()) {
                    const size_t keys = samp.inputs.size();
                    std::vector<glm::vec4> values;
                    values.reserve(keys);
                    for (size_t k = 0; k < keys; ++k) {
                        const size_t idx = k * 3 + 1;
                        if (idx < raw.size()) values.push_back(raw[idx]);
                    }
                    samp.outputs = std::move(values);
                } else {
                    samp.outputs = std::move(raw);
                }
            }

            clip.samplers[si] = std::move(samp);
        }

        clip.channels.reserve(anim.channels.size());
        for (const auto& ch : anim.channels) {
            if (!fgOptHas(ch.nodeIndex))    continue;
            if (!fgOptHas(ch.samplerIndex)) continue;

            AnimationChannel c;
            c.targetNode   = (int)fgOptGet(ch.nodeIndex);
            c.samplerIndex = (int)fgOptGet(ch.samplerIndex);

            switch (ch.path) {
                case fastgltf::AnimationPath::Translation: c.path = ChannelPath::Translation; break;
                case fastgltf::AnimationPath::Rotation:    c.path = ChannelPath::Rotation;    break;
                case fastgltf::AnimationPath::Scale:       c.path = ChannelPath::Scale;       break;
                default: continue;
            }

            clip.channels.push_back(c);
        }

        animations.push_back(std::move(clip));
    }

    std::cerr << "[gltf] fastgltf animations=" << animations.size()
              << " skins=" << skins.size()
              << " nodes=" << nodesDefault.size() << "\n";

    // ---- Meshes + textures ----
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    vertices.reserve(20000);
    indices.reserve(60000);

    std::vector<pac::model_fastgltf::CPUTexture> baseColorTexturesCPU;
    std::vector<pac::model_fastgltf::CPUTexture> emissiveTexturesCPU;
    baseColorTexturesCPU.reserve(64);
    emissiveTexturesCPU.reserve(64);

    float minX = std::numeric_limits<float>::max(), minY = std::numeric_limits<float>::max(), minZ = std::numeric_limits<float>::max();
    float maxX = -minX, maxY = -minY, maxZ = -minZ;

    for (size_t meshIdx = 0; meshIdx < asset.meshes.size(); ++meshIdx) {
        const auto& mesh = asset.meshes[meshIdx];

        for (size_t primIdx = 0; primIdx < mesh.primitives.size(); ++primIdx) {
            const auto& p = mesh.primitives[primIdx];
            if (p.type != fastgltf::PrimitiveType::Triangles) continue;


            int materialIndex = -1;
            if (fgOptHas(p.materialIndex)) {
                materialIndex = (int)fgOptGet(p.materialIndex);
            }

            auto itPos = p.findAttribute("POSITION");
            if (itPos == p.attributes.end()) {
                std::cerr << "[fastgltf] Missing POSITION in primitive\n";
                continue;
            }

            // Determine which UV set this primitive *wants* based on the material texCoord indices.
            // Compatibility-safe behavior:
            // - If material wants TEXCOORD_0 -> use it (same as before)
            // - If material wants TEXCOORD_n -> try it, and fallback to TEXCOORD_0 if missing
            // - PAC_GLTF_RESPECT_TEXCOORD can still force logging/diagnostics semantics, but isn't required anymore.
            int requiredTexCoord = pac::model_fastgltf::requiredTexCoordForMaterial(asset, materialIndex);

            std::string uvAttr = "TEXCOORD_" + std::to_string(requiredTexCoord);
            auto itUv = p.findAttribute(uvAttr);

            // Fallback to TEXCOORD_0 if missing.
            if (itUv == p.attributes.end()) {
                if (dbgThisModel && requiredTexCoord != 0) {
                    std::cerr << "[gltf][WARN] Primitive material wants " << uvAttr
                              << " but it's missing; falling back to TEXCOORD_0.\n";
                }
                requiredTexCoord = 0;
                itUv = p.findAttribute("TEXCOORD_0");
            }

            const bool hasUv = (itUv != p.attributes.end());
            if (!hasUv) {
                std::cerr << "[fastgltf] Missing TEXCOORD_0 in primitive; generating planar UVs from POSITION.\n";
            }

            const size_t posAcc = itPos->accessorIndex;
            const size_t uvAcc  = hasUv ? itUv->accessorIndex : 0;

            std::vector<glm::vec3> pos;
            std::vector<glm::vec2> uv;
            pos.reserve(asset.accessors[posAcc].count);
            uv.reserve(hasUv ? asset.accessors[uvAcc].count : asset.accessors[posAcc].count);

            fastgltf::iterateAccessorWithIndex<glm::vec3>(
                asset, asset.accessors[posAcc],
                [&](glm::vec3 v, size_t) { pos.push_back(v); },
                adapter
            );

            if (hasUv) {
                fastgltf::iterateAccessorWithIndex<glm::vec2>(
                    asset, asset.accessors[uvAcc],
                    [&](glm::vec2 v, size_t) { uv.push_back(v); },
                    adapter
                );
            } else {
                // Fallback UVs from projected position when TEXCOORD is missing.
                // Pick the two widest axes to avoid collapse on flat dimensions.
                glm::vec3 pMin( std::numeric_limits<float>::max());
                glm::vec3 pMax(-std::numeric_limits<float>::max());
                for (const auto& p3 : pos) {
                    pMin.x = (std::min)(pMin.x, p3.x); pMin.y = (std::min)(pMin.y, p3.y); pMin.z = (std::min)(pMin.z, p3.z);
                    pMax.x = (std::max)(pMax.x, p3.x); pMax.y = (std::max)(pMax.y, p3.y); pMax.z = (std::max)(pMax.z, p3.z);
                }

                const float r[3] = { pMax.x - pMin.x, pMax.y - pMin.y, pMax.z - pMin.z };
                int a = 0;
                if (r[1] > r[a]) a = 1;
                if (r[2] > r[a]) a = 2;

                int b = (a == 0) ? 1 : 0;
                for (int i = 0; i < 3; ++i) {
                    if (i == a) continue;
                    if (r[i] > r[b]) b = i;
                }

                const float minA = (a == 0) ? pMin.x : ((a == 1) ? pMin.y : pMin.z);
                const float minB = (b == 0) ? pMin.x : ((b == 1) ? pMin.y : pMin.z);
                const float denA = std::max(1e-6f, (a == 0) ? r[0] : ((a == 1) ? r[1] : r[2]));
                const float denB = std::max(1e-6f, (b == 0) ? r[0] : ((b == 1) ? r[1] : r[2]));

                uv.reserve(pos.size());
                for (const auto& p3 : pos) {
                    const float ca = (a == 0) ? p3.x : ((a == 1) ? p3.y : p3.z);
                    const float cb = (b == 0) ? p3.x : ((b == 1) ? p3.y : p3.z);
                    uv.emplace_back((ca - minA) / denA, (cb - minB) / denB);
                }
            }

            // ---- COLOR_0 (vertex color) ----
            std::vector<glm::vec4> color;
            color.resize(pos.size(), glm::vec4(1.0f)); // default = white

            auto itC = p.findAttribute("COLOR_0");
            if (itC != p.attributes.end()) {
                const size_t colAcc = itC->accessorIndex;
                const auto& acc = asset.accessors[colAcc];

                color.clear();
                color.reserve(acc.count);

                if (acc.type == fastgltf::AccessorType::Vec3) {
                    fastgltf::iterateAccessorWithIndex<glm::vec3>(
                        asset, acc,
                        [&](glm::vec3 v, size_t) { color.emplace_back(v.x, v.y, v.z, 1.0f); },
                        adapter
                    );
                } else {
                    fastgltf::iterateAccessorWithIndex<glm::vec4>(
                        asset, acc,
                        [&](glm::vec4 v, size_t) { color.push_back(v); },
                        adapter
                    );
                }

                // Safety: if counts mismatch, ignore the attribute
                if (color.size() != pos.size()) {
                    color.assign(pos.size(), glm::vec4(1.0f));
                }
            }

            // --- Apply KHR_texture_transform (bake into UVs) if present ---
            auto applyKHRTextureTransform = [&](const fastgltf::TextureInfo* ti) {
                if (!ti) return;
                if (!ti->transform) return;

                const auto& tr = *ti->transform;

                // fastgltf uses uvOffset/uvScale for KHR_texture_transform
                glm::vec2 offset(tr.uvOffset[0], tr.uvOffset[1]);
                glm::vec2 scale (tr.uvScale[0],  tr.uvScale[1]);
                float rot = (float)tr.rotation;

                float c = std::cos(rot);
                float s = std::sin(rot);

                for (auto& t : uv) {
                    glm::vec2 p = t;
                    p *= scale;
                    p = glm::vec2(c * p.x - s * p.y,
                                  s * p.x + c * p.y);
                    p += offset;
                    t = p;
                }

                if (dbgThisModel) {
                    std::cerr << "[gltf][XFORM] Applied KHR_texture_transform: "
                              << "offset=(" << offset.x << "," << offset.y << ") "
                              << "scale=(" << scale.x << "," << scale.y << ") "
                              << "rot=" << rot << "\n";
                }
            };

            // Only bake transform for the baseColorTexture that matches the UV set we're using
            const fastgltf::TextureInfo* baseTI = nullptr;
            if (materialIndex >= 0 && materialIndex < (int)asset.materials.size()) {
                const auto& mat = asset.materials[(size_t)materialIndex];
                if (mat.pbrData.baseColorTexture.has_value()) {
                    const auto& ti = mat.pbrData.baseColorTexture.value();
                    if ((int)ti.texCoordIndex == requiredTexCoord) {
                        baseTI = &ti;
                    }
                }
            }
            applyKHRTextureTransform(baseTI);

            if (pos.empty() || uv.empty() || pos.size() != uv.size()) {
                std::cerr << "[fastgltf] Invalid POSITION/TEXCOORD sizes\n";
                continue;
            }

            if (dbgThisModel) {
                glm::vec2 uvMin( 1e9f), uvMax(-1e9f);
                for (const auto& t : uv) {
                    uvMin.x = (std::min)(uvMin.x, t.x); uvMin.y = (std::min)(uvMin.y, t.y);
                    uvMax.x = (std::max)(uvMax.x, t.x); uvMax.y = (std::max)(uvMax.y, t.y);
                }
                std::cerr << "[gltf][UV] mat=" << materialIndex
                          << " texCoord=" << requiredTexCoord
                          << " uvMin=(" << uvMin.x << "," << uvMin.y << ")"
                          << " uvMax=(" << uvMax.x << "," << uvMax.y << ")"
                          << " vertCount=" << uv.size() << "\n";
            }

            std::vector<glm::u16vec4> joints;
            std::vector<glm::vec4> weights;
            auto itJ = p.findAttribute("JOINTS_0");
            auto itW = p.findAttribute("WEIGHTS_0");
            if (itJ != p.attributes.end() && itW != p.attributes.end()) {
                joints.reserve(asset.accessors[itJ->accessorIndex].count);
                weights.reserve(asset.accessors[itW->accessorIndex].count);

                fastgltf::iterateAccessorWithIndex<glm::u16vec4>(
                    asset, asset.accessors[itJ->accessorIndex],
                    [&](glm::u16vec4 v, size_t) { joints.push_back(v); },
                    adapter
                );

                fastgltf::iterateAccessorWithIndex<glm::vec4>(
                    asset, asset.accessors[itW->accessorIndex],
                    [&](glm::vec4 v, size_t) { weights.push_back(v); },
                    adapter
                );

                if (joints.size() != pos.size() || weights.size() != pos.size()) {
                    joints.clear();
                    weights.clear();
                }
            }

            std::vector<uint32_t> primIdxU32;
            if (p.indicesAccessor.has_value()) {
                const auto& idxAcc = asset.accessors[p.indicesAccessor.value()];
                primIdxU32.reserve(idxAcc.count);

                fastgltf::iterateAccessorWithIndex<std::uint32_t>(
                    asset, idxAcc,
                    [&](std::uint32_t v, size_t) { primIdxU32.push_back(v); },
                    adapter
                );
            }

            if (primIdxU32.empty()) {
                primIdxU32.resize(pos.size());
                for (size_t i = 0; i < pos.size(); ++i) primIdxU32[i] = (uint32_t)i;
            }

            const size_t baseVertex = vertices.size();
            const size_t subIndexOffset = indices.size();

            for (size_t i = 0; i < pos.size(); ++i) {
                Vertex v{};
                v.px = pos[i].x; v.py = pos[i].y; v.pz = pos[i].z;
                v.u  = uv[i].x;  v.v  = uv[i].y;

                v.j0 = v.j1 = v.j2 = v.j3 = 0;
                v.w0 = 1.0f; v.w1 = v.w2 = v.w3 = 0.0f;

                // Vertex color (linear)
                v.r = color[i].r;
                v.g = color[i].g;
                v.b = color[i].b;
                v.a = color[i].a;

                if (!joints.empty() && !weights.empty()) {
                    auto j = joints[i];
                    auto w = weights[i];

                    float sum = w.x + w.y + w.z + w.w;
                    if (sum <= 0.0001f) w = glm::vec4(1,0,0,0);
                    else w /= sum;

                    v.j0 = j.x; v.j1 = j.y; v.j2 = j.z; v.j3 = j.w;
                    v.w0 = w.x; v.w1 = w.y; v.w2 = w.z; v.w3 = w.w;
                }

                vertices.push_back(v);

                minX = (std::min)(minX, v.px); minY = (std::min)(minY, v.py); minZ = (std::min)(minZ, v.pz);
                maxX = (std::max)(maxX, v.px); maxY = (std::max)(maxY, v.py); maxZ = (std::max)(maxZ, v.pz);
            }

            for (auto idx : primIdxU32) {
                indices.push_back((uint32_t)(baseVertex + idx));
            }

            // --- material decode (minimal glTF) ---
            int baseTexCoordUsed = 0;
            int emissiveTexCoordUsed = 0;
            pac::model_fastgltf::CPUTexture baseCPU = pac::model_fastgltf::decodeBaseColorTextureFast(asset, fg->baseDir, materialIndex, dbgThisModel, filepath, &baseTexCoordUsed);
            pac::model_fastgltf::CPUTexture emissiveCPU = pac::model_fastgltf::decodeEmissiveTextureFast(asset, fg->baseDir, materialIndex, dbgThisModel, filepath, &emissiveTexCoordUsed);
            if (dbgThisModel && (baseTexCoordUsed != requiredTexCoord || emissiveTexCoordUsed != requiredTexCoord)) {
                std::cerr << "[gltf][INFO] Material texCoord(base=" << baseTexCoordUsed
                        << ", emissive=" << emissiveTexCoordUsed
                        << "), meshUV=" << requiredTexCoord << "\n";
            }

            glm::vec3 emissiveFactor(0.0f);
            int alphaMode = 0;       // OPAQUE
            float alphaCutoff = 0.5f;
            bool doubleSided = false;

            if (materialIndex >= 0 && materialIndex < (int)asset.materials.size()) {
                const auto& mat = asset.materials[(size_t)materialIndex];

                // emissiveFactor is always present in glTF (defaults to (0,0,0))
                emissiveFactor = glm::vec3(mat.emissiveFactor[0], mat.emissiveFactor[1], mat.emissiveFactor[2]);

                // Apply emissive strength ONCE
                emissiveFactor *= (float)mat.emissiveStrength;

                // Boost ONLY the tail fire, without affecting the rest of the model.
                const std::string matName(mat.name.begin(), mat.name.end());
                if (matName == "fire") {
                    const float kTailFireBoost = 1.35f;
                    emissiveFactor *= kTailFireBoost;
                }

                // alpha mode
                switch (mat.alphaMode) {
                    case fastgltf::AlphaMode::Mask:  alphaMode = 1; break;
                    case fastgltf::AlphaMode::Blend: alphaMode = 2; break;
                    default:                         alphaMode = 0; break; // Opaque
                }
                alphaCutoff = (float)mat.alphaCutoff;
                doubleSided = mat.doubleSided;

                // Some source assets tag materials as BLEND even when alpha is effectively
                // fully opaque (e.g., eyes), which causes depth-write issues and "hollow"
                // look-through artifacts. Normalize these here using decoded base alpha.
                if (alphaMode == 2 && !baseCPU.rgba.empty()) {
                    const size_t pixelCount = baseCPU.rgba.size() / 4u;
                    if (pixelCount > 0u) {
                        uint8_t minA = 255u;
                        uint8_t maxA = 0u;
                        size_t zeroA = 0u;
                        size_t midA = 0u;

                        for (size_t i = 3; i < baseCPU.rgba.size(); i += 4u) {
                            const uint8_t a = baseCPU.rgba[i];
                            minA = (std::min)(minA, a);
                            maxA = (std::max)(maxA, a);
                            if (a == 0u) ++zeroA;
                            else if (a < 255u) ++midA;
                        }

                        const float midFrac = static_cast<float>(midA) / static_cast<float>(pixelCount);
                        const bool effectivelyOpaque = (minA >= 250u) && (midFrac <= 0.001f);
                        const bool mostlyBinaryCutout = (zeroA > 0u) && (midFrac <= 0.015f);

                        if (effectivelyOpaque) {
                            alphaMode = 0; // OPAQUE
                        } else if (mostlyBinaryCutout) {
                            alphaMode = 1; // MASK
                            alphaCutoff = std::clamp(alphaCutoff, 0.1f, 0.9f);
                        }

                        if (dbgThisModel && alphaMode != 2) {
                            std::cerr << "[gltf][MAT] normalized BLEND material '" << matName
                                      << "' -> " << (alphaMode == 0 ? "OPAQUE" : "MASK")
                                      << " (minA=" << (int)minA
                                      << " maxA=" << (int)maxA
                                      << " zero=" << zeroA
                                      << " mid=" << midA
                                      << " px=" << pixelCount << ")\n";
                        }
                    }
                }
            }

            // Upload baseColor texture
            GLuint baseTexId = 0;
            glGenTextures(1, &baseTexId);
            glBindTexture(GL_TEXTURE_2D, baseTexId);
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

            const uint32_t bw = (baseCPU.width  == 0 ? 1u : baseCPU.width);
            const uint32_t bh = (baseCPU.height == 0 ? 1u : baseCPU.height);
            const void* bpixels = baseCPU.rgba.empty() ? nullptr : baseCPU.rgba.data();

            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, (GLsizei)bw, (GLsizei)bh, 0,
                         GL_RGBA, GL_UNSIGNED_BYTE, bpixels);
            if (dbgThisModel) {
                GLenum err = glGetError();
                if (err != GL_NO_ERROR) {
                    std::cerr << "[gltf][GL] baseTex glTexImage2D error=0x" << std::hex << (unsigned)err << std::dec << "\n";
                }
                GLint wq = 0, hq = 0, ifmt = 0;
                glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &wq);
                glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &hq);
                glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_INTERNAL_FORMAT, &ifmt);
                std::cerr << "[gltf][GL] baseTex uploaded size=" << wq << "x" << hq << " ifmt=" << ifmt << "\n";
            }

            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, (GLint)baseCPU.wrapS);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, (GLint)baseCPU.wrapT);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, (GLint)baseCPU.minF);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, (GLint)baseCPU.magF);

            if (isMipmapMinFilter((GLint)baseCPU.minF)) {
                glGenerateMipmap(GL_TEXTURE_2D);
            }

            // Upload emissive texture
            GLuint emissiveTexId = 0;
            glGenTextures(1, &emissiveTexId);
            glBindTexture(GL_TEXTURE_2D, emissiveTexId);
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

            const uint32_t ew = (emissiveCPU.width  == 0 ? 1u : emissiveCPU.width);
            const uint32_t eh = (emissiveCPU.height == 0 ? 1u : emissiveCPU.height);
            const void* epixels = emissiveCPU.rgba.empty() ? nullptr : emissiveCPU.rgba.data();

            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, (GLsizei)ew, (GLsizei)eh, 0,
                         GL_RGBA, GL_UNSIGNED_BYTE, epixels);
            if (dbgThisModel) {
                GLenum err = glGetError();
                if (err != GL_NO_ERROR) {
                    std::cerr << "[gltf][GL] emissiveTex glTexImage2D error=0x" << std::hex << (unsigned)err << std::dec << "\n";
                }
                GLint wq = 0, hq = 0, ifmt = 0;
                glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &wq);
                glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &hq);
                glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_INTERNAL_FORMAT, &ifmt);
                std::cerr << "[gltf][GL] emissiveTex uploaded size=" << wq << "x" << hq << " ifmt=" << ifmt << "\n";
            }

            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, (GLint)emissiveCPU.wrapS);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, (GLint)emissiveCPU.wrapT);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, (GLint)emissiveCPU.minF);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, (GLint)emissiveCPU.magF);

            if (isMipmapMinFilter((GLint)emissiveCPU.minF)) {
                glGenerateMipmap(GL_TEXTURE_2D);
            }

            Submesh sm;
            sm.indexOffset = subIndexOffset;
            sm.indexCount  = primIdxU32.size();
            sm.baseColorTexID = baseTexId;
            sm.emissiveTexID  = emissiveTexId;
            sm.emissiveFactor = emissiveFactor;
            sm.alphaMode      = alphaMode;
            sm.alphaCutoff    = alphaCutoff;
            sm.doubleSided    = doubleSided;
            sm.meshIndex   = (int)meshIdx;
            submeshes.push_back(sm);

            baseColorTexturesCPU.push_back(std::move(baseCPU));
            emissiveTexturesCPU.push_back(std::move(emissiveCPU));
        }
    }

    // ---- Bounds (model space) ----
    if (!vertices.empty()) {
        boundsMin = glm::vec3(minX, minY, minZ);
        boundsMax = glm::vec3(maxX, maxY, maxZ);
        boundsValid = true;

        const glm::vec3 ext = boundsMax - boundsMin;
        boundsRadius = 0.5f * glm::length(ext);

        // Pick the largest extent as "up", compute radius in the other two axes.
        int upAxis = 0;
        if (ext.y >= ext.x && ext.y >= ext.z) upAxis = 1;
        else if (ext.z >= ext.x && ext.z >= ext.y) upAxis = 2;

        float ex = ext.x, ey = ext.y, ez = ext.z;
        if (upAxis == 0) boundsRadiusHorizontal = 0.5f * std::sqrt(ey * ey + ez * ez);
        else if (upAxis == 1) boundsRadiusHorizontal = 0.5f * std::sqrt(ex * ex + ez * ez);
        else boundsRadiusHorizontal = 0.5f * std::sqrt(ex * ex + ey * ey);
    } else {
        boundsMin = boundsMax = glm::vec3(0.0f);
        boundsRadius = 0.0f;
        boundsRadiusHorizontal = 0.0f;
        boundsValid = false;
    }

    // ---- Scale factor ----
    float desiredHeight = 0.8f;
    const float ex = std::max(0.0f, maxX - minX);
    const float ey = std::max(0.0f, maxY - minY);
    const float ez = std::max(0.0f, maxZ - minZ);
    float denom = std::max(ex, std::max(ey, ez));
    if (std::abs(denom) < 1e-6f) denom = 1.0f;
    modelScaleFactor = desiredHeight / denom;

    STARTUP_LOG(
        std::string("[Model] Loaded (fastgltf): ") + filepath +
        " vertices=" + std::to_string(vertices.size()) +
        " indices=" + std::to_string(indices.size()) +
        " submeshes=" + std::to_string(submeshes.size())
    );

    // ---- Upload geometry ----
    glGenVertexArrays(1, &VAO);
    glGenBuffers(1, &VBO);
    glGenBuffers(1, &EBO);

    glBindVertexArray(VAO);

    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(Vertex), vertices.data(), GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(uint32_t), indices.data(), GL_STATIC_DRAW);

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, px));
    glEnableVertexAttribArray(0);

    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, u));
    glEnableVertexAttribArray(1);

    glVertexAttribIPointer(2, 4, GL_UNSIGNED_SHORT, sizeof(Vertex), (void*)offsetof(Vertex, j0));
    glEnableVertexAttribArray(2);

    glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, w0));
    glEnableVertexAttribArray(3);

    // COLOR_0 (vec4)
    glVertexAttribPointer(4, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, r));
    glEnableVertexAttribArray(4);

    glBindVertexArray(0);

    // ---- Cache write ----
    writeCache(filepath, vertices, indices, baseColorTexturesCPU, emissiveTexturesCPU);
    std::cerr << "[gltf][FASTGLTF] COMPLETE for: " << filepath << "\n";
}
