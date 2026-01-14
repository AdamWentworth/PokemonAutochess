// src/engine/render/Model.cpp

#include "Model.h"

#include <glad/glad.h>
#include <iostream>
#include <vector>
#include <limits>
#include <algorithm>
#include <cmath>
#include <functional>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <cstring>
#include <type_traits>
#include <utility>

#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "../utils/ShaderLibrary.h"

#include <nlohmann/json.hpp>

// fastgltf loader
#include "./FastGLTFLoader.h"
#include <fastgltf/tools.hpp>
#include <fastgltf/glm_element_traits.hpp>

#include <stb_image.h>

#ifndef PAC_VERBOSE_STARTUP
#define PAC_VERBOSE_STARTUP 0
#endif

#if PAC_VERBOSE_STARTUP
    #define STARTUP_LOG(msg) do { std::cout << msg << "\n"; } while(0)
#else
    #define STARTUP_LOG(msg) do {} while(0)
#endif

bool isMipmapMinFilter(GLint minF);

#include "ModelCache.h"

namespace fs = std::filesystem;

bool isMipmapMinFilter(GLint minF) {
    return minF == GL_NEAREST_MIPMAP_NEAREST ||
           minF == GL_LINEAR_MIPMAP_NEAREST ||
           minF == GL_NEAREST_MIPMAP_LINEAR ||
           minF == GL_LINEAR_MIPMAP_LINEAR;
}

namespace {

// detects o.has_value()
template <typename T, typename = void>
struct fg_has_has_value : std::false_type {};
template <typename T>
struct fg_has_has_value<T, std::void_t<decltype(std::declval<const T&>().has_value())>>
    : std::true_type {};

// detects o.value()  (function)
template <typename T, typename = void>
struct fg_has_value_fn : std::false_type {};
template <typename T>
struct fg_has_value_fn<T, std::void_t<decltype(std::declval<const T&>().value())>>
    : std::true_type {};

// detects o.get() (function)
template <typename T, typename = void>
struct fg_has_get : std::false_type {};
template <typename T>
struct fg_has_get<T, std::void_t<decltype(std::declval<const T&>().get())>>
    : std::true_type {};

// detects o.value (data member)
template <typename T, typename = void>
struct fg_has_value_member : std::false_type {};
template <typename T>
struct fg_has_value_member<T, std::void_t<decltype((std::declval<const T&>().value))>>
    : std::true_type {};

// detects *o
template <typename T, typename = void>
struct fg_is_deref : std::false_type {};
template <typename T>
struct fg_is_deref<T, std::void_t<decltype(*std::declval<const T&>())>>
    : std::true_type {};

template <typename Opt>
bool fgOptHas(const Opt& o) {
    if constexpr (std::is_integral_v<std::decay_t<Opt>> || std::is_enum_v<std::decay_t<Opt>>) {
        return true; // plain value is always "present"
    } else if constexpr (fg_has_has_value<Opt>::value) {
        return o.has_value();
    } else {
        return static_cast<bool>(o);
    }
}

template <typename Opt>
std::size_t fgOptGet(const Opt& o) {
    // if it's already an integer (or enum), just return it
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

// ------------------------------------------------------------
// TRS helper
// ------------------------------------------------------------
glm::mat4 Model::trsToMat4(const NodeTRS& n)
{
    if (n.hasMatrix) return n.matrix;

    glm::mat4 T = glm::translate(glm::mat4(1.0f), n.t);
    glm::mat4 R = glm::toMat4(n.r);
    glm::mat4 S = glm::scale(glm::mat4(1.0f), n.s);
    return T * R * S;
}

Model::Model(const std::string& filepath)
{
    loadGLTF(filepath);

    modelShader = ShaderLibrary::get("assets/shaders/model/model.vert", "assets/shaders/model/model.frag");

    locMVP     = glGetUniformLocation(modelShader->getID(), "u_MVP");
    locUseSkin = glGetUniformLocation(modelShader->getID(), "u_UseSkin");
    locJoints0 = glGetUniformLocation(modelShader->getID(), "u_Joints[0]");

    if (locMVP < 0)     std::cerr << "[Model] WARNING: u_MVP not found\n";
    if (locUseSkin < 0) std::cerr << "[Model] WARNING: u_UseSkin not found\n";
    if (locJoints0 < 0) std::cerr << "[Model] WARNING: u_Joints[0] not found\n";
}

Model::~Model()
{
    if (VAO) glDeleteVertexArrays(1, &VAO);
    if (VBO) glDeleteBuffers(1, &VBO);
    if (EBO) glDeleteBuffers(1, &EBO);

    for (auto& sm : submeshes) {
        if (sm.textureID) glDeleteTextures(1, &sm.textureID);
    }

    modelShader.reset();
}

int Model::getAnimationCount() const
{
    return static_cast<int>(animations.size());
}

float Model::getAnimationDurationSec(int animIndex) const
{
    if (animIndex < 0 || animIndex >= (int)animations.size()) return 0.0f;
    return animations[animIndex].durationSec;
}

// ------------------------------------------------------------
// Load glTF: cache wrapper + fastgltf loader
// ------------------------------------------------------------
void Model::loadGLTF(const std::string& filepath)
{
    if (tryLoadCache(filepath)) {
        std::cerr << "[gltf][CACHE] HIT (no parsing) for: " << filepath << "\n";
        return;
    }
    std::cerr << "[gltf][CACHE] MISS (will parse) for: " << filepath << "\n";

    // Full loader lives here (step 1 modularization)
    #include "ModelFastGltfLoad.inl"
}
