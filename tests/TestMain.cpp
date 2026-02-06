// tests/TestMain.cpp
#include <iostream>
#include <string>

bool test_lua_bindings_smoke(std::string& outFail);
bool test_eventbus_basic(std::string& outFail);
bool test_gameconfig_diagnostics(std::string& outFail);

static int run(const char* name, bool (*fn)(std::string&), int& failCount) {
    std::string fail;
    const bool ok = fn(fail);
    if (!ok) {
        ++failCount;
        std::cerr << "[PAC_Tests] FAIL: " << name << " :: " << fail << "\n";
        return 1;
    }
    std::cout << "[PAC_Tests] PASS: " << name << "\n";
    return 0;
}

int main() {
    int fails = 0;

    run("lua_bindings_smoke", &test_lua_bindings_smoke, fails);
    run("eventbus_basic", &test_eventbus_basic, fails);
    run("gameconfig_diagnostics", &test_gameconfig_diagnostics, fails);

    if (fails == 0) {
        std::cout << "[PAC_Tests] All tests passed.\n";
        return 0;
    }
    std::cerr << "[PAC_Tests] " << fails << " test(s) failed.\n";
    return 1;
}
