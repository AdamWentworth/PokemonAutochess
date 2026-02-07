// src/game/assets/PackedAssetStore.h
#pragma once

#include <cstdint>
#include <fstream>
#include <string>
#include <unordered_map>
#include <vector>

#include "engine/core/IAssetStore.h"

namespace game::assets {

// PackedAssetStore: placeholder for a future packaged content backend.
class PackedAssetStore final : public engine::IAssetStore {
public:
    PackedAssetStore() = default;

    // In a future iteration, load a packed bundle on disk.
    bool open(const std::string& packagePath, std::string* outError = nullptr);
    bool isOpen() const { return opened_; }

    bool readText(const std::string& virtualPath,
                  std::string& outText,
                  std::string* outError = nullptr) const override;

    bool readBytes(const std::string& virtualPath,
                   std::vector<std::uint8_t>& outBytes,
                   std::string* outError = nullptr) const override;

    bool exists(const std::string& virtualPath) const override;

private:
    struct Entry {
        std::uint64_t offset = 0;
        std::uint64_t size = 0;
    };

    std::string normalizePath(const std::string& virtualPath) const;

    bool opened_ = false;
    std::string packagePath_;
    mutable std::ifstream file_;
    std::unordered_map<std::string, Entry> entries_;
};

} // namespace game::assets
