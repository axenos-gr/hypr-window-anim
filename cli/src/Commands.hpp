#pragma once

#include <string>
#include <vector>

namespace hwa::cli {
    struct SOptions {
        std::string              instance;
        std::string              pluginPath;
        bool                     force   = false;
        bool                     offline = false;
        bool                     all     = false;
        std::vector<std::string> args;
    };

    int cmdInit(const SOptions& o);
    int cmdLoad(const SOptions& o);
    int cmdUnload(const SOptions& o);
    int cmdRestart(const SOptions& o);
    int cmdStatus(const SOptions& o);
    int cmdPassthrough(const SOptions& o, const std::string& sub);
    int cmdCheck(const SOptions& o);
    int cmdDoctor(const SOptions& o);
    int cmdInstances(const SOptions& o);
}
