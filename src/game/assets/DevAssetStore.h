// src/game/assets/DevAssetStore.h
#pragma once

#include <string>
#include <vector>

#include "engine/core/IAssetStore.h"

namespace game::assets {

// DevAssetStore: reads assets directly from the filesystem.
// Virtual paths are resolved relative to the configured root.
class DevAssetStore final : public engine::IAssetStore {
public:
    DevAssetStore() = default;
    explicit DevAssetStore(std::string rootPath);

    void setRoot(std::string rootPath);
    const std::string& root() const { return rootPath_; }

    bool readText(const std::string& virtualPath,
                  std::string& outText,
                  std::string* outError = nullptr) const override;

    bool readBytes(const std::string& virtualPath,
                   std::vector<std::uint8_t>& outBytes,
                   std::string* outError = nullptr) const override;

    bool exists(const std::string& virtualPath) const override;

private:
    std::string rootPath_ = ".";
    std::string resolvePath(const std::string& virtualPath) const;
};

} // namespace game::assets
