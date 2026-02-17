#include "engine/core/Environment.h"

#include <cstdlib>
#include <memory>
#include <optional>
#include <string>

namespace {

std::optional<std::string> readRawEnv(const char* name) {
    if (name == nullptr || *name == '\0') return std::nullopt;

#if defined(_MSC_VER)
    char* raw = nullptr;
    std::size_t len = 0;
    if (_dupenv_s(&raw, &len, name) != 0 || raw == nullptr) return std::nullopt;
    std::unique_ptr<char, decltype(&std::free)> holder(raw, &std::free);
    return std::string(holder.get());
#else
    const char* raw = std::getenv(name);
    if (raw == nullptr) return std::nullopt;
    return std::string(raw);
#endif
}

bool setEnvVar(const char* name, const char* value) {
    if (name == nullptr || *name == '\0') return false;
#if defined(_MSC_VER)
    return _putenv_s(name, value == nullptr ? "" : value) == 0;
#else
    if (value == nullptr) return unsetenv(name) == 0;
    return setenv(name, value, 1) == 0;
#endif
}

struct ScopedEnvVar {
    explicit ScopedEnvVar(std::string key)
        : name(std::move(key))
        , previous(readRawEnv(name.c_str())) {}

    ~ScopedEnvVar() {
        if (previous.has_value()) {
            setEnvVar(name.c_str(), previous->c_str());
        } else {
            setEnvVar(name.c_str(), nullptr);
        }
    }

    std::string name;
    std::optional<std::string> previous;
};

} // namespace

bool test_environment_helpers_contract(std::string& outFail) {
    ScopedEnvVar guard("PAC_TEST_ENV_HELPERS");

    if (!setEnvVar(guard.name.c_str(), nullptr)) {
        outFail = "Failed to clear test env variable";
        return false;
    }

    if (engine::env::get(guard.name.c_str()).has_value()) {
        outFail = "Missing var should return no value";
        return false;
    }
    if (engine::env::truthyNonZero(guard.name.c_str())) {
        outFail = "Missing var should not be truthy";
        return false;
    }
    if (engine::env::flagEnabled(guard.name.c_str())) {
        outFail = "Missing var should not be enabled";
        return false;
    }

    if (!setEnvVar(guard.name.c_str(), "")) {
        outFail = "Failed to set empty env value";
        return false;
    }
    if (engine::env::get(guard.name.c_str()).has_value()) {
        outFail = "Empty var should return no value";
        return false;
    }

    if (!setEnvVar(guard.name.c_str(), "abc")) {
        outFail = "Failed to set literal env value";
        return false;
    }
    if (!engine::env::equals(guard.name.c_str(), "abc")) {
        outFail = "equals() should match exact literal value";
        return false;
    }
    if (!engine::env::truthyNonZero(guard.name.c_str())) {
        outFail = "Non-empty literal should be truthy";
        return false;
    }
    if (engine::env::flagEnabled(guard.name.c_str())) {
        outFail = "Non-flag literal should not be enabled";
        return false;
    }

    if (!setEnvVar(guard.name.c_str(), "0")) {
        outFail = "Failed to set zero env value";
        return false;
    }
    if (engine::env::truthyNonZero(guard.name.c_str())) {
        outFail = "'0' should be false for truthyNonZero()";
        return false;
    }

    if (!setEnvVar(guard.name.c_str(), "YES")) {
        outFail = "Failed to set YES env value";
        return false;
    }
    if (!engine::env::flagEnabled(guard.name.c_str())) {
        outFail = "YES should be true for flagEnabled()";
        return false;
    }

    if (!setEnvVar(guard.name.c_str(), "off")) {
        outFail = "Failed to set off env value";
        return false;
    }
    if (engine::env::flagEnabled(guard.name.c_str())) {
        outFail = "off should be false for flagEnabled()";
        return false;
    }

    if (!setEnvVar(guard.name.c_str(), "1")) {
        outFail = "Failed to set one env value";
        return false;
    }
    if (!engine::env::flagEnabled(guard.name.c_str())) {
        outFail = "'1' should be true for flagEnabled()";
        return false;
    }
    if (!engine::env::equals(guard.name.c_str(), "1")) {
        outFail = "equals() should match '1'";
        return false;
    }
    if (engine::env::equals(guard.name.c_str(), "true")) {
        outFail = "equals() should remain exact and case-sensitive";
        return false;
    }

    return true;
}
