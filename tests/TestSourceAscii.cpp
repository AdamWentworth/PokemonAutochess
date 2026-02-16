#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

bool shouldCheck(const std::filesystem::path& path) {
    static const std::vector<std::string> exts = {
        ".h", ".hpp", ".hh", ".inl", ".ipp",
        ".c", ".cc", ".cpp", ".cxx"
    };
    const std::string ext = path.extension().string();
    return std::find(exts.begin(), exts.end(), ext) != exts.end();
}

std::string toHexByte(std::uint8_t b) {
    static constexpr char kHex[] = "0123456789ABCDEF";
    std::string out = "0x";
    out.push_back(kHex[(b >> 4) & 0x0F]);
    out.push_back(kHex[b & 0x0F]);
    return out;
}

bool findNonAsciiByte(const std::filesystem::path& filePath,
                      int& outLine,
                      int& outColumn,
                      std::uint8_t& outByte,
                      std::string& outError) {
    std::ifstream in(filePath, std::ios::binary);
    if (!in.is_open()) {
        outError = "failed to open file";
        return false;
    }

    std::vector<std::uint8_t> data((std::istreambuf_iterator<char>(in)),
                                   std::istreambuf_iterator<char>());

    std::size_t start = 0;
    if (data.size() >= 3 && data[0] == 0xEF && data[1] == 0xBB && data[2] == 0xBF) {
        // Allow UTF-8 BOM.
        start = 3;
    }

    int line = 1;
    int col = 1;
    for (std::size_t i = start; i < data.size(); ++i) {
        const std::uint8_t b = data[i];
        if (b > 0x7F) {
            outLine = line;
            outColumn = col;
            outByte = b;
            return true;
        }
        if (b == '\n') {
            ++line;
            col = 1;
            continue;
        }
        if (b != '\r') {
            ++col;
        }
    }

    return false;
}

}  // namespace

bool test_source_ascii_hygiene(std::string& outFail) {
    const std::vector<std::filesystem::path> roots = {
        "src",
        "tests",
    };

    for (const auto& root : roots) {
        if (!std::filesystem::exists(root)) {
            outFail = "source root missing: " + root.string();
            return false;
        }

        for (const auto& entry : std::filesystem::recursive_directory_iterator(root)) {
            if (!entry.is_regular_file()) continue;
            if (!shouldCheck(entry.path())) continue;

            int line = 0;
            int col = 0;
            std::uint8_t badByte = 0;
            std::string ioErr;
            if (findNonAsciiByte(entry.path(), line, col, badByte, ioErr)) {
                outFail = "non-ASCII byte in " + entry.path().string() +
                          ":" + std::to_string(line) + ":" + std::to_string(col) +
                          " (" + toHexByte(badByte) + ")";
                return false;
            }
            if (!ioErr.empty()) {
                outFail = ioErr + ": " + entry.path().string();
                return false;
            }
        }
    }

    return true;
}
