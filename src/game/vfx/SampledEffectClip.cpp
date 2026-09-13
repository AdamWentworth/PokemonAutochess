#include "game/vfx/SampledEffectClip.h"
#include <nlohmann/json.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>

namespace game::vfx {
namespace {
using Json = nlohmann::json;
void require(bool value, const char *message) {
    if (!value) throw std::runtime_error(message);
}
std::vector<float> unpack(const Json &value, std::size_t stride) {
    require(std::endian::native == std::endian::little, "Sampled clip requires little endian floats");
    const auto &bytes = value.get_binary();
    require(bytes.size() % (stride * sizeof(float)) == 0 && bytes.size() <= 8 * 1024 * 1024, "Invalid sampled frame payload");
    std::vector<float> data(bytes.size() / sizeof(float));
    if (!bytes.empty()) std::memcpy(data.data(), bytes.data(), bytes.size());
    for (float v : data)
        require(std::isfinite(v), "Non-finite sampled vertex");
    return data;
}
} // namespace

bool SampledEffectClip::load(const engine::IAssetStore &store, const std::string &directory, std::string &error) {
    try {
        SampledEffectClip result;
        std::vector<std::uint8_t> bytes;
        if (!store.readBytes(directory + "/clip.cbor", bytes, &error)) return false;
        require(bytes.size() <= 128 * 1024 * 1024, "Sampled effect package is too large");
        const auto data = Json::from_cbor(bytes);
        require(data.at("kind") == "sampled_effect_clip" && data.at("schema") == 1, "Unsupported sampled clip");
        result.fps_ = data.at("fps").get<float>();
        result.frameCount_ = data.at("frame_count").get<int>();
        result.floor_ = data.at("floor_y").get<float>();
        require(std::isfinite(result.floor_) && result.fps_ > 0 && result.fps_ <= 120 && result.frameCount_ > 0 && result.frameCount_ <= 600, "Invalid clip timeline");
        require(data.at("textures").size() <= 512 && data.at("meshes").size() <= 2048 && data.at("cards").size() <= 4096, "Excessive clip layers");
        std::uint64_t hash = 14695981039346656037ull;
        for (auto b : bytes) {
            hash ^= b;
            hash *= 1099511628211ull;
        }
        result.geometryPrefix_ = "sampled-clip:" + std::to_string(hash);
        for (const auto &path : data.at("textures")) {
            const auto relative = path.get<std::string>();
            require(relative.starts_with("textures/") && relative.ends_with(".png") && relative.find("..") == std::string::npos && relative.find_first_of(":\\") == std::string::npos, "Unsafe clip texture path");
            result.textures_.push_back(directory + "/" + relative);
        }
        auto material = [&](const Json &j) {
            Material m;
            m.texture = j.at("texture").get<int>();
            require(m.texture >= -1 && m.texture < static_cast<int>(result.textures_.size()), "Invalid clip texture index");
            m.depthWrite = j.value("depth_write", false);
            m.additive = j.value("additive", false);
            const int s = j.value("wrap_s", 0), t = j.value("wrap_t", 0);
            require(s >= 0 && s <= 2 && t >= 0 && t <= 2, "Invalid clip texture wrap");
            m.wrapS = static_cast<unsigned char>(s);
            m.wrapT = static_cast<unsigned char>(t);
            return m;
        };
        result.groundMin_ = glm::vec3(std::numeric_limits<float>::max());
        result.groundMax_ = -result.groundMin_;
        for (const auto &j : data.at("meshes")) {
            Mesh m;
            m.material = material(j);
            m.indices = j.at("indices").get<std::vector<std::uint32_t>>();
            require(!m.indices.empty() && m.indices.size() % 3 == 0 && m.indices.size() <= 1000000, "Invalid clip triangles");
            require(j.at("frames").size() == static_cast<std::size_t>(result.frameCount_), "Missing clip mesh frames");
            for (const auto &frame : j.at("frames")) {
                const auto values = unpack(frame, 9);
                auto &vertices = m.frames.emplace_back();
                for (std::size_t i = 0; i < values.size(); i += 9) {
                    IRenderBackend::WorldMeshVertex v;
                    v.x = values[i];
                    v.y = values[i + 1];
                    v.z = values[i + 2];
                    v.u = values[i + 3];
                    v.v = values[i + 4];
                    v.r = values[i + 5];
                    v.g = values[i + 6];
                    v.b = values[i + 7];
                    v.a = values[i + 8];
                    v.ny = 1;
                    vertices.push_back(v);
                    if (m.material.depthWrite) {
                        result.groundMin_ = glm::min(result.groundMin_, glm::vec3(v.x, v.y, v.z));
                        result.groundMax_ = glm::max(result.groundMax_, glm::vec3(v.x, v.y, v.z));
                    }
                }
                if (!vertices.empty())
                    for (auto i : m.indices)
                        require(i < vertices.size(), "Clip index outside frame");
            }
            result.meshes_.push_back(std::move(m));
        }
        for (const auto &j : data.at("cards")) {
            Card c;
            c.material = material(j);
            require(j.at("quad").size() == 4 && j.at("uvs").size() == 4 && j.at("frames").size() == static_cast<std::size_t>(result.frameCount_), "Invalid clip card");
            for (int i = 0; i < 4; ++i) {
                c.quad[i] = {j.at("quad")[i][0].get<float>(), j.at("quad")[i][1].get<float>()};
                c.uv[i] = {j.at("uvs")[i][0].get<float>(), j.at("uvs")[i][1].get<float>()};
            }
            if (j.contains("direction")) c.direction = {j["direction"][0].get<float>(), j["direction"][1].get<float>(), j["direction"][2].get<float>()};
            for (const auto &frame : j.at("frames")) {
                const auto values = unpack(frame, 12);
                require(values.size() == 12 || values.empty(), "Invalid sampled card frame");
                auto &out = c.frames.emplace_back();
                if (!values.empty()) std::copy(values.begin(), values.end(), out.begin());
            }
            result.cards_.push_back(std::move(c));
        }
        require(!result.meshes_.empty(), "Empty sampled effect");
        *this = std::move(result);
        return true;
    } catch (const std::exception &e) {
        error = e.what();
        return false;
    }
}

void SampledEffectClip::prewarmTextures(const TextureLoader &textures) const {
    for (const auto &path : textures_) {
        const auto *texture = textures(path, false);
        if (!texture || !texture->valid) throw std::runtime_error("Missing sampled effect texture: " + path);
    }
}

SampledEffectClip::Batch SampledEffectClip::batch(const Material &material, const TextureLoader &textures) const {
    Batch b;
    if (material.texture >= 0) {
        b.textureKey = b.textureCacheKey = textures_.at(material.texture);
        const auto *t = textures(b.textureKey, false);
        if (!t || !t->valid) throw std::runtime_error("Missing sampled effect texture: " + b.textureKey);
        b.textureRgba = t->rgba.data();
        b.textureWidth = t->width;
        b.textureHeight = t->height;
    }
    b.alphaMode = material.depthWrite ? 1u : 2u;
    b.alphaCutoff = .001f;
    b.blendMode = material.additive ? 1u : 0u;
    b.textureWrapS = material.wrapS;
    b.textureWrapT = material.wrapT;
    b.depthTestEnabled = 1;
    b.preserveSubmissionOrder = true;
    return b;
}

void SampledEffectClip::append(float age, glm::vec3 origin, glm::vec3 scale, const glm::mat4 &vp,
                               glm::vec3 right, glm::vec3 up, const TextureLoader &textures,
                               std::vector<Batch> &batches) const {
    if (!std::isfinite(age) || age < 0 || age >= duration()) return;
    const int frame = std::min(frameCount_ - 1, static_cast<int>(age * fps_));
    const auto matrix = glm::translate(glm::mat4(1), origin) * glm::scale(glm::mat4(1), scale) * glm::translate(glm::mat4(1), glm::vec3(0, -floor_, 0));
    for (std::size_t i = 0; i < meshes_.size(); ++i) {
        const auto &mesh = meshes_[i];
        const auto &vertices = mesh.frames[frame];
        if (vertices.empty()) continue;
        auto b = batch(mesh.material, textures);
        b.sharedVertices = vertices.data();
        b.sharedVertexCount = vertices.size();
        b.sharedIndices = mesh.indices.data();
        b.sharedIndexCount = mesh.indices.size();
        b.geometryCacheKey = geometryPrefix_ + ":" + std::to_string(i) + ":" + std::to_string(frame);
        std::copy_n(glm::value_ptr(matrix), 16, b.modelMatrix.begin());
        batches.push_back(std::move(b));
    }
    // The card centres follow world motion. Their artwork faces the current
    // camera, so the editor can orbit without baking a particular view.
    for (const auto &card : cards_) {
        const auto &f = card.frames[frame];
        if (f[9] <= .001f || f[3] <= 0 || f[4] <= 0) continue;
        auto b = batch(card.material, textures);
        const glm::vec3 p = glm::vec3(matrix * glm::vec4(f[0], f[1], f[2], 1)) + right * f[10] * scale.x + up * f[11] * scale.y;
        float cs = std::cos(glm::radians(f[5])), sn = std::sin(glm::radians(f[5]));
        if (glm::dot(card.direction, card.direction) > 1e-8f) {
            const auto a = vp * glm::vec4(p, 1), d = vp * glm::vec4(p + card.direction * scale * .01f, 1);
            if (std::abs(a.w) > 1e-6f && std::abs(d.w) > 1e-6f) {
                const auto delta = glm::vec2(d) / d.w - glm::vec2(a) / a.w;
                if (glm::length(delta) > 1e-7f) {
                    const auto n = glm::normalize(delta);
                    cs = n.y;
                    sn = -n.x;
                }
            }
        }
        for (int i = 0; i < 4; ++i) {
            const float x = card.quad[i].x * f[3] * scale.x, y = card.quad[i].y * f[4] * scale.y;
            const auto q = p + right * (x * cs - y * sn) + up * (x * sn + y * cs);
            IRenderBackend::WorldMeshVertex v;
            v.x = q.x;
            v.y = q.y;
            v.z = q.z;
            v.u = card.uv[i].x;
            v.v = card.uv[i].y;
            v.r = f[6];
            v.g = f[7];
            v.b = f[8];
            v.a = f[9];
            v.ny = 1;
            b.vertices.push_back(v);
        }
        b.indices = {0, 1, 2, 0, 2, 3};
        batches.push_back(std::move(b));
    }
}
} // namespace game::vfx
