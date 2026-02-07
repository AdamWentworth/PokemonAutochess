// src/game/assets/PackedAssetStore.cpp
#include "game/assets/PackedAssetStore.h"

#include <algorithm>
#include <cstring>

#include "engine/core/AssetPackFormat.h"

namespace game::assets {
namespace {
bool readU32(std::ifstream& in, std::uint32_t& out) {
    in.read(reinterpret_cast<char*>(&out), sizeof(out));
    return static_cast<bool>(in);
}

bool readU64(std::ifstream& in, std::uint64_t& out) {
    in.read(reinterpret_cast<char*>(&out), sizeof(out));
    return static_cast<bool>(in);
}
} // namespace

std::string PackedAssetStore::normalizePath(const std::string& virtualPath) const {
    std::string path = virtualPath;
    std::replace(path.begin(), path.end(), '\\', '/');
    while (!path.empty() && (path.front() == '/' || path.front() == '\\')) {
        path.erase(path.begin());
    }
    if (path.rfind("./", 0) == 0) {
        path.erase(0, 2);
    }
    return path;
}

bool PackedAssetStore::open(const std::string& packagePath, std::string* outError) {
    opened_ = false;
    packagePath_.clear();
    entries_.clear();
    if (file_.is_open()) file_.close();

    file_.open(packagePath, std::ios::binary);
    if (!file_) {
        if (outError) *outError = "Failed to open pack: " + packagePath;
        return false;
    }

    file_.seekg(0, std::ios::end);
    const std::uint64_t fileSize = static_cast<std::uint64_t>(file_.tellg());
    file_.seekg(0, std::ios::beg);

    char magic[4] = {};
    file_.read(magic, sizeof(magic));
    if (!file_ || std::memcmp(magic, engine::assets::kPackMagic, sizeof(magic)) != 0) {
        if (outError) *outError = "Invalid pack magic";
        return false;
    }

    std::uint32_t version = 0;
    std::uint32_t entryCount = 0;
    std::uint64_t indexOffset = 0;

    if (!readU32(file_, version) || !readU32(file_, entryCount) || !readU64(file_, indexOffset)) {
        if (outError) *outError = "Failed to read pack header";
        return false;
    }
    if (version != engine::assets::kPackVersion) {
        if (outError) *outError = "Unsupported pack version";
        return false;
    }
    if (indexOffset >= fileSize) {
        if (outError) *outError = "Invalid pack index offset";
        return false;
    }

    file_.seekg(static_cast<std::streamoff>(indexOffset), std::ios::beg);
    for (std::uint32_t i = 0; i < entryCount; ++i) {
        std::uint32_t pathLength = 0;
        std::uint64_t offset = 0;
        std::uint64_t size = 0;

        if (!readU32(file_, pathLength) || !readU64(file_, offset) || !readU64(file_, size)) {
            if (outError) *outError = "Failed to read pack index";
            return false;
        }
        if (pathLength == 0 || pathLength > engine::assets::kPackMaxPathLength) {
            if (outError) *outError = "Invalid path length in pack";
            return false;
        }
        if (offset + size > indexOffset) {
            if (outError) *outError = "Invalid entry range in pack";
            return false;
        }

        std::string path(pathLength, '\0');
        file_.read(path.data(), static_cast<std::streamsize>(pathLength));
        if (!file_) {
            if (outError) *outError = "Failed to read pack path";
            return false;
        }

        const std::string norm = normalizePath(path);
        entries_[norm] = Entry{offset, size};
    }

    opened_ = true;
    packagePath_ = packagePath;
    return true;
}

bool PackedAssetStore::readText(const std::string& virtualPath,
                                std::string& outText,
                                std::string* outError) const {
    std::vector<std::uint8_t> bytes;
    if (!readBytes(virtualPath, bytes, outError)) {
        outText.clear();
        return false;
    }
    outText.assign(bytes.begin(), bytes.end());
    return true;
}

bool PackedAssetStore::readBytes(const std::string& virtualPath,
                                 std::vector<std::uint8_t>& outBytes,
                                 std::string* outError) const {
    outBytes.clear();
    if (!opened_) {
        if (outError) *outError = "Pack is not open";
        return false;
    }

    const std::string norm = normalizePath(virtualPath);
    const auto it = entries_.find(norm);
    if (it == entries_.end()) {
        if (outError) *outError = "Asset not found in pack: " + norm;
        return false;
    }

    const Entry entry = it->second;
    outBytes.resize(static_cast<size_t>(entry.size));
    file_.clear();
    file_.seekg(static_cast<std::streamoff>(entry.offset), std::ios::beg);
    if (!file_) {
        if (outError) *outError = "Failed to seek pack entry";
        return false;
    }
    if (entry.size > 0) {
        file_.read(reinterpret_cast<char*>(outBytes.data()), static_cast<std::streamsize>(entry.size));
        if (!file_) {
            if (outError) *outError = "Failed to read pack entry";
            outBytes.clear();
            return false;
        }
    }
    return true;
}

bool PackedAssetStore::exists(const std::string& virtualPath) const {
    const std::string norm = normalizePath(virtualPath);
    return entries_.find(norm) != entries_.end();
}

} // namespace game::assets
