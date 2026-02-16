// src/engine/render/ModelFastGltfLoader.cpp
// Extracted from ModelFastGltfLoad.inl to keep Model.cpp small and testable.

#include "Model.h"
#include "ModelStartupLog.h"
#include "FastGLTFLoader.h"

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

    
    enum class TextureKind { BaseColor, Emissive };

    static bool envTruthy(const char* name) {
        const char* v = std::getenv(name);
        if (!v || !*v) return false;
        return std::strcmp(v, "0") != 0;
    }

    static std::string toLower(std::string s) {
        for (char& c : s) c = (char)std::tolower((unsigned char)c);
        return s;
    }

    static bool ciContains(const std::string& s, const std::string& needle) {
        return toLower(s).find(toLower(needle)) != std::string::npos;
    }


    static int requiredTexCoordForMaterial(const fastgltf::Asset& asset, int materialIndex) {
        if (materialIndex < 0 || materialIndex >= (int)asset.materials.size()) return 0;

        const auto& m = asset.materials[(size_t)materialIndex];

        if (m.pbrData.baseColorTexture.has_value()) {
            return (int)m.pbrData.baseColorTexture->texCoordIndex;
        }
        if (m.emissiveTexture.has_value()) {
            return (int)m.emissiveTexture->texCoordIndex;
        }
        return 0;
    }

    static const char* magicName(const std::vector<std::uint8_t>& bytes) {
        if (bytes.size() >= 12) {
            // PNG
            static const unsigned char pngSig[8] = {0x89,'P','N','G',0x0D,0x0A,0x1A,0x0A};
            if (std::memcmp(bytes.data(), pngSig, 8) == 0) return "PNG";
            // JPEG
            if (bytes[0] == 0xFF && bytes[1] == 0xD8) return "JPG";
            // KTX2
            static const unsigned char ktx2Sig[12] = {0xAB,'K','T','X',' ','2','0',0xBB,0x0D,0x0A,0x1A,0x0A};
            if (std::memcmp(bytes.data(), ktx2Sig, 12) == 0) return "KTX2";
            // DDS
            if (bytes[0] == 'D' && bytes[1] == 'D' && bytes[2] == 'S' && bytes[3] == ' ') return "DDS";
            // WEBP (RIFF....WEBP)
            if (bytes[0]=='R' && bytes[1]=='I' && bytes[2]=='F' && bytes[3]=='F' &&
                bytes[8]=='W' && bytes[9]=='E' && bytes[10]=='B' && bytes[11]=='P') return "WEBP";
        }
        return "UNKNOWN";
    }

    static void logTexDecode(const std::string& prefix, const std::string& msg) {
        std::cerr << prefix << msg << "\n";
    }

    static std::string glEnumName(GLint e) {
        switch (e) {
            case GL_REPEAT: return "GL_REPEAT";
            case GL_CLAMP_TO_EDGE: return "GL_CLAMP_TO_EDGE";
            case GL_MIRRORED_REPEAT: return "GL_MIRRORED_REPEAT";
            case GL_NEAREST: return "GL_NEAREST";
            case GL_LINEAR: return "GL_LINEAR";
            case GL_NEAREST_MIPMAP_NEAREST: return "GL_NEAREST_MIPMAP_NEAREST";
            case GL_LINEAR_MIPMAP_NEAREST: return "GL_LINEAR_MIPMAP_NEAREST";
            case GL_NEAREST_MIPMAP_LINEAR: return "GL_NEAREST_MIPMAP_LINEAR";
            case GL_LINEAR_MIPMAP_LINEAR: return "GL_LINEAR_MIPMAP_LINEAR";
            default: return "GL_ENUM(" + std::to_string((int)e) + ")";
        }
    }

    static void dumpRGBAtoPNG(const std::filesystem::path& outPath, const CPUTexture& t) {
        if (t.rgba.empty() || t.width == 0 || t.height == 0) return;
        try {
            std::filesystem::create_directories(outPath.parent_path());
        } catch (const std::exception& e) {
            if (envTruthy("PAC_GLTF_DEBUG_ALL") || envTruthy("PAC_GLTF_DUMP_TEXTURES")) {
                std::cerr << "[gltf][TEX] create_directories failed: " << e.what() << "\n";
            }
        }
        stbi_write_png(outPath.string().c_str(),
                       (int)t.width, (int)t.height,
                       4, t.rgba.data(), (int)t.width * 4);
    }

    static CPUTexture decodeTextureFast(const fastgltf::Asset& asset,
                                        const std::filesystem::path& baseDir,
                                        int materialIndex,
                                        TextureKind kind,
                                        bool dbg,
                                        const std::string& modelPath,
                                        int* outTexCoordIndex /*optional*/) {
        if (outTexCoordIndex) *outTexCoordIndex = 0;

        const std::string lowerPath = toLower(modelPath);
        const bool forceDbg = dbg || envTruthy("PAC_GLTF_DEBUG_ALL");
        const std::string prefix = "[gltf][TEX] ";

        auto wantLog = [&]() {
            if (forceDbg) return true;

            // Optional targeted debugging: set PAC_GLTF_DEBUG_MATCH to a comma/semicolon/space-separated list
            // of substrings to match against the model path (case-insensitive).
            const auto envMatch = [&](const char* envVar) -> bool {
                const char* v = std::getenv(envVar);
                if (!v || !*v) return false;
                std::string s(v);
                for (char& c : s) {
                    if (c == ';') c = ',';
                }
                std::stringstream ss(s);
                std::string tok;
                while (std::getline(ss, tok, ',')) {
                    // also split on whitespace inside each token
                    std::string cur;
                    for (char ch : tok) {
                        if (std::isspace((unsigned char)ch)) {
                            if (!cur.empty()) {
                                if (ciContains(lowerPath, cur)) return true;
                                cur.clear();
                            }
                        } else {
                            cur.push_back(ch);
                        }
                    }
                    if (!cur.empty() && ciContains(lowerPath, cur)) return true;
                }
                return false;
            };

            return envMatch("PAC_GLTF_DEBUG_MATCH");
        };

        auto white = makeWhiteCPUTexture();
        auto black = makeBlackCPUTexture();

        if (materialIndex < 0 || materialIndex >= (int)asset.materials.size()) {
            return (kind == TextureKind::BaseColor) ? white : black;
        }

        const auto& mat = asset.materials[(size_t)materialIndex];

        // Pull the textureInfo (if any) WITHOUT copying (fastgltf::TextureInfo is move-only)
        const fastgltf::TextureInfo* texInfoPtr = nullptr;
        if (kind == TextureKind::BaseColor) {
            if (mat.pbrData.baseColorTexture.has_value()) {
                texInfoPtr = &mat.pbrData.baseColorTexture.value();
            }
        } else {
            if (mat.emissiveTexture.has_value()) {
                texInfoPtr = &mat.emissiveTexture.value();
            }
        }


        if (texInfoPtr == nullptr) {
            // BaseColor: 1x1 factor tint, Emissive: 1x1 black
            if (kind == TextureKind::BaseColor) {
                CPUTexture t;
                t.width = 1; t.height = 1;
                t.wrapS = GL_REPEAT;
                t.wrapT = GL_REPEAT;
                t.minF  = GL_LINEAR;
                t.magF  = GL_LINEAR;

                auto f = mat.pbrData.baseColorFactor;
                auto toU8 = [](float x)->uint8_t {
                    x = (std::max)(0.0f, (std::min)(1.0f, x));
                    return (uint8_t)std::lround(x * 255.0f);
                };
                t.rgba = { toU8(f[0]), toU8(f[1]), toU8(f[2]), toU8(f[3]) };

                if (wantLog()) {
                    logTexDecode(prefix, "mat[" + std::to_string(materialIndex) + "] '" +
                        std::string(mat.name.begin(), mat.name.end()) +
                        "' has NO baseColorTexture; using baseColorFactor RGBA8.");
                }
                return t;
            }

            if (wantLog()) {
                logTexDecode(prefix, "mat[" + std::to_string(materialIndex) + "] '" +
                    std::string(mat.name.begin(), mat.name.end()) +
                    "' has NO emissiveTexture; using 1x1 black.");
            }
            return black;
        }

        const auto& texInfo = *texInfoPtr;
        const size_t texIndex = texInfo.textureIndex;
        const int texCoord = (int)texInfo.texCoordIndex;
        if (outTexCoordIndex) *outTexCoordIndex = texCoord;

        if (texIndex >= asset.textures.size()) {
            if (wantLog()) {
                logTexDecode(prefix, "mat[" + std::to_string(materialIndex) + "] textureIndex out of range: " + std::to_string(texIndex));
            }
            return (kind == TextureKind::BaseColor) ? white : black;
        }

        const auto& tex = asset.textures[texIndex];

        // fastgltf exposes multiple possible image indices (regular/webp/basisu/dds)
        fastgltf::Optional<std::size_t> imgIndexOpt = tex.imageIndex;
        const char* imgSlot = "imageIndex";
        if (!imgIndexOpt.has_value()) { imgIndexOpt = tex.webpImageIndex;   imgSlot = "webpImageIndex"; }
        if (!imgIndexOpt.has_value()) { imgIndexOpt = tex.basisuImageIndex; imgSlot = "basisuImageIndex"; }
        if (!imgIndexOpt.has_value()) { imgIndexOpt = tex.ddsImageIndex;    imgSlot = "ddsImageIndex"; }

        if (!imgIndexOpt.has_value()) {
            if (wantLog()) {
                logTexDecode(prefix, "mat[" + std::to_string(materialIndex) + "] tex[" + std::to_string(texIndex) + "] has NO image index (all empty).");
            }
            return (kind == TextureKind::BaseColor) ? white : black;
        }

        const size_t imgIndex = imgIndexOpt.value();
        if (imgIndex >= asset.images.size()) {
            if (wantLog()) {
                logTexDecode(prefix, "mat[" + std::to_string(materialIndex) + "] imageIndex out of range: " + std::to_string(imgIndex));
            }
            return (kind == TextureKind::BaseColor) ? white : black;
        }

        const auto& img = asset.images[imgIndex];
        auto enc = getEncodedImageBytes(asset, baseDir, img);

        if (!enc.has_value() || enc->bytes.empty()) {
            if (wantLog()) {
                logTexDecode(prefix, "mat[" + std::to_string(materialIndex) + "] tex[" + std::to_string(texIndex) +
                    "] img[" + std::to_string(imgIndex) + "] (" + imgSlot + ") encoded bytes EMPTY (" +
                    (enc.has_value() ? enc->debugName : std::string("no enc")) + ")");
            }
            return (kind == TextureKind::BaseColor) ? white : black;
        }

        // Try decode via stb_image
        int w = 0, h = 0, comp = 0;
        stbi_uc* decoded = stbi_load_from_memory(
            enc->bytes.data(),
            (int)enc->bytes.size(),
            &w, &h, &comp, 4
        );

        if (wantLog()) {
            const std::string matName(mat.name.begin(), mat.name.end());
            const char* fmt = magicName(enc->bytes);
            std::string msg = "mat[" + std::to_string(materialIndex) + "] '" + matName + "' ";
            msg += (kind == TextureKind::BaseColor) ? "BaseColor" : "Emissive";
            msg += " tex[" + std::to_string(texIndex) + "] img[" + std::to_string(imgIndex) + "](" + imgSlot + ")";
            msg += " texCoord=" + std::to_string(texCoord);
            msg += " src='" + enc->debugName + "'";
            msg += " encodedBytes=" + std::to_string(enc->bytes.size());
            msg += " magic=" + std::string(fmt);
            msg += " stbi=" + std::string(decoded ? "OK" : "FAIL");
            if (decoded) {
                msg += " w=" + std::to_string(w) + " h=" + std::to_string(h) + " comp=" + std::to_string(comp);
            } else {
                const char* reason = stbi_failure_reason();
                msg += " reason='" + std::string(reason ? reason : "unknown") + "'";
                if (std::string(fmt) == "KTX2" || std::string(fmt) == "DDS") {
                    msg += " (likely unsupported compressed texture format)";
                }
            }
            logTexDecode(prefix, msg);
        }

        if (decoded == nullptr || w <= 0 || h <= 0) {
            if (decoded) stbi_image_free(decoded);
            return (kind == TextureKind::BaseColor) ? white : black;
        }

        CPUTexture out;
        out.width  = (uint32_t)w;
        out.height = (uint32_t)h;

        // defaults
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

        // glTF spec: baseColor = texture * baseColorFactor
        if (kind == TextureKind::BaseColor) {
            auto f = mat.pbrData.baseColorFactor;

            const float fr = f[0];
            const float fg = f[1];
            const float fb = f[2];
            const float fa = f[3];

            if (fr != 1.0f || fg != 1.0f || fb != 1.0f || fa != 1.0f) {
                auto mulClampU8 = [](uint8_t v, float factor) -> uint8_t {
                    float x = (float)v * factor;
                    x = (std::max)(0.0f, (std::min)(255.0f, x));
                    return (uint8_t)std::lround(x);
                };

                for (size_t i = 0; i + 3 < out.rgba.size(); i += 4) {
                    out.rgba[i + 0] = mulClampU8(out.rgba[i + 0], fr);
                    out.rgba[i + 1] = mulClampU8(out.rgba[i + 1], fg);
                    out.rgba[i + 2] = mulClampU8(out.rgba[i + 2], fb);
                    out.rgba[i + 3] = mulClampU8(out.rgba[i + 3], fa);
                }
            }
        }

        stbi_image_free(decoded);

        // Optional texture dumps (only when explicitly enabled; can be large)
        if (wantLog() && envTruthy("PAC_GLTF_DUMP_TEXTURES")) {
            const std::string matName = std::string(mat.name.begin(), mat.name.end());
            std::string kindStr = (kind == TextureKind::BaseColor) ? "base" : "emissive";
            std::filesystem::path dumpDir = std::filesystem::path("debug") / "gltf_textures";
            std::filesystem::path outP = dumpDir / (std::filesystem::path(modelPath).stem().string()
                + "_mat" + std::to_string(materialIndex) + "_" + kindStr + ".png");
            dumpRGBAtoPNG(outP, out);
            logTexDecode(prefix, "dumped " + kindStr + " -> " + outP.string() +
                " (" + std::to_string(out.width) + "x" + std::to_string(out.height) + ", wrapS=" +
                glEnumName(out.wrapS) + ", wrapT=" + glEnumName(out.wrapT) + ")");
        }

        return out;
    }

    static CPUTexture decodeBaseColorTextureFast(const fastgltf::Asset& asset,
                                                 const std::filesystem::path& baseDir,
                                                 int materialIndex,
                                                 bool dbg,
                                                 const std::string& modelPath,
                                                 int* outTexCoordIndex /*optional*/) {
        return decodeTextureFast(asset, baseDir, materialIndex, TextureKind::BaseColor, dbg, modelPath, outTexCoordIndex);
    }

    static CPUTexture decodeEmissiveTextureFast(const fastgltf::Asset& asset,
                                                const std::filesystem::path& baseDir,
                                                int materialIndex,
                                                bool dbg,
                                                const std::string& modelPath,
                                                int* outTexCoordIndex /*optional*/) {
        return decodeTextureFast(asset, baseDir, materialIndex, TextureKind::Emissive, dbg, modelPath, outTexCoordIndex);
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
    auto fg = pac::fastgltf_loader::tryLoad(filepath);
    if (!fg.has_value()) {
        std::cerr << "[gltf][FASTGLTF] FAILED to parse: " << filepath << "\n";
        return;
    }

    const fastgltf::Asset& asset = fg->asset;

    const bool dbgThisModel = FG::envTruthy("PAC_GLTF_DEBUG") || FG::ciContains(filepath, "0019_rattata") || FG::ciContains(filepath, "rattata");
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
                if (!samp.inputs.empty()) {
                    clip.durationSec = (std::max)(clip.durationSec, samp.inputs.back());
                }
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
            int requiredTexCoord = FG::requiredTexCoordForMaterial(asset, materialIndex);

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
            FG::CPUTexture baseCPU = FG::decodeBaseColorTextureFast(asset, fg->baseDir, materialIndex, dbgThisModel, filepath, &baseTexCoordUsed);
            FG::CPUTexture emissiveCPU = FG::decodeEmissiveTextureFast(asset, fg->baseDir, materialIndex, dbgThisModel, filepath, &emissiveTexCoordUsed);
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
