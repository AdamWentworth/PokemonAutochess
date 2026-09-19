#pragma once
#include <filesystem>
#include <string>
namespace tools::route1_authoring {
bool exportKit(const std::filesystem::path &output, std::string &error);
bool validateScene(const std::string &path, std::string &error);
} // namespace tools::route1_authoring
