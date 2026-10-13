#pragma once

#include "Config.hpp"

#include <expected>
#include <string>
#include <vector>

namespace hwa {
    struct SEffectSource {
        std::string name;
        std::string origin; // file path or "builtin"
        std::string body;   // fragment body without #version
        int         durationMs = 600;
        double      padding    = 0.0;
        ParamMap    params;
    };

    struct SResolvedEffect {
        SEffectSource source;
        int           durationMs = 600;
        double        padding    = 0.0;
        ParamMap      params;
    };

    struct SEffectListing {
        std::string name;
        std::string origin;
    };

    // User effects override built-ins
    std::expected<SEffectSource, std::string> loadEffect(std::string_view name);
    std::vector<SEffectListing>               listEffects();
    SResolvedEffect                           resolveEffect(const SEffectSource& src, const SConfig& cfg, std::optional<int> durationOverride);
}
