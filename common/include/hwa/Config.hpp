#pragma once

#include <expected>
#include <filesystem>
#include <map>
#include <optional>
#include <regex>
#include <set>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace hwa {
    // Vectors are vec2..vec4; colors become vec4
    using ParamValue = std::variant<bool, int64_t, double, std::vector<double>>;
    using ParamMap   = std::map<std::string, ParamValue>; // keyed by uniform name
    using EffectPool = std::vector<std::string>;          // one is picked at random per window

    struct SEffectSettings {
        std::optional<int>    durationMs;
        std::optional<double> padding;
        ParamMap              params;
    };

    struct SRule {
        std::string               classPattern;
        std::string               titlePattern;
        std::optional<std::regex> classRe;
        std::optional<std::regex> titleRe;
        std::optional<bool>       floating;
        std::optional<bool>       xwayland;
        std::optional<EffectPool> effect;
        std::optional<int>        durationMs;
        std::optional<bool>       hold;
        std::optional<EffectPool> openEffect;
        std::optional<int>        openDurationMs;
    };

    struct SGeneral {
        bool        enabled         = true;
        EffectPool  defaultEffect   = {"tv-glitch"};
        bool        holdLayout      = true;
        bool        appCloseEffects = true;
        int         closeTimeoutMs  = 1500;
        int         maxConcurrent   = 6;
        bool        openEffects     = true;
        EffectPool  openEffect      = {"tv-glitch"};
    };

    struct SConfig {
        SGeneral                               general;
        std::map<std::string, SEffectSettings> effects;
        std::vector<SRule>                     rules;
        std::vector<std::string>               warnings;
    };

    struct SWindowFacts {
        std::string cls;
        std::string title;
        bool        floating = false;
        bool        xwayland = false;
    };

    enum class eAnim {
        CLOSE,
        OPEN,
    };

    struct SDecision {
        EffectPool         effects{"none"}; // pick right before playing, once
        std::optional<int> durationMs;
        bool               hold = true; // closing only
    };

    std::expected<SConfig, std::string>             parseConfig(std::string_view text, std::string_view source = "config.toml");
    std::expected<SConfig, std::string>             loadConfigFile(const std::filesystem::path& path); // missing file = defaults
    SDecision                                       decide(const SConfig& cfg, const SWindowFacts& facts, eAnim anim);
    std::string                                     pickEffect(const EffectPool& pool, eAnim anim); // never the same twice in a row
    bool                                            mayAnimate(const EffectPool& pool); // anything but "none"
    std::set<std::string>                           configuredEffects(const SConfig& cfg);              // without "none"
    std::string                                     describePool(const EffectPool& pool);

    std::expected<std::vector<double>, std::string> parseColor(std::string_view str);
    std::string                                     uniformNameFor(std::string_view key);
    bool                                            isValidEffectName(std::string_view name);
    std::string                                     describe(const ParamValue& v);
}
