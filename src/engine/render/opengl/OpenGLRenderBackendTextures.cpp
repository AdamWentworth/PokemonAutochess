#include "engine/render/OpenGLRenderBackend.h"

#include <algorithm>
#include <string>
#include <vector>

#include <glad/glad.h>
#include <stb_image.h>

namespace {

GLint sanitizeWrapMode(int wrap) {
    switch (wrap) {
    case 33071: return GL_CLAMP_TO_EDGE;
    case 33648: return GL_MIRRORED_REPEAT;
    case 10497: return GL_REPEAT;
    default: return GL_REPEAT;
    }
}

constexpr const char* kFallbackSpriteTextureKey = "__fallback_sprite_texture__";

} // namespace

unsigned int OpenGLRenderBackend::ensureWorldTexture(const WorldTextureData* textureData) {
    if (!textureData || !textureData->rgba || textureData->width <= 0 || textureData->height <= 0 ||
        !textureData->key || textureData->key[0] == '\0') {
        return 0;
    }

    const std::string key(textureData->key);
    auto existing = worldTextures_.find(key);
    if (existing != worldTextures_.end()) {
        return existing->second.textureId;
    }

    unsigned int textureId = 0;
    glGenTextures(1, &textureId);
    if (textureId == 0) return 0;

    const GLint wrapS = sanitizeWrapMode(textureData->wrapS);
    const GLint wrapT = sanitizeWrapMode(textureData->wrapT);
    glBindTexture(GL_TEXTURE_2D, textureId);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrapS);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrapT);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D,
                 0,
                 GL_RGBA8,
                 textureData->width,
                 textureData->height,
                 0,
                 GL_RGBA,
                 GL_UNSIGNED_BYTE,
                 textureData->rgba);

    TextureCacheEntry entry;
    entry.textureId = textureId;
    entry.width = textureData->width;
    entry.height = textureData->height;
    entry.wrapS = textureData->wrapS;
    entry.wrapT = textureData->wrapT;
    worldTextures_.emplace(key, entry);
    return textureId;
}

unsigned int OpenGLRenderBackend::ensureSpriteTexture(const std::string& texturePath) {
    ensureSpritePipeline();
    if (texturePath.empty()) return spriteFallbackTexture_;

    auto existing = spriteTextures_.find(texturePath);
    if (existing != spriteTextures_.end()) return existing->second;

    int width = 0;
    int height = 0;
    int channels = 0;
    stbi_set_flip_vertically_on_load(false);
    unsigned char* pixels = stbi_load(texturePath.c_str(), &width, &height, &channels, 4);
    if (!pixels) {
        std::string altPath = texturePath;
        std::replace(altPath.begin(), altPath.end(), '\\', '/');
        if (altPath != texturePath) {
            stbi_set_flip_vertically_on_load(false);
            pixels = stbi_load(altPath.c_str(), &width, &height, &channels, 4);
        }
    }

    if (!pixels || width <= 0 || height <= 0) {
        if (pixels) stbi_image_free(pixels);
        return spriteFallbackTexture_;
    }

    unsigned int textureId = 0;
    glGenTextures(1, &textureId);
    if (textureId == 0) {
        stbi_image_free(pixels);
        return spriteFallbackTexture_;
    }

    glBindTexture(GL_TEXTURE_2D, textureId);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D,
                 0,
                 GL_RGBA8,
                 width,
                 height,
                 0,
                 GL_RGBA,
                 GL_UNSIGNED_BYTE,
                 pixels);
    glGenerateMipmap(GL_TEXTURE_2D);
#if defined(GL_TEXTURE_MAX_ANISOTROPY_EXT) && defined(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT)
    if (GLAD_GL_EXT_texture_filter_anisotropic) {
        float maxAniso = 1.0f;
        glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &maxAniso);
        const float requested = std::min(8.0f, std::max(1.0f, maxAniso));
        glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, requested);
    }
#endif
    stbi_image_free(pixels);

    spriteTextures_[texturePath] = textureId;
    return textureId;
}

void OpenGLRenderBackend::clearTextureCaches() {
    std::vector<unsigned int> texturesToDelete;
    texturesToDelete.reserve(worldTextures_.size() + spriteTextures_.size() + 2u);
    for (const auto& [_, entry] : worldTextures_) {
        if (entry.textureId != 0) texturesToDelete.push_back(entry.textureId);
    }
    for (const auto& [_, textureId] : spriteTextures_) {
        if (textureId != 0) texturesToDelete.push_back(textureId);
    }
    if (worldFallbackTexture_ != 0) texturesToDelete.push_back(worldFallbackTexture_);
    if (spriteFallbackTexture_ != 0) texturesToDelete.push_back(spriteFallbackTexture_);
    if (!texturesToDelete.empty()) {
        std::sort(texturesToDelete.begin(), texturesToDelete.end());
        texturesToDelete.erase(std::unique(texturesToDelete.begin(), texturesToDelete.end()), texturesToDelete.end());
        glDeleteTextures(static_cast<GLsizei>(texturesToDelete.size()), texturesToDelete.data());
    }
    worldTextures_.clear();
    spriteTextures_.clear();
    worldFallbackTexture_ = 0;
    spriteFallbackTexture_ = 0;
}
