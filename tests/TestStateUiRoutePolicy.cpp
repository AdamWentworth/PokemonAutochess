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

bool test_state_ui_route_policy_contract(std::string& outFail) {
    const std::filesystem::path stateRoot = "src/game/state";
    const std::vector<std::string> forbiddenTokens = {
        "services.usesLegacyGameUiPath(",
        "services.usesBackendGameUiPath("
    };

    if (!std::filesystem::exists(stateRoot)) {
        outFail = "missing state root: " + stateRoot.string();
        return false;
    }

    for (const auto& entry : std::filesystem::recursive_directory_iterator(stateRoot)) {
        if (!entry.is_regular_file()) continue;
        const auto filePath = entry.path();
        if (!isSourceExtension(filePath)) continue;

        const std::string normalized = normalizePath(filePath);
        for (const auto& token : forbiddenTokens) {
            if (!fileContainsToken(filePath, token)) continue;
            outFail = "state UI route policy violation: " + normalized +
                      " contains '" + token +
                      "'; use routesFromServices + backend_ui policy helpers instead";
            return false;
        }
    }

    return true;
}

