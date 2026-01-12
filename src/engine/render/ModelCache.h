// src/engine/render/ModelCache.h
#pragma once

// NOTE: This header is intended to be included ONLY by src/engine/render/Model.cpp.
// It contains the on-disk cache implementation for Model (read/write).
// Keeping it as a single header avoids adding another compilation unit right now.

#include "Model.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <functional>
#include <cstdint>
#include <cstddef>   // offsetof
#include <vector>
#include <string>

#include <glad/glad.h>
#include <glm/gtc/type_ptr.hpp>

// Model.cpp defines STARTUP_LOG and also provides isMipmapMinFilter(GLint).
// This header relies on both being available in the including translation unit.
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

// Cache format constants
static constexpr uint64_t kModelCacheMagic = 0x4C444D434150554FULL; // "PACMDML" in little-endian-ish
static constexpr uint32_t kModelCacheVersion = 1;

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
inline bool Model::tryLoadCache(const std::string& filepath)
{
    using namespace pac_model_cache_detail;

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
                for (uint32_t j = 0; j < cc; ++j) {
                    int32_t v = -1;
                    if (!readPod(in, v)) return false;
                    nodeChildren[i][j] = v;
                }
            }

            for (uint32_t i = 0; i < hdr.nodeCount; ++i) {
                int32_t v = -1;
                if (!readPod(in, v)) return false;
                nodeMesh[i] = v;
            }
            for (uint32_t i = 0; i < hdr.nodeCount; ++i) {
                int32_t v = -1;
                if (!readPod(in, v)) return false;
                nodeSkin[i] = v;
            }

            uint32_t rootCount = 0;
            if (!readPod(in, rootCount)) return false;
            sceneRoots.resize(rootCount);
            for (uint32_t i = 0; i < rootCount; ++i) {
                int32_t r = -1;
                if (!readPod(in, r)) return false;
                sceneRoots[i] = r;
            }

            // Skins
            skins.resize(hdr.skinCount);
            for (uint32_t si = 0; si < hdr.skinCount; ++si) {
                uint32_t jointCount = 0;
                if (!readPod(in, jointCount)) return false;

                SkinData sd;
                sd.joints.resize(jointCount);
                for (uint32_t j = 0; j < jointCount; ++j) {
                    int32_t node = -1;
                    if (!readPod(in, node)) return false;
                    sd.joints[j] = node;
                }

                sd.inverseBind.resize(jointCount, glm::mat4(1.0f));
                for (uint32_t j = 0; j < jointCount; ++j) {
                    glm::mat4 m(1.0f);
                    if (!readPod(in, m)) return false;
                    sd.inverseBind[j] = m;
                }

                skins[si] = std::move(sd);
            }

            // Animations
            animations.resize(hdr.animCount);
            for (uint32_t ai = 0; ai < hdr.animCount; ++ai) {
                AnimationClip clip;
                if (!readString(in, clip.name)) return false;
                if (!readPod(in, clip.durationSec)) return false;

                uint32_t samplerCount = 0;
                if (!readPod(in, samplerCount)) return false;
                clip.samplers.resize(samplerCount);

                for (uint32_t s = 0; s < samplerCount; ++s) {
                    AnimationSampler samp;
                    if (!readString(in, samp.interpolation)) return false;
                    uint8_t isv = 0;
                    if (!readPod(in, isv)) return false;
                    samp.isVec4 = (isv != 0);

                    uint32_t inCount = 0;
                    if (!readPod(in, inCount)) return false;
                    samp.inputs.resize(inCount);
                    if (inCount > 0) {
                        if (!in.read(reinterpret_cast<char*>(samp.inputs.data()), inCount * sizeof(float))) return false;
                    }

                    uint32_t outCount = 0;
                    if (!readPod(in, outCount)) return false;
                    samp.outputs.resize(outCount);
                    if (outCount > 0) {
                        if (!in.read(reinterpret_cast<char*>(samp.outputs.data()), outCount * sizeof(glm::vec4))) return false;
                    }

                    clip.samplers[s] = std::move(samp);
                }

                uint32_t chanCount = 0;
                if (!readPod(in, chanCount)) return false;
                clip.channels.resize(chanCount);

                for (uint32_t c = 0; c < chanCount; ++c) {
                    AnimationChannel ch;
                    int32_t si2 = -1, tn = -1;
                    uint8_t path = 0;
                    if (!readPod(in, si2)) return false;
                    if (!readPod(in, tn)) return false;
                    if (!readPod(in, path)) return false;
                    ch.samplerIndex = si2;
                    ch.targetNode = tn;
                    ch.path = (path == 1) ? ChannelPath::Rotation : (path == 2) ? ChannelPath::Scale : ChannelPath::Translation;
                    clip.channels[c] = ch;
                }

                animations[ai] = std::move(clip);
            }

            // Geometry
            std::vector<Vertex> vertices;
            std::vector<uint32_t> indices;

            vertices.resize(hdr.vertexCount);
            if (hdr.vertexCount > 0) {
                if (!in.read(reinterpret_cast<char*>(vertices.data()), hdr.vertexCount * sizeof(Vertex))) return false;
            }

            indices.resize(hdr.indexCount);
            if (hdr.indexCount > 0) {
                if (!in.read(reinterpret_cast<char*>(indices.data()), hdr.indexCount * sizeof(uint32_t))) return false;
            }

            // Submeshes + textures
            submeshes.resize(hdr.submeshCount);
            std::vector<CPUTexture> texCPU;
            texCPU.resize(hdr.submeshCount);

            for (uint32_t i = 0; i < hdr.submeshCount; ++i) {
                uint64_t off = 0, cnt = 0;
                int32_t meshIdx = -1;
                if (!readPod(in, off)) return false;
                if (!readPod(in, cnt)) return false;
                if (!readPod(in, meshIdx)) return false;

                Submesh sm;
                sm.indexOffset = (size_t)off;
                sm.indexCount  = (size_t)cnt;
                sm.meshIndex   = meshIdx;
                sm.textureID   = 0;
                submeshes[i] = sm;

                CPUTexture t;
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
                texCPU[i] = std::move(t);
            }

            // Upload GL buffers
            glGenVertexArrays(1, &VAO);
            glGenBuffers(1, &VBO);
            glGenBuffers(1, &EBO);

            glBindVertexArray(VAO);

            glBindBuffer(GL_ARRAY_BUFFER, VBO);
            glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(Vertex),
                         vertices.empty() ? nullptr : vertices.data(),
                         GL_STATIC_DRAW);

            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
            glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(uint32_t),
                         indices.empty() ? nullptr : indices.data(),
                         GL_STATIC_DRAW);

            glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, px));
            glEnableVertexAttribArray(0);

            glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, u));
            glEnableVertexAttribArray(1);

            glVertexAttribIPointer(2, 4, GL_UNSIGNED_SHORT, sizeof(Vertex), (void*)offsetof(Vertex, j0));
            glEnableVertexAttribArray(2);

            glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, w0));
            glEnableVertexAttribArray(3);

            glBindVertexArray(0);

            // Upload textures
            for (uint32_t i = 0; i < hdr.submeshCount; ++i) {
                const CPUTexture& t = texCPU[i];

                GLuint tex = 0;
                glGenTextures(1, &tex);
                glBindTexture(GL_TEXTURE_2D, tex);
                glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

                const uint32_t w = (t.width  == 0 ? 1u : t.width);
                const uint32_t h = (t.height == 0 ? 1u : t.height);

                const std::vector<uint8_t>& rgba = t.rgba;
                const void* pixels = rgba.empty() ? nullptr : rgba.data();

                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, (GLsizei)w, (GLsizei)h,
                             0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);

                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, (GLint)t.wrapS);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, (GLint)t.wrapT);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, (GLint)t.minF);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, (GLint)t.magF);

                if (isMipmapMinFilter((GLint)t.minF)) {
                    glGenerateMipmap(GL_TEXTURE_2D);
                }

                submeshes[i].textureID = tex;
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
inline void Model::writeCache(const std::string& filepath,
                             const std::vector<Vertex>& vertices,
                             const std::vector<uint32_t>& indices,
                             const std::vector<CPUTexture>& texturesCPU) const
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

        hdr.vertexCount = (uint32_t)vertices.size();
        hdr.indexCount  = (uint32_t)indices.size();
        hdr.submeshCount = (uint32_t)submeshes.size();

        hdr.nodeCount = (uint32_t)nodesDefault.size();
        hdr.skinCount = (uint32_t)skins.size();
        hdr.animCount = (uint32_t)animations.size();

        // Basic sanity: require matching texture entries
        if (texturesCPU.size() != submeshes.size()) return;

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

        uint32_t rootCount = (uint32_t)sceneRoots.size();
        if (!writePod(out, rootCount)) return;
        for (int r : sceneRoots) {
            int32_t rr = (int32_t)r;
            if (!writePod(out, rr)) return;
        }

        // Skins
        for (const auto& s : skins) {
            uint32_t jc = (uint32_t)s.joints.size();
            if (!writePod(out, jc)) return;
            for (int j : s.joints) {
                int32_t jj = (int32_t)j;
                if (!writePod(out, jj)) return;
            }
            for (uint32_t i = 0; i < jc; ++i) {
                const glm::mat4& m = (i < s.inverseBind.size()) ? s.inverseBind[i] : glm::mat4(1.0f);
                if (!writePod(out, m)) return;
            }
        }

        // Animations
        for (const auto& clip : animations) {
            if (!writeString(out, clip.name)) return;
            if (!writePod(out, clip.durationSec)) return;

            uint32_t sc = (uint32_t)clip.samplers.size();
            if (!writePod(out, sc)) return;

            for (const auto& samp : clip.samplers) {
                if (!writeString(out, samp.interpolation)) return;
                uint8_t isv = samp.isVec4 ? 1u : 0u;
                if (!writePod(out, isv)) return;

                uint32_t ic = (uint32_t)samp.inputs.size();
                if (!writePod(out, ic)) return;
                if (ic > 0) {
                    if (!out.write(reinterpret_cast<const char*>(samp.inputs.data()), ic * sizeof(float))) return;
                }

                uint32_t oc = (uint32_t)samp.outputs.size();
                if (!writePod(out, oc)) return;
                if (oc > 0) {
                    if (!out.write(reinterpret_cast<const char*>(samp.outputs.data()), oc * sizeof(glm::vec4))) return;
                }
            }

            uint32_t cc = (uint32_t)clip.channels.size();
            if (!writePod(out, cc)) return;
            for (const auto& ch : clip.channels) {
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

        // Submeshes + textures
        for (size_t i = 0; i < submeshes.size(); ++i) {
            const auto& sm = submeshes[i];
            uint64_t off = (uint64_t)sm.indexOffset;
            uint64_t cnt = (uint64_t)sm.indexCount;
            int32_t meshIdx = (int32_t)sm.meshIndex;
            if (!writePod(out, off)) return;
            if (!writePod(out, cnt)) return;
            if (!writePod(out, meshIdx)) return;

            const CPUTexture& t = texturesCPU[i];
            if (!writePod(out, t.width)) return;
            if (!writePod(out, t.height)) return;
            if (!writePod(out, t.wrapS)) return;
            if (!writePod(out, t.wrapT)) return;
            if (!writePod(out, t.minF)) return;
            if (!writePod(out, t.magF)) return;
            uint32_t bytes = (uint32_t)t.rgba.size();
            if (!writePod(out, bytes)) return;
            if (bytes > 0) {
                if (!out.write(reinterpret_cast<const char*>(t.rgba.data()), bytes)) return;
            }
        }

        STARTUP_LOG(std::string("[Model] Cache wrote: ") + filepath + " -> " + cpath.string());
    } catch (...) {
        // ignore cache write failures
    }
}
