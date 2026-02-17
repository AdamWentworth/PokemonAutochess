#include "engine/render/DxgiAdapterSelection.h"

#include <string>
#include <vector>

bool test_dxgi_adapter_selection_policy(std::string& outFail) {
    using engine::render::dxgi::AdapterPreferenceCandidate;
    using engine::render::dxgi::selectPreferredAdapterCandidate;

    const std::vector<AdapterPreferenceCandidate> mixed = {
        {"Intel(R) HD Graphics 630", false, false},
        {"NVIDIA GeForce GTX 1050", true, false},
        {"Microsoft Basic Render Driver", false, true},
    };

    if (selectPreferredAdapterCandidate(mixed, "nvidia") != 1) {
        outFail = "preferred-name selection should choose matching adapter";
        return false;
    }

    if (selectPreferredAdapterCandidate(mixed, "") != 1) {
        outFail = "default selection should prefer discrete non-software adapter";
        return false;
    }

    const std::vector<AdapterPreferenceCandidate> noDiscrete = {
        {"Intel(R) UHD Graphics", false, false},
        {"Microsoft Basic Render Driver", false, true},
    };

    if (selectPreferredAdapterCandidate(noDiscrete, "") != 0) {
        outFail = "selection should choose non-software adapter when no discrete adapter exists";
        return false;
    }

    const std::vector<AdapterPreferenceCandidate> onlySoftware = {
        {"Microsoft Basic Render Driver", false, true},
    };

    if (selectPreferredAdapterCandidate(onlySoftware, "") != 0) {
        outFail = "selection should fall back to first candidate when only software adapters exist";
        return false;
    }

    const std::vector<AdapterPreferenceCandidate> empty = {};
    if (selectPreferredAdapterCandidate(empty, "") != -1) {
        outFail = "selection should return -1 when no candidates are available";
        return false;
    }

    return true;
}
