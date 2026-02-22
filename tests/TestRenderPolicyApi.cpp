#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

bool fileContains(const std::filesystem::path& path, const std::string& needle) {
    std::ifstream in(path);
    if (!in.is_open()) return false;
    std::string line;
    while (std::getline(in, line)) {
        if (line.find(needle) != std::string::npos) return true;
    }
    return false;
}

} // namespace

bool test_render_policy_api_contract(std::string& outFail) {
    struct Rule {
        std::filesystem::path file;
        std::string forbiddenSignature;
    };

    const std::vector<Rule> rules = {
        {"src/game/runtime/BackendRenderPolicy.h", "shouldRenderBackendDebugLayer(bool "},
        {"src/game/runtime/BackendRenderPolicy.h", "shouldRenderBackendWorldBackdrop(bool "},
        {"src/game/runtime/RenderFlowDecisions.h", "decideFrameRenderFlow(bool "},
        {"src/game/state/BackendUiPolicy.h", "shouldUseBackendUi(bool "},
        {"src/game/state/BackendUiPolicy.h", "shouldRenderBackendTextMenu(bool "}
    };

    for (const auto& rule : rules) {
        if (!std::filesystem::exists(rule.file)) {
            outFail = "missing policy file: " + rule.file.string();
            return false;
        }
        if (fileContains(rule.file, rule.forbiddenSignature)) {
            outFail = "legacy bool policy API reintroduced in " + rule.file.string() +
                      ": " + rule.forbiddenSignature;
            return false;
        }
    }

    return true;
}
