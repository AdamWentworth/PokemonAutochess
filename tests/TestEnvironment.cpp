#include "engine/core/Environment.h"
#include "TestEnvVarUtils.h"

#include <string>

namespace {
using test::env_utils::ScopedEnvVar;
using test::env_utils::setEnvVar;

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
