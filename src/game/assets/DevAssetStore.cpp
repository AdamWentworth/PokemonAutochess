// src/game/assets/DevAssetStore.cpp
#include "game/assets/DevAssetStore.h"

#include <filesystem>
#include <fstream>
#include <sstream>

namespace game::assets {

DevAssetStore::DevAssetStore(std::string rootPath) {
    setRoot(std::move(rootPath));
}

void DevAssetStore::setRoot(std::string rootPath) {
    if (rootPath.empty()) rootPath = ".";
    rootPath_ = std::move(rootPath);
}

std::string DevAssetStore::resolvePath(const std::string& virtualPath) const {
    std::filesystem::path root(rootPath_);
    std::filesystem::path rel(virtualPath);
    auto combined = (root / rel).lexically_normal();
    return combined.string();
}

bool DevAssetStore::readText(const std::string& virtualPath,
                             std::string& outText,
                             std::string* outError) const {
    const std::string fullPath = resolvePath(virtualPath);
    std::ifstream in(fullPath, std::ios::in | std::ios::binary);
    if (!in) {
        if (outError) *outError = "Failed to open " + fullPath;
        return false;
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    outText = ss.str();
    return true;
}

bool DevAssetStore::readBytes(const std::string& virtualPath,
                              std::vector<std::uint8_t>& outBytes,
                              std::string* outError) const {
    const std::string fullPath = resolvePath(virtualPath);
    std::ifstream in(fullPath, std::ios::binary | std::ios::ate);
    if (!in) {
        if (outError) *outError = "Failed to open " + fullPath;
        return false;
    }
    const std::ifstream::pos_type size = in.tellg();
    if (size < 0) {
        if (outError) *outError = "Failed to read " + fullPath;
        return false;
    }
    outBytes.resize(static_cast<size_t>(size));
    in.seekg(0, std::ios::beg);
    if (!outBytes.empty()) {
        in.read(reinterpret_cast<char*>(outBytes.data()), size);
    }
    return true;
}

bool DevAssetStore::exists(const std::string& virtualPath) const {
    const std::string fullPath = resolvePath(virtualPath);
    return std::filesystem::exists(fullPath);
}

} // namespace game::assets
