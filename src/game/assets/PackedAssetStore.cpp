// src/game/assets/PackedAssetStore.cpp
#include "game/assets/PackedAssetStore.h"

namespace game::assets {

bool PackedAssetStore::open(const std::string& packagePath, std::string* outError) {
    (void)packagePath;
    if (outError) *outError = "PackedAssetStore not implemented";
    opened_ = false;
    packagePath_.clear();
    return false;
}

bool PackedAssetStore::readText(const std::string& virtualPath,
                                std::string& outText,
                                std::string* outError) const {
    (void)virtualPath;
    outText.clear();
    if (outError) *outError = "PackedAssetStore not implemented";
    return false;
}

bool PackedAssetStore::readBytes(const std::string& virtualPath,
                                 std::vector<std::uint8_t>& outBytes,
                                 std::string* outError) const {
    (void)virtualPath;
    outBytes.clear();
    if (outError) *outError = "PackedAssetStore not implemented";
    return false;
}

bool PackedAssetStore::exists(const std::string& virtualPath) const {
    (void)virtualPath;
    return false;
}

} // namespace game::assets
