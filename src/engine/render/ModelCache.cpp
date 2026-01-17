// src/engine/render/ModelCache.cpp

#include "Model.h"
#include "ModelStartupLog.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <functional>
#include <cstdint>
#include <cstddef>   // offsetof
#include <vector>
#include <string>
#include <cstdlib>   // std::getenv
#include <cstring>   // std::strcmp

#include <glad/glad.h>
#include <glm/gtc/type_ptr.hpp>

// Model.cpp provides this helper (keep definition there).
extern bool isMipmapMinFilter(GLint minF);

namespace pac_model_cache_detail {

namespace fs = std::filesystem;

template<typename T>
static bool readPod(std::istream& in, T& v) {
    return (bool)in.read(reinterpret_cast<char*>(&v), sizeof(T));
}
template<typename T>
static bool writePod(std::ostream& out, const T& v) {
    return (bool)out.write(reinterpret_cast<const char*>(&v), sizeof(T));
}

static bool readString(std::istream& in, std::string& s) {
    uint32_t n = 0;
    if (!readPod(in, n)) return false;
    s.clear();
    if (n == 0) return true;
    s.resize(n);
    return (bool)in.read(s.data(), n);
}
static bool writeString(std::ostream& out, const std::string& s) {
    uint32_t n = (uint32_t)s.size();
    if (!writePod(out, n)) return false;
    if (n == 0) return true;
    return (bool)out.write(s.data(), n);
}

static std::string hexHash64(uint64_t v) {
    std::ostringstream oss;
    oss << std::hex << std::setfill('0') << std::setw(16) << v;
    return oss.str();
}

// Treat any non-empty value other than "0" as true.
static bool envTruthy(const char* name) {
    const char* v = std::getenv(name);
    if (!v || !*v) return false;
    return std::strcmp(v, "0") != 0;
}

// Cache format constants
static constexpr uint64_t kModelCacheMagic = 0x4C444D434150554FULL; // "PACMDML" in little-endian-ish
static constexpr uint32_t kModelCacheVersion = 2;

#pragma pack(push, 1)
struct CacheHeader {
    uint64_t magic = kModelCacheMagic;
    uint32_t version = kModelCacheVersion;

    int64_t  srcWriteTime = 0;
    uint64_t srcFileSize  = 0;

    float    modelScaleFactor = 1.0f;

    uint32_t vertexCount  = 0;
    uint32_t indexCount   = 0;
    uint32_t submeshCount = 0;

    uint32_t nodeCount    = 0;
    uint32_t skinCount    = 0;
    uint32_t animCount    = 0;
};
#pragma pack(pop)

static fs::path cachePathForModel(const std::string& filepath) {
    // Keep it local + simple: cache/models/<hash>.pacmdl
    // Hash includes full input path string (relative/absolute as provided).
    uint64_t h = (uint64_t)std::hash<std::string>{}(filepath);
    fs::path dir = fs::path("cache") / "models";
    return dir / (hexHash64(h) + ".pacmdl");
}

} // namespace pac_model_cache_detail

// ------------------------------------------------------------
// Cache I/O (read)
// ------------------------------------------------------------
bool Model::tryLoadCache(const std::string& filepath)
{
    using namespace pac_model_cache_detail;

    // This keeps "test fastgltf" runs deterministic without forcing you to delete cache files.
    if (envTruthy("PAC_DISABLE_MODELCACHE")) return false;

    try {
        const fs::path cpath = cachePathForModel(filepath);
        if (!fs::exists(cpath)) return false;

        // Validate source file metadata
        if (!fs::exists(filepath)) return false;

        CacheHeader hdr{};
        {
            std::ifstream in(cpath, std::ios::binary);
            if (!in.is_open()) return false;
            if (!readPod(in, hdr)) return false;
            if (hdr.magic != kModelCacheMagic) return false;
            if (hdr.version != kModelCacheVersion) return false;

            const auto srcSz = (uint64_t)fs::file_size(filepath);
            const auto srcWt = fs::last_write_time(filepath).time_since_epoch().count();

            if (hdr.srcFileSize != srcSz) return false;
            if (hdr.srcWriteTime != (int64_t)srcWt) return false;

            // Apply cached state
            modelScaleFactor = hdr.modelScaleFactor;

            // Read animation/node/skin state first (so drawAnimated works)
            nodesDefault.clear();
            nodeChildren.clear();
            nodeMesh.clear();
            nodeSkin.clear();
            sceneRoots.clear();
            skins.clear();
            animations.clear();
            submeshes.clear();

            // Nodes
            nodesDefault.resize(hdr.nodeCount);
            nodeChildren.resize(hdr.nodeCount);
            nodeMesh.resize(hdr.nodeCount, -1);
            nodeSkin.resize(hdr.nodeCount, -1);

            for (uint32_t i = 0; i < hdr.nodeCount; ++i) {
                NodeTRS n{};
                if (!readPod(in, n.t)) return false;
                if (!readPod(in, n.r)) return false;
                if (!readPod(in, n.s)) return false;
                uint8_t hm = 0;
                if (!readPod(in, hm)) return false;
                n.hasMatrix = (hm != 0);
                if (!readPod(in, n.matrix)) return false;
                nodesDefault[i] = n;
            }

            for (uint32_t i = 0; i < hdr.nodeCount; ++i) {
                uint32_t cc = 0;
                if (!readPod(in, cc)) return false;
                nodeChildren[i].resize(cc);
                for (uint32_t k = 0; k < cc; ++k) {
                    int32_t vv = 0;
                    if (!readPod(in, vv)) return false;
                    nodeChildren[i][k] = (int)vv;
                }
            }

            for (uint32_t i = 0; i < hdr.nodeCount; ++i) {
                int32_t v = -1;
                if (!readPod(in, v)) return false;
                nodeMesh[i] = (int)v;
            }

            for (uint32_t i = 0; i < hdr.nodeCount; ++i) {
                int32_t v = -1;
                if (!readPod(in, v)) return false;
                nodeSkin[i] = (int)v;
            }

            // scene roots
            {
                uint32_t rc = 0;
                if (!readPod(in, rc)) return false;
                sceneRoots.resize(rc);
                for (uint32_t i = 0; i < rc; ++i) {
                    int32_t v = -1;
                    if (!readPod(in, v)) return false;
                    sceneRoots[i] = (int)v;
                }
            }

            // Skins
            skins.resize(hdr.skinCount);
            for (uint32_t si = 0; si < hdr.skinCount; ++si) {
                uint32_t jc = 0;
                if (!readPod(in, jc)) return false;
                skins[si].joints.resize(jc);
                skins[si].inverseBind.resize(jc);

                for (uint32_t j = 0; j < jc; ++j) {
                    int32_t v = -1;
                    if (!readPod(in, v)) return false;
                    skins[si].joints[j] = (int)v;
                }
                for (uint32_t j = 0; j < jc; ++j) {
                    if (!readPod(in, skins[si].inverseBind[j])) return false;
                }
            }

            // Animations
            animations.resize(hdr.animCount);
            for (uint32_t ai = 0; ai < hdr.animCount; ++ai) {
                AnimationClip clip{};
                if (!readString(in, clip.name)) return false;
                if (!readPod(in, clip.durationSec)) return false;

                uint32_t sc = 0;
                if (!readPod(in, sc)) return false;
                clip.samplers.resize(sc);
                for (uint32_t s = 0; s < sc; ++s) {
                    AnimationSampler samp{};
                    if (!readString(in, samp.interpolation)) return false;
                    uint8_t iv = 0;
                    if (!readPod(in, iv)) return false;
                    samp.isVec4 = (iv != 0);

                    uint32_t ic = 0;
                    if (!readPod(in, ic)) return false;
                    samp.inputs.resize(ic);
                    for (uint32_t k = 0; k < ic; ++k) {
                        if (!readPod(in, samp.inputs[k])) return false;
                    }

                    uint32_t oc = 0;
                    if (!readPod(in, oc)) return false;
                    samp.outputs.resize(oc);
                    for (uint32_t k = 0; k < oc; ++k) {
                        if (!readPod(in, samp.outputs[k])) return false;
                    }

                    clip.samplers[s] = std::move(samp);
                }

                uint32_t cc = 0;
                if (!readPod(in, cc)) return false;
                clip.channels.resize(cc);
                for (uint32_t c = 0; c < cc; ++c) {
                    int32_t si2 = -1;
                    int32_t tn  = -1;
                    uint8_t path = 0;
                    if (!readPod(in, si2)) return false;
                    if (!readPod(in, tn)) return false;
                    if (!readPod(in, path)) return false;

                    AnimationChannel ch{};
                    ch.samplerIndex = (int)si2;
                    ch.targetNode   = (int)tn;
                    ch.path = (path == 1u) ? ChannelPath::Rotation :
                              (path == 2u) ? ChannelPath::Scale :
                                             ChannelPath::Translation;
                    clip.channels[c] = ch;
                }

                animations[ai] = std::move(clip);
            }

            // Geometry
            std::vector<Vertex> vertices;
            std::vector<uint32_t> indices;

            vertices.resize(hdr.vertexCount);
            indices.resize(hdr.indexCount);

            if (hdr.vertexCount > 0) {
                if (!in.read(reinterpret_cast<char*>(vertices.data()), vertices.size() * sizeof(Vertex))) return false;
            }
            if (hdr.indexCount > 0) {
                if (!in.read(reinterpret_cast<char*>(indices.data()), indices.size() * sizeof(uint32_t))) return false;
            }

            // Submeshes + textures (upload textures to GL here)
            submeshes.resize(hdr.submeshCount);
            std::vector<CPUTexture> baseColorCPU;
            std::vector<CPUTexture> emissiveCPU;
            baseColorCPU.resize(hdr.submeshCount);
            emissiveCPU.resize(hdr.submeshCount);

            for (uint32_t i = 0; i < hdr.submeshCount; ++i) {
                uint64_t off = 0, cnt = 0;
                int32_t meshIdx = -1;
                if (!readPod(in, off)) return false;
                if (!readPod(in, cnt)) return false;
                if (!readPod(in, meshIdx)) return false;

                float ef[3] = {0.0f, 0.0f, 0.0f};
                uint8_t alphaMode = 0;
                float alphaCutoff = 0.5f;
                uint8_t doubleSided = 0;
                if (!readPod(in, ef[0])) return false;
                if (!readPod(in, ef[1])) return false;
                if (!readPod(in, ef[2])) return false;
                if (!readPod(in, alphaMode)) return false;
                if (!readPod(in, alphaCutoff)) return false;
                if (!readPod(in, doubleSided)) return false;

                submeshes[i].indexOffset = (size_t)off;
                submeshes[i].indexCount  = (size_t)cnt;
                submeshes[i].meshIndex   = (int)meshIdx;
                submeshes[i].emissiveFactor = glm::vec3(ef[0], ef[1], ef[2]);
                submeshes[i].alphaMode      = (int)alphaMode;
                submeshes[i].alphaCutoff    = alphaCutoff;
                submeshes[i].doubleSided    = (doubleSided != 0);

                auto readCPUTexture = [&](CPUTexture& t)->bool {
                    if (!readPod(in, t.width)) return false;
                    if (!readPod(in, t.height)) return false;
                    if (!readPod(in, t.wrapS)) return false;
                    if (!readPod(in, t.wrapT)) return false;
                    if (!readPod(in, t.minF)) return false;
                    if (!readPod(in, t.magF)) return false;
                    uint32_t bytes = 0;
                    if (!readPod(in, bytes)) return false;
                    t.rgba.resize(bytes);
                    if (bytes > 0) {
                        if (!in.read(reinterpret_cast<char*>(t.rgba.data()), bytes)) return false;
                    }
                    return true;
                };

                CPUTexture base{};
                CPUTexture emissive{};
                if (!readCPUTexture(base)) return false;
                if (!readCPUTexture(emissive)) return false;

                baseColorCPU[i] = std::move(base);
                emissiveCPU[i]  = std::move(emissive);
            }

            // Upload VBO/EBO/VAO exactly like the slow path
            glGenVertexArrays(1, &VAO);
            glGenBuffers(1, &VBO);
            glGenBuffers(1, &EBO);

            glBindVertexArray(VAO);

            glBindBuffer(GL_ARRAY_BUFFER, VBO);
            glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(Vertex), vertices.data(), GL_STATIC_DRAW);

            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
            glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(uint32_t), indices.data(), GL_STATIC_DRAW);

            // attributes: pos (0), uv (1), joints (2), weights (3)
            glEnableVertexAttribArray(0);
            glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, px));

            glEnableVertexAttribArray(1);
            glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, u));

            glEnableVertexAttribArray(2);
            glVertexAttribIPointer(2, 4, GL_UNSIGNED_SHORT, sizeof(Vertex), (void*)offsetof(Vertex, j0));

            glEnableVertexAttribArray(3);
            glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, w0));

            glBindVertexArray(0);

            // Upload textures
            auto uploadCPUTexture = [&](const CPUTexture& t)->GLuint {
                const int w = (int)(t.width  == 0 ? 1u : t.width);
                const int h = (int)(t.height == 0 ? 1u : t.height);
                const void* pixels = t.rgba.empty() ? nullptr : t.rgba.data();

                GLuint tex = 0;
                glGenTextures(1, &tex);
                glBindTexture(GL_TEXTURE_2D, tex);

                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, (GLsizei)w, (GLsizei)h,
                             0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);

                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, (GLint)t.wrapS);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, (GLint)t.wrapT);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, (GLint)t.minF);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, (GLint)t.magF);

                if (isMipmapMinFilter((GLint)t.minF)) {
                    glGenerateMipmap(GL_TEXTURE_2D);
                }
                return tex;
            };

            for (size_t i = 0; i < submeshes.size(); ++i) {
                submeshes[i].baseColorTexID = uploadCPUTexture(baseColorCPU[i]);
                submeshes[i].emissiveTexID  = uploadCPUTexture(emissiveCPU[i]);
            }

            glBindTexture(GL_TEXTURE_2D, 0);

            STARTUP_LOG(std::string("[Model] Cache hit: ") + filepath + " -> " + cpath.string());
            return true;
        }
    } catch (...) {
        return false;
    }
}

// ------------------------------------------------------------
// Cache I/O (write)
// ------------------------------------------------------------
void Model::writeCache(const std::string& filepath,
                       const std::vector<Vertex>& vertices,
                       const std::vector<uint32_t>& indices,
                       const std::vector<CPUTexture>& baseColorTexturesCPU,
                       const std::vector<CPUTexture>& emissiveTexturesCPU) const
{
    using namespace pac_model_cache_detail;

    try {
        if (!fs::exists(filepath)) return;

        fs::path cpath = cachePathForModel(filepath);
        fs::create_directories(cpath.parent_path());

        CacheHeader hdr{};
        hdr.srcFileSize = (uint64_t)fs::file_size(filepath);
        hdr.srcWriteTime = (int64_t)fs::last_write_time(filepath).time_since_epoch().count();

        hdr.modelScaleFactor = modelScaleFactor;

        hdr.vertexCount  = (uint32_t)vertices.size();
        hdr.indexCount   = (uint32_t)indices.size();
        hdr.submeshCount = (uint32_t)submeshes.size();

        hdr.nodeCount = (uint32_t)nodesDefault.size();
        hdr.skinCount = (uint32_t)skins.size();
        hdr.animCount = (uint32_t)animations.size();

        // Basic sanity: require matching texture entries
        if (baseColorTexturesCPU.size() != submeshes.size()) return;
        if (emissiveTexturesCPU.size() != submeshes.size()) return;

        std::ofstream out(cpath, std::ios::binary | std::ios::trunc);
        if (!out.is_open()) return;

        if (!writePod(out, hdr)) return;

        // Nodes
        for (const auto& n : nodesDefault) {
            if (!writePod(out, n.t)) return;
            if (!writePod(out, n.r)) return;
            if (!writePod(out, n.s)) return;
            uint8_t hm = n.hasMatrix ? 1u : 0u;
            if (!writePod(out, hm)) return;
            if (!writePod(out, n.matrix)) return;
        }

        for (const auto& ch : nodeChildren) {
            uint32_t cc = (uint32_t)ch.size();
            if (!writePod(out, cc)) return;
            for (int v : ch) {
                int32_t vv = (int32_t)v;
                if (!writePod(out, vv)) return;
            }
        }

        for (int v : nodeMesh) {
            int32_t vv = (int32_t)v;
            if (!writePod(out, vv)) return;
        }
        for (int v : nodeSkin) {
            int32_t vv = (int32_t)v;
            if (!writePod(out, vv)) return;
        }

        // scene roots
        {
            uint32_t rc = (uint32_t)sceneRoots.size();
            if (!writePod(out, rc)) return;
            for (int v : sceneRoots) {
                int32_t vv = (int32_t)v;
                if (!writePod(out, vv)) return;
            }
        }

        // Skins
        for (const auto& s : skins) {
            uint32_t jc = (uint32_t)s.joints.size();
            if (!writePod(out, jc)) return;
            for (int v : s.joints) {
                int32_t vv = (int32_t)v;
                if (!writePod(out, vv)) return;
            }
            for (const auto& m : s.inverseBind) {
                if (!writePod(out, m)) return;
            }
        }

        // Animations
        for (const auto& a : animations) {
            if (!writeString(out, a.name)) return;
            if (!writePod(out, a.durationSec)) return;

            uint32_t sc = (uint32_t)a.samplers.size();
            if (!writePod(out, sc)) return;
            for (const auto& s : a.samplers) {
                if (!writeString(out, s.interpolation)) return;
                uint8_t iv = s.isVec4 ? 1u : 0u;
                if (!writePod(out, iv)) return;

                uint32_t ic = (uint32_t)s.inputs.size();
                if (!writePod(out, ic)) return;
                for (float v : s.inputs) {
                    if (!writePod(out, v)) return;
                }

                uint32_t oc = (uint32_t)s.outputs.size();
                if (!writePod(out, oc)) return;
                for (const auto& v : s.outputs) {
                    if (!writePod(out, v)) return;
                }
            }

            uint32_t cc = (uint32_t)a.channels.size();
            if (!writePod(out, cc)) return;
            for (const auto& ch : a.channels) {
                int32_t si = (int32_t)ch.samplerIndex;
                int32_t tn = (int32_t)ch.targetNode;
                uint8_t path = (ch.path == ChannelPath::Rotation) ? 1u : (ch.path == ChannelPath::Scale) ? 2u : 0u;
                if (!writePod(out, si)) return;
                if (!writePod(out, tn)) return;
                if (!writePod(out, path)) return;
            }
        }

        // Geometry
        if (!vertices.empty()) {
            if (!out.write(reinterpret_cast<const char*>(vertices.data()), vertices.size() * sizeof(Vertex))) return;
        }
        if (!indices.empty()) {
            if (!out.write(reinterpret_cast<const char*>(indices.data()), indices.size() * sizeof(uint32_t))) return;
        }

        // Submeshes + textures + minimal material params
        for (size_t i = 0; i < submeshes.size(); ++i) {
            const auto& sm = submeshes[i];
            uint64_t off = (uint64_t)sm.indexOffset;
            uint64_t cnt = (uint64_t)sm.indexCount;
            int32_t meshIdx = (int32_t)sm.meshIndex;
            if (!writePod(out, off)) return;
            if (!writePod(out, cnt)) return;
            if (!writePod(out, meshIdx)) return;

            // material params
            if (!writePod(out, sm.emissiveFactor.x)) return;
            if (!writePod(out, sm.emissiveFactor.y)) return;
            if (!writePod(out, sm.emissiveFactor.z)) return;
            uint8_t alphaMode = (uint8_t)sm.alphaMode;
            uint8_t doubleSided = sm.doubleSided ? 1u : 0u;
            if (!writePod(out, alphaMode)) return;
            if (!writePod(out, sm.alphaCutoff)) return;
            if (!writePod(out, doubleSided)) return;

            auto writeCPUTexture = [&](const CPUTexture& t)->bool {
                if (!writePod(out, t.width)) return false;
                if (!writePod(out, t.height)) return false;
                if (!writePod(out, t.wrapS)) return false;
                if (!writePod(out, t.wrapT)) return false;
                if (!writePod(out, t.minF)) return false;
                if (!writePod(out, t.magF)) return false;
                uint32_t bytes = (uint32_t)t.rgba.size();
                if (!writePod(out, bytes)) return false;
                if (bytes > 0) {
                    if (!out.write(reinterpret_cast<const char*>(t.rgba.data()), bytes)) return false;
                }
                return true;
            };

            if (!writeCPUTexture(baseColorTexturesCPU[i])) return;
            if (!writeCPUTexture(emissiveTexturesCPU[i])) return;
        }

        STARTUP_LOG(std::string("[Model] Cache wrote: ") + filepath + " -> " + cpath.string());
    } catch (...) {
        // ignore cache write failures
    }
}


