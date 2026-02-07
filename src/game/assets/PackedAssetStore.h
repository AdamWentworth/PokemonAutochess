// src/game/assets/PackedAssetStore.h
#pragma once

#include <string>
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
    bool opened_ = false;
    std::string packagePath_;
};

} // namespace game::assets
