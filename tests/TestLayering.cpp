// tests/TestLayering.cpp
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {
std::string toLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c){ return static_cast<char>(std::tolower(c)); });
    return s;
}

std::string stripComments(const std::string& line, bool& inBlock) {
    std::string out;
    std::size_t i = 0;
    while (i < line.size()) {
        if (!inBlock) {
            if (i + 1 < line.size() && line[i] == '/' && line[i + 1] == '*') {
                inBlock = true;
                i += 2;
                continue;
            }
            if (i + 1 < line.size() && line[i] == '/' && line[i + 1] == '/') {
                break; // line comment
            }
            out.push_back(line[i++]);
        } else {
            if (i + 1 < line.size() && line[i] == '*' && line[i + 1] == '/') {
                inBlock = false;
                i += 2;
                continue;
            }
            ++i;
        }
    }
    return out;
}

std::string ltrim(std::string s) {
    std::size_t i = 0;
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) ++i;
    s.erase(0, i);
    return s;
}

bool isGameInclude(const std::string& path) {
    std::string p = path;
    std::replace(p.begin(), p.end(), '\\', '/');
    std::string seg;
    std::istringstream ss(p);
    while (std::getline(ss, seg, '/')) {
        if (seg.empty() || seg == "." || seg == "..") continue;
        if (toLower(seg) == "game") return true;
    }
    return false;
}

bool hasForbiddenInclude(const std::filesystem::path& filePath,
                         std::string& outInclude) {
    std::ifstream f(filePath);
    if (!f) return false;

    bool inBlock = false;
    std::string line;
    while (std::getline(f, line)) {
        const std::string stripped = stripComments(line, inBlock);
        std::string trimmed = ltrim(stripped);
        if (trimmed.rfind("#include", 0) != 0) continue;

        std::size_t start = trimmed.find_first_of("\"<");
        if (start == std::string::npos) continue;
        const char endCh = (trimmed[start] == '<') ? '>' : '"';
        std::size_t end = trimmed.find(endCh, start + 1);
        if (end == std::string::npos || end <= start + 1) continue;

        std::string inc = trimmed.substr(start + 1, end - start - 1);
        if (isGameInclude(inc)) {
            outInclude = inc;
            return true;
        }
    }
    return false;
}
} // namespace

bool test_layering_engine_no_game_includes(std::string& outFail) {
#ifndef PAC_PHLOSION_ENGINE_SOURCE_DIR
    outFail = "PAC_PHLOSION_ENGINE_SOURCE_DIR is not defined.";
    return false;
#else
    const std::filesystem::path engineRoot =
        std::filesystem::path(PAC_PHLOSION_ENGINE_SOURCE_DIR) / "src/engine";
#endif
#ifndef PAC_PHLOSION_VFX_SOURCE_DIR
    outFail = "PAC_PHLOSION_VFX_SOURCE_DIR is not defined.";
    return false;
#else
    const std::filesystem::path vfxRoot =
        std::filesystem::path(PAC_PHLOSION_VFX_SOURCE_DIR) / "src/vfx";
#endif
    if (!std::filesystem::exists(engineRoot)) {
        outFail = "Engine root not found: " + engineRoot.string();
        return false;
    }
    if (!std::filesystem::exists(vfxRoot)) {
        outFail = "VFX root not found: " + vfxRoot.string();
        return false;
    }

    const std::vector<std::string> exts = {
        ".h", ".hpp", ".hh", ".inl", ".ipp",
        ".c", ".cc", ".cxx", ".cpp"
    };

    const std::vector<std::filesystem::path> reusableRoots = {
        engineRoot,
        vfxRoot,
    };
    for (const auto& reusableRoot : reusableRoots) {
        for (const auto& entry :
             std::filesystem::recursive_directory_iterator(reusableRoot)) {
            if (!entry.is_regular_file()) continue;
            const auto ext = entry.path().extension().string();
            if (std::find(exts.begin(), exts.end(), ext) == exts.end()) continue;

            std::string includePath;
            if (hasForbiddenInclude(entry.path(), includePath)) {
                outFail = "Reusable source includes game header: " +
                          entry.path().string() + " -> " + includePath;
                return false;
            }
        }
    }

    return true;
}
