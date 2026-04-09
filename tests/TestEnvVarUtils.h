#pragma once

#include <cstdlib>
#include <functional>
#include <memory>
#include <optional>
#include <string>

namespace test::env_utils {

inline std::optional<std::string> readRawEnv(const char* name) {
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

inline bool setEnvVar(const char* name, const char* value) {
    if (name == nullptr || *name == '\0') return false;
#if defined(_MSC_VER)
    return _putenv_s(name, value == nullptr ? "" : value) == 0;
#else
    if (value == nullptr) return unsetenv(name) == 0;
    return setenv(name, value, 1) == 0;
#endif
}

struct ScopedEnvVar {
    explicit ScopedEnvVar(std::string key, std::function<void()> onRestore = {})
        : name(std::move(key))
        , previous(readRawEnv(name.c_str()))
        , onRestore(std::move(onRestore)) {}

    ScopedEnvVar(const ScopedEnvVar&) = delete;
    ScopedEnvVar& operator=(const ScopedEnvVar&) = delete;

    ~ScopedEnvVar() {
        if (previous.has_value()) {
            setEnvVar(name.c_str(), previous->c_str());
        } else {
            setEnvVar(name.c_str(), nullptr);
        }
        if (onRestore) onRestore();
    }

    std::string name;
    std::optional<std::string> previous;
    std::function<void()> onRestore;
};

} // namespace test::env_utils
