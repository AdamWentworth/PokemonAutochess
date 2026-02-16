// EvolutionConfigLoader.h
#pragma once

#include <cstddef>
#include <string>
#include <unordered_map>

namespace LogBus { class Logger; }
namespace engine { class IAssetStore; }

struct EvolutionRule {
    std::string evolvesTo;
    int level = 0;
};

class EvolutionConfigLoader {
public:
    bool loadConfig(const std::string& filePath,
                    LogBus::Logger* logger = nullptr,
                    const engine::IAssetStore* store = nullptr);

    const EvolutionRule* getRule(const std::string& species) const;
    bool getPreEvolution(const std::string& species, std::string& outPreEvolution) const;
    std::size_t ruleCount() const { return rules_.size(); }

private:
    std::unordered_map<std::string, EvolutionRule> rules_;
};
