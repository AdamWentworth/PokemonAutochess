// MovesConfigLoader.h
#pragma once
#include <string>
#include <unordered_map>
#include <../../third_party/nlohmann/json.hpp>

struct MoveStatus {
    std::string effect;
    float magnitude = 0.0f;
    float durationSec = 0.0f;
    std::string target;
    bool valid = false;
};

struct MoveData {
    std::string name;
    std::string type;        // fire, water, etc
    std::string kind;        // "fast" or "charged"
    float cooldownSec = 0.5f;
    int   power = 0;
    float range = 1.5f;

    // Fast: energyGain; Charged: energyCost
    int   energyGain = 0;
    int   energyCost = 0;

    MoveStatus status;
};

class MovesConfigLoader {
public:
    static MovesConfigLoader& getInstance();

    bool loadConfig(const std::string& filePath);
    const MoveData* getMove(const std::string& name) const;

private:
    MovesConfigLoader() = default;
    std::unordered_map<std::string, MoveData> moves_;
};
