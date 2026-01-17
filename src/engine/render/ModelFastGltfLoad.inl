// src/engine/render/ModelFastGltfLoad.inl
//
// Intentionally included inside Model::loadGLTF(...) in Model.cpp
// so it can use Model's private nested types (Vertex, NodeTRS, etc).
//
// IMPORTANT: This file is included inside a function body.
// - No free-function definitions here.
// - Local classes are allowed, but MSVC forbids *member templates* in local classes.
//   So any templates (traits, etc.) must live at file-scope in Model.cpp.

struct FG {
    using CPUTexture = Model::CPUTexture;

    static GLint wrapToGL(fastgltf::Wrap w) {
        switch (w) {
            case fastgltf::Wrap::ClampToEdge:    return GL_CLAMP_TO_EDGE;
            case fastgltf::Wrap::MirroredRepeat: return GL_MIRRORED_REPEAT;
            case fastgltf::Wrap::Repeat:
            default:                             return GL_REPEAT;
        }
    }

    static GLint filterToGLMin(int f) {
        switch (f) {
            case 9728: return GL_NEAREST;
            case 9729: return GL_LINEAR;
            case 9984: return GL_NEAREST_MIPMAP_NEAREST;
            case 9985: return GL_LINEAR_MIPMAP_NEAREST;
            case 9986: return GL_NEAREST_MIPMAP_LINEAR;
            case 9987: return GL_LINEAR_MIPMAP_LINEAR;
            default:   return GL_LINEAR_MIPMAP_LINEAR;
        }
    }

    static GLint filterToGLMag(int f) {
        switch (f) {
            case 9728: return GL_NEAREST;
            case 9729: return GL_LINEAR;
            default:   return GL_LINEAR;
        }
    }

    struct EncodedImageBytes {
        std::vector<std::uint8_t> bytes;
        std::string debugName;
    };

    static int b64Value(unsigned char c) {
        if (c >= 'A' && c <= 'Z') return c - 'A';
        if (c >= 'a' && c <= 'z') return c - 'a' + 26;
        if (c >= '0' && c <= '9') return c - '0' + 52;
        if (c == '+') return 62;
        if (c == '/') return 63;
        return -1;
    }

    static bool decodeBase64(std::string_view in, std::vector<std::uint8_t>& out) {
        out.clear();
        int val = 0, valb = -8;
        for (unsigned char c : in) {
            if (c == '=') break;
            int v = b64Value(c);
            if (v < 0) continue;
            val = (val << 6) + v;
            valb += 6;
            if (valb >= 0) {
                out.push_back((std::uint8_t)((val >> valb) & 0xFF));
                valb -= 8;
            }
        }
        return !out.empty();
    }

    static std::optional<EncodedImageBytes>
    getEncodedImageBytes(const fastgltf::Asset& asset,
                         const std::filesystem::path& baseDir,
                         const fastgltf::Image& image) {
        EncodedImageBytes out{};

        if (const auto* uri = std::get_if<fastgltf::sources::URI>(&image.data)) {
            std::string u(uri->uri.string().begin(), uri->uri.string().end());
            out.debugName = u;

            if (u.rfind("data:", 0) == 0) {
                const size_t comma = u.find(',');
                if (comma == std::string::npos) return std::nullopt;

                std::string_view meta(u.data(), comma);
                std::string_view payload(u.data() + comma + 1, u.size() - (comma + 1));

                const bool isBase64 = (meta.find(";base64") != std::string_view::npos);
                if (!isBase64) return std::nullopt;

                if (!decodeBase64(payload, out.bytes)) return std::nullopt;
                return out;
            }

            std::filesystem::path p = baseDir / std::string(uri->uri.path().begin(), uri->uri.path().end());
            out.debugName = p.string();

            std::ifstream f(p, std::ios::binary);
            if (!f) return std::nullopt;
            f.seekg(0, std::ios::end);
            std::streamsize size = f.tellg();
            f.seekg(0, std::ios::beg);
            if (size <= 0) return std::nullopt;
            out.bytes.resize((size_t)size);
            if (!f.read(reinterpret_cast<char*>(out.bytes.data()), size)) return std::nullopt;
            return out;
        }

        if (const auto* vec = std::get_if<fastgltf::sources::Vector>(&image.data)) {
            out.debugName = image.name.empty() ? "VectorImage" : std::string(image.name.begin(), image.name.end());
            out.bytes.resize(vec->bytes.size());
            if (!vec->bytes.empty()) {
                std::memcpy(out.bytes.data(), vec->bytes.data(), vec->bytes.size());
            }
            return out;
        }

        if (const auto* arr = std::get_if<fastgltf::sources::Array>(&image.data)) {
            out.debugName = image.name.empty() ? "ArrayImage" : std::string(image.name.begin(), image.name.end());
            out.bytes.resize(arr->bytes.size());
            if (!arr->bytes.empty()) {
                std::memcpy(out.bytes.data(), arr->bytes.data(), arr->bytes.size());
            }
            return out;
        }

        if (const auto* view = std::get_if<fastgltf::sources::ByteView>(&image.data)) {
            out.debugName = image.name.empty() ? "ByteViewImage" : std::string(image.name.begin(), image.name.end());
            out.bytes.resize(view->bytes.size());
            if (!view->bytes.empty()) {
                std::memcpy(out.bytes.data(), view->bytes.data(), view->bytes.size());
            }
            return out;
        }

        if (const auto* bv = std::get_if<fastgltf::sources::BufferView>(&image.data)) {
            if (bv->bufferViewIndex >= asset.bufferViews.size()) return std::nullopt;
            const auto& bufferView = asset.bufferViews[bv->bufferViewIndex];
            if (bufferView.bufferIndex >= asset.buffers.size()) return std::nullopt;
            const auto& buffer = asset.buffers[bufferView.bufferIndex];

            const std::byte* bufPtr = nullptr;
            size_t bufSize = 0;

            if (const auto* bufVec = std::get_if<fastgltf::sources::Vector>(&buffer.data)) {
                bufPtr = bufVec->bytes.data();
                bufSize = bufVec->bytes.size();
            } else if (const auto* bufArr = std::get_if<fastgltf::sources::Array>(&buffer.data)) {
                bufPtr = bufArr->bytes.data();
                bufSize = bufArr->bytes.size();
            } else if (const auto* bufView = std::get_if<fastgltf::sources::ByteView>(&buffer.data)) {
                bufPtr = bufView->bytes.data();
                bufSize = bufView->bytes.size();
            } else {
                return std::nullopt;
            }

            const size_t start = (size_t)bufferView.byteOffset;
            const size_t size  = (size_t)bufferView.byteLength;
            if (start + size > bufSize) return std::nullopt;

            out.debugName = image.name.empty() ? "BufferViewImage" : std::string(image.name.begin(), image.name.end());
            out.bytes.resize(size);
            if (size > 0) {
                std::memcpy(out.bytes.data(),
                            reinterpret_cast<const void*>(bufPtr + start),
                            size);
            }
            return out;
        }

        return std::nullopt;
    }

    static CPUTexture makeWhiteCPUTexture() {
        CPUTexture t;
        t.width = 1; t.height = 1;
        t.wrapS = GL_REPEAT;
        t.wrapT = GL_REPEAT;
        t.minF  = GL_LINEAR;
        t.magF  = GL_LINEAR;
        t.rgba = {255,255,255,255};
        return t;
    }

    static CPUTexture makeBlackCPUTexture() {
        CPUTexture t;
        t.width = 1; t.height = 1;
        t.wrapS = GL_REPEAT;
        t.wrapT = GL_REPEAT;
        t.minF  = GL_LINEAR;
        t.magF  = GL_LINEAR;
        t.rgba = {0,0,0,255};
        return t;
    }

    static CPUTexture decodeBaseColorTextureFast(const fastgltf::Asset& asset,
                                                 const std::filesystem::path& baseDir,
                                                 int materialIndex) {
        if (materialIndex < 0 || materialIndex >= (int)asset.materials.size()) {
            return makeWhiteCPUTexture();
        }

        const auto& mat = asset.materials[(size_t)materialIndex];

        if (!mat.pbrData.baseColorTexture.has_value()) {
            CPUTexture t;
            t.width = 1; t.height = 1;
            t.wrapS = GL_REPEAT;
            t.wrapT = GL_REPEAT;
            t.minF  = GL_LINEAR;
            t.magF  = GL_LINEAR;

            auto f = mat.pbrData.baseColorFactor;
            auto toU8 = [](float x)->uint8_t {
                x = std::max(0.0f, std::min(1.0f, x));
                return (uint8_t)std::lround(x * 255.0f);
            };
            t.rgba = { toU8(f[0]), toU8(f[1]), toU8(f[2]), toU8(f[3]) };
            return t;
        }

        const auto& texInfo = mat.pbrData.baseColorTexture.value();
        const size_t texIndex = texInfo.textureIndex;
        if (texIndex >= asset.textures.size()) {
            return makeWhiteCPUTexture();
        }

        const auto& tex = asset.textures[texIndex];

        fastgltf::Optional<std::size_t> imgIndexOpt = tex.imageIndex;
        if (!imgIndexOpt.has_value()) imgIndexOpt = tex.webpImageIndex;
        if (!imgIndexOpt.has_value()) imgIndexOpt = tex.basisuImageIndex;
        if (!imgIndexOpt.has_value()) imgIndexOpt = tex.ddsImageIndex;

        if (!imgIndexOpt.has_value()) return makeWhiteCPUTexture();

        const size_t imgIndex = imgIndexOpt.value();
        if (imgIndex >= asset.images.size()) return makeWhiteCPUTexture();

        const auto& img = asset.images[imgIndex];
        auto enc = getEncodedImageBytes(asset, baseDir, img);
        if (!enc.has_value() || enc->bytes.empty()) return makeWhiteCPUTexture();

        int w = 0, h = 0, comp = 0;
        stbi_uc* decoded = stbi_load_from_memory(
            enc->bytes.data(),
            (int)enc->bytes.size(),
            &w, &h, &comp, 4
        );

        if (decoded == nullptr || w <= 0 || h <= 0) {
            if (decoded) stbi_image_free(decoded);
            return makeWhiteCPUTexture();
        }

        CPUTexture out;
        out.width  = (uint32_t)w;
        out.height = (uint32_t)h;

        out.wrapS = GL_REPEAT;
        out.wrapT = GL_REPEAT;
        out.minF  = GL_LINEAR_MIPMAP_LINEAR;
        out.magF  = GL_LINEAR;

        if (tex.samplerIndex.has_value() && tex.samplerIndex.value() < asset.samplers.size()) {
            const auto& s = asset.samplers[tex.samplerIndex.value()];
            out.wrapS = wrapToGL(s.wrapS);
            out.wrapT = wrapToGL(s.wrapT);
            if (s.minFilter.has_value()) out.minF = filterToGLMin((int)s.minFilter.value());
            if (s.magFilter.has_value()) out.magF = filterToGLMag((int)s.magFilter.value());
        }

        const size_t pxCount = (size_t)w * (size_t)h;
        out.rgba.resize(pxCount * 4);
        std::memcpy(out.rgba.data(), decoded, pxCount * 4);
        stbi_image_free(decoded);

        return out;
    }

    static CPUTexture decodeEmissiveTextureFast(const fastgltf::Asset& asset,
                                                const std::filesystem::path& baseDir,
                                                int materialIndex) {
        if (materialIndex < 0 || materialIndex >= (int)asset.materials.size()) {
            return makeBlackCPUTexture();
        }

        const auto& mat = asset.materials[(size_t)materialIndex];

        // No emissive texture -> return a 1x1 black tex; factor will still be applied in shader.
        if (!mat.emissiveTexture.has_value()) {
            return makeBlackCPUTexture();
        }

        const auto& texInfo = mat.emissiveTexture.value();
        const size_t texIndex = texInfo.textureIndex;
        if (texIndex >= asset.textures.size()) {
            return makeBlackCPUTexture();
        }

        const auto& tex = asset.textures[texIndex];

        fastgltf::Optional<std::size_t> imgIndexOpt = tex.imageIndex;
        if (!imgIndexOpt.has_value()) imgIndexOpt = tex.webpImageIndex;
        if (!imgIndexOpt.has_value()) imgIndexOpt = tex.basisuImageIndex;
        if (!imgIndexOpt.has_value()) imgIndexOpt = tex.ddsImageIndex;

        if (!imgIndexOpt.has_value()) return makeBlackCPUTexture();

        const size_t imgIndex = imgIndexOpt.value();
        if (imgIndex >= asset.images.size()) return makeBlackCPUTexture();

        const auto& img = asset.images[imgIndex];
        auto enc = getEncodedImageBytes(asset, baseDir, img);
        if (!enc.has_value() || enc->bytes.empty()) return makeBlackCPUTexture();

        int w = 0, h = 0, comp = 0;
        stbi_uc* decoded = stbi_load_from_memory(
            enc->bytes.data(),
            (int)enc->bytes.size(),
            &w, &h, &comp, 4
        );

        if (decoded == nullptr || w <= 0 || h <= 0) {
            if (decoded) stbi_image_free(decoded);
            return makeBlackCPUTexture();
        }

        CPUTexture out;
        out.width  = (uint32_t)w;
        out.height = (uint32_t)h;

        out.wrapS = GL_REPEAT;
        out.wrapT = GL_REPEAT;
        out.minF  = GL_LINEAR_MIPMAP_LINEAR;
        out.magF  = GL_LINEAR;

        if (tex.samplerIndex.has_value() && tex.samplerIndex.value() < asset.samplers.size()) {
            const auto& s = asset.samplers[tex.samplerIndex.value()];
            out.wrapS = wrapToGL(s.wrapS);
            out.wrapT = wrapToGL(s.wrapT);
            if (s.minFilter.has_value()) out.minF = filterToGLMin((int)s.minFilter.value());
            if (s.magFilter.has_value()) out.magF = filterToGLMag((int)s.magFilter.value());
        }

        const size_t pxCount = (size_t)w * (size_t)h;
        out.rgba.resize(pxCount * 4);
        std::memcpy(out.rgba.data(), decoded, pxCount * 4);
        stbi_image_free(decoded);

        return out;
    }

    static void readScalarFloat(const fastgltf::Asset& asset,
                                const fastgltf::Accessor& acc,
                                std::vector<float>& out,
                                fastgltf::DefaultBufferDataAdapter& adapter) {
        out.clear();
        out.reserve(acc.count);
        fastgltf::iterateAccessorWithIndex<float>(
            asset, acc,
            [&](float v, size_t) { out.push_back(v); },
            adapter
        );
    }

    static void readVec3AsVec4(const fastgltf::Asset& asset,
                               const fastgltf::Accessor& acc,
                               std::vector<glm::vec4>& out,
                               fastgltf::DefaultBufferDataAdapter& adapter) {
        out.clear();
        out.reserve(acc.count);
        fastgltf::iterateAccessorWithIndex<glm::vec3>(
            asset, acc,
            [&](glm::vec3 v, size_t) { out.emplace_back(v.x, v.y, v.z, 0.0f); },
            adapter
        );
    }

    static void readVec4(const fastgltf::Asset& asset,
                         const fastgltf::Accessor& acc,
                         std::vector<glm::vec4>& out,
                         fastgltf::DefaultBufferDataAdapter& adapter) {
        out.clear();
        out.reserve(acc.count);
        fastgltf::iterateAccessorWithIndex<glm::vec4>(
            asset, acc,
            [&](glm::vec4 v, size_t) { out.push_back(v); },
            adapter
        );
    }

    static void readMat4(const fastgltf::Asset& asset,
                         const fastgltf::Accessor& acc,
                         std::vector<glm::mat4>& out,
                         fastgltf::DefaultBufferDataAdapter& adapter) {
        out.clear();
        out.reserve(acc.count);
        fastgltf::iterateAccessorWithIndex<glm::mat4>(
            asset, acc,
            [&](glm::mat4 m, size_t) { out.push_back(m); },
            adapter
        );
    }
};

// ------------------------------------------------------------
// FastGLTF load (full path)
// ------------------------------------------------------------
{
    auto fg = pac::fastgltf_loader::tryLoad(filepath);
    if (!fg.has_value()) {
        std::cerr << "[gltf][FASTGLTF] FAILED to parse: " << filepath << "\n";
        return;
    }

    const fastgltf::Asset& asset = fg->asset;

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
                FG::readMat4(asset, asset.accessors[accIndex], mats, adapter);
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
                FG::readScalarFloat(asset, asset.accessors[s.inputAccessor], samp.inputs, adapter);
                if (!samp.inputs.empty()) clip.durationSec = std::max(clip.durationSec, samp.inputs.back());
            }

            if (s.outputAccessor < asset.accessors.size()) {
                const auto& outAcc = asset.accessors[s.outputAccessor];
                std::vector<glm::vec4> raw;

                if (outAcc.type == fastgltf::AccessorType::Vec3) {
                    FG::readVec3AsVec4(asset, outAcc, raw, adapter);
                    samp.isVec4 = false;
                } else {
                    FG::readVec4(asset, outAcc, raw, adapter);
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

    std::vector<FG::CPUTexture> baseColorTexturesCPU;
    std::vector<FG::CPUTexture> emissiveTexturesCPU;
    baseColorTexturesCPU.reserve(64);
    emissiveTexturesCPU.reserve(64);

    float minX = std::numeric_limits<float>::max(), minY = std::numeric_limits<float>::max(), minZ = std::numeric_limits<float>::max();
    float maxX = -minX, maxY = -minY, maxZ = -minZ;

    for (size_t meshIdx = 0; meshIdx < asset.meshes.size(); ++meshIdx) {
        const auto& mesh = asset.meshes[meshIdx];

        for (size_t primIdx = 0; primIdx < mesh.primitives.size(); ++primIdx) {
            const auto& p = mesh.primitives[primIdx];
            if (p.type != fastgltf::PrimitiveType::Triangles) continue;

            auto itPos = p.findAttribute("POSITION");
            auto itUv  = p.findAttribute("TEXCOORD_0");
            if (itPos == p.attributes.end() || itUv == p.attributes.end()) {
                std::cerr << "[fastgltf] Missing POSITION or TEXCOORD_0 in primitive\n";
                continue;
            }

            const size_t posAcc = itPos->accessorIndex;
            const size_t uvAcc  = itUv->accessorIndex;

            std::vector<glm::vec3> pos;
            std::vector<glm::vec2> uv;
            pos.reserve(asset.accessors[posAcc].count);
            uv.reserve(asset.accessors[uvAcc].count);

            fastgltf::iterateAccessorWithIndex<glm::vec3>(
                asset, asset.accessors[posAcc],
                [&](glm::vec3 v, size_t) { pos.push_back(v); },
                adapter
            );

            fastgltf::iterateAccessorWithIndex<glm::vec2>(
                asset, asset.accessors[uvAcc],
                [&](glm::vec2 v, size_t) { uv.push_back(v); },
                adapter
            );

            if (pos.empty() || uv.empty() || pos.size() != uv.size()) {
                std::cerr << "[fastgltf] Invalid POSITION/TEXCOORD_0 sizes\n";
                continue;
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

                minX = std::min(minX, v.px); minY = std::min(minY, v.py); minZ = std::min(minZ, v.pz);
                maxX = std::max(maxX, v.px); maxY = std::max(maxY, v.py); maxZ = std::max(maxZ, v.pz);
            }

            for (auto idx : primIdxU32) {
                indices.push_back((uint32_t)(baseVertex + idx));
            }

            int materialIndex = -1;
            if (fgOptHas(p.materialIndex)) {
                materialIndex = (int)fgOptGet(p.materialIndex);
            }

            // --- material decode (minimal glTF) ---
            FG::CPUTexture baseCPU = FG::decodeBaseColorTextureFast(asset, fg->baseDir, materialIndex);
            FG::CPUTexture emissiveCPU = FG::decodeEmissiveTextureFast(asset, fg->baseDir, materialIndex);

            glm::vec3 emissiveFactor(0.0f);
            int alphaMode = 0;       // OPAQUE
            float alphaCutoff = 0.5f;
            bool doubleSided = false;

            if (materialIndex >= 0 && materialIndex < (int)asset.materials.size()) {
                const auto& mat = asset.materials[(size_t)materialIndex];

                // emissiveFactor is always present in glTF (defaults to (0,0,0))
                emissiveFactor = glm::vec3(mat.emissiveFactor[0], mat.emissiveFactor[1], mat.emissiveFactor[2]);

                // alpha mode
                switch (mat.alphaMode) {
                    case fastgltf::AlphaMode::Mask:  alphaMode = 1; break;
                    case fastgltf::AlphaMode::Blend: alphaMode = 2; break;
                    default:                         alphaMode = 0; break; // Opaque
                }
                alphaCutoff = (float)mat.alphaCutoff;
                doubleSided = mat.doubleSided;
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

    // ---- Scale factor ----
    float desiredHeight = 0.8f;
    float denom = (maxZ - minZ);
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

    glBindVertexArray(0);

    // ---- Cache write ----
    writeCache(filepath, vertices, indices, baseColorTexturesCPU, emissiveTexturesCPU);
    std::cerr << "[gltf][FASTGLTF] COMPLETE for: " << filepath << "\n";
}
