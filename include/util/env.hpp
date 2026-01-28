#pragma once

#include <string>
#include <cstdlib>

inline std::string getenv_or(const char* name, const std::string& def) {
    if (const char* v = std::getenv(name)) {
        return v;
    }
    return def;
}
