#include "engine/render/RendererParityContract.h"

#include <string>

bool test_renderer_parity_contract_baseline(std::string& outFail) {
    using namespace engine::render::parity_contract;

    const RuntimeConfig baseline = makeBaselineConfig();
    const ValidationResult result = validate(baseline);
    if (!result.ok) {
        outFail = std::string("baseline config must validate: ") + result.message;
        return false;
    }
    if (result.signature != kExpectedBaselineSignature) {
        outFail = std::string("baseline signature drifted: got=") + result.signature +
                  " expected=" + kExpectedBaselineSignature;
        return false;
    }
    return true;
}

bool test_renderer_parity_contract_detects_drift(std::string& outFail) {
    using namespace engine::render::parity_contract;

    RuntimeConfig drifted = makeBaselineConfig();
    drifted.worldSamplerAnisotropy = 0;
    const ValidationResult result = validate(drifted);
    if (result.ok) {
        outFail = "drifted parity config unexpectedly validated";
        return false;
    }
    if (result.message.find("worldSamplerAnisotropy invalid") == std::string::npos) {
        outFail = std::string("unexpected validation message: ") + result.message;
        return false;
    }
    return true;
}
