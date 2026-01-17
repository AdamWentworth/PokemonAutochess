// src/engine/render/Model.cpp

#include "Model.h"
#include "ModelStartupLog.h"
#include "../utils/ShaderLibrary.h"

#include <iostream>
#include <fstream>
#include <filesystem>
#include <algorithm>
#include <type_traits>
#include <utility>

#include <glad/glad.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>

// FastGLTF loader
#include "FastGLTFLoader.h"
#include <fastgltf/tools.hpp>
#include <fastgltf/glm_element_traits.hpp>

#include <nlohmann/json.hpp>

// stb_image (IMPLEMENTATION IS PROVIDED ELSEWHERE in your project)
#include <stb_image.h>

namespace fs = std::filesystem;

// Helper used by cache and runtime: detect whether a GL min filter implies mipmaps.
bool isMipmapMinFilter(GLint minF)
{
    switch (minF) {
        case GL_NEAREST_MIPMAP_NEAREST:
        case GL_NEAREST_MIPMAP_LINEAR:
        case GL_LINEAR_MIPMAP_NEAREST:
        case GL_LINEAR_MIPMAP_LINEAR:
            return true;
        default:
            return false;
    }
}

namespace {

// ---- Optional-like helpers used by ModelFastGltfLoad.inl ----
// These were present in your earlier code dump. 

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

glm::mat4 Model::trsToMat4(const NodeTRS& n)
{
    if (n.hasMatrix) {
        return n.matrix;
    }
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

    // material uniforms
    locBaseColorTex   = glGetUniformLocation(modelShader->getID(), "u_BaseColorTex");
    locEmissiveTex    = glGetUniformLocation(modelShader->getID(), "u_EmissiveTex");
    locEmissiveFactor = glGetUniformLocation(modelShader->getID(), "u_EmissiveFactor");
    locAlphaMode      = glGetUniformLocation(modelShader->getID(), "u_AlphaMode");
    locAlphaCutoff    = glGetUniformLocation(modelShader->getID(), "u_AlphaCutoff");

    // bind samplers to fixed texture units once
    modelShader->use();
    if (locBaseColorTex >= 0) glUniform1i(locBaseColorTex, 0);
    if (locEmissiveTex  >= 0) glUniform1i(locEmissiveTex, 1);
}

Model::~Model()
{
    if (VAO) glDeleteVertexArrays(1, &VAO);
    if (VBO) glDeleteBuffers(1, &VBO);
    if (EBO) glDeleteBuffers(1, &EBO);

    for (auto& sm : submeshes) {
        if (sm.baseColorTexID) glDeleteTextures(1, &sm.baseColorTexID);
        if (sm.emissiveTexID)  glDeleteTextures(1, &sm.emissiveTexID);
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
    return animations[(size_t)animIndex].durationSec;
}

// IMPORTANT: the .inl is written to be included inside this function body.
void Model::loadGLTF(const std::string& filepath)
{
#include "ModelFastGltfLoad.inl"
}
