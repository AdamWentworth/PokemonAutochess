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
    drifted.worldDualSourceBlendPolicyEnabled = false;
    const ValidationResult result = validate(drifted);
    if (result.ok) {
        outFail = "drifted parity config unexpectedly validated";
        return false;
    }
    if (result.message.find("worldDualSourceBlendPolicyEnabled mismatch") ==
        std::string::npos) {
        outFail = std::string("unexpected validation message: ") + result.message;
        return false;
    }

    RuntimeConfig encodingDrift = makeBaselineConfig();
    encodingDrift.neutralPmremEncoding = NeutralPmremEncoding::Rgbm;
    const ValidationResult encodingResult = validate(encodingDrift);
    if (encodingResult.ok ||
        encodingResult.message.find("neutralPmremEncoding mismatch") ==
            std::string::npos) {
        outFail = std::string("environment encoding drift was not detected: ") +
                  encodingResult.message;
        return false;
    }

    RuntimeConfig formatDrift = makeBaselineConfig();
    formatDrift.neutralPmremGpuFormat = NeutralPmremGpuFormat::Rgba8Unorm;
    const ValidationResult formatResult = validate(formatDrift);
    if (formatResult.ok ||
        formatResult.message.find("neutralPmremGpuFormat mismatch") ==
            std::string::npos) {
        outFail = std::string("environment GPU format drift was not detected: ") +
                  formatResult.message;
        return false;
    }
    return true;
}
