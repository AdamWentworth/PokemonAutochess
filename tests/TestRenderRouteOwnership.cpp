#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

bool isSourceExtension(const std::filesystem::path& path) {
    const std::string ext = path.extension().string();
    return ext == ".h" || ext == ".hpp" || ext == ".cpp" || ext == ".cc";
}

std::string normalizePath(std::filesystem::path path) {
    path.make_preferred();
    std::string s = path.string();
    std::replace(s.begin(), s.end(), '\\', '/');
    return s;
}

bool fileContainsToken(const std::filesystem::path& path, const std::string& token) {
    std::ifstream in(path);
    if (!in.is_open()) return false;
    std::string line;
    while (std::getline(in, line)) {
        if (line.find(token) != std::string::npos) return true;
    }
    return false;
}

} // namespace

bool test_render_route_ownership_contract(std::string& outFail) {
    const std::filesystem::path gameRoot = "src/game";
    const std::string allowedFile = "src/game/runtime/GameSession.cpp";
    const std::vector<std::string> tokens = {
        "prefersLegacyGameRenderPath(",
        "prefersLegacyGameUiPath("
    };

    if (!std::filesystem::exists(gameRoot)) {
        outFail = "missing game root: " + gameRoot.string();
        return false;
    }

    for (const auto& entry : std::filesystem::recursive_directory_iterator(gameRoot)) {
        if (!entry.is_regular_file()) continue;
        const auto filePath = entry.path();
        if (!isSourceExtension(filePath)) continue;

        const std::string normalized = normalizePath(filePath);
        if (normalized == allowedFile) continue;

        for (const auto& token : tokens) {
            if (!fileContainsToken(filePath, token)) continue;
            outFail = "route ownership violation: " + normalized +
                      " contains '" + token + "' (only " + allowedFile + " may read backend route preferences)";
            return false;
        }
    }

    return true;
}
