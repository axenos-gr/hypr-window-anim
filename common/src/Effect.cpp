#include "hwa/Effect.hpp"

#include "hwa/Assets.hpp"
#include "hwa/Paths.hpp"

#include <toml++/toml.hpp>

#include <algorithm>
#include <format>
#include <fstream>
#include <set>
#include <sstream>

namespace hwa {
    namespace {
        std::optional<std::string> readFile(const std::filesystem::path& p) {
            std::ifstream in{p};
            if (!in)
                return std::nullopt;
            std::stringstream ss;
            ss << in.rdbuf();
            return ss.str();
        }

        std::string stripVersion(const std::string& body) {
            std::stringstream in{body};
            std::string       out, line;
            while (std::getline(in, line)) {
                // keep line numbers for #line mapping
                out += line.starts_with("#version") ? "" : line;
                out += '\n';
            }
            return out;
        }

        std::expected<void, std::string> applyMeta(SEffectSource& e, std::string_view text, std::string_view source) {
            toml::table t;
            try {
                t = toml::parse(text, source);
            } catch (const toml::parse_error& err) {
                const auto& b = err.source().begin;
                return std::unexpected(std::format("{}:{}:{}: {}", source, b.line, b.column, err.description()));
            }

            if (auto d = t["duration_ms"].value<int64_t>())
                e.durationMs = static_cast<int>(*d);
            if (auto p = t["padding"].value<double>())
                e.padding = *p;

            if (const auto* params = t.get("params"); params && params->is_table()) {
                std::stringstream ss;
                ss << "[effects.x.params]\n" << *params->as_table();
                auto cfg = parseConfig(ss.str(), source);
                if (!cfg)
                    return std::unexpected(cfg.error());
                e.params = cfg->effects["x"].params;
            }
            return {};
        }
    }

    std::expected<SEffectSource, std::string> loadEffect(std::string_view name) {
        if (!isValidEffectName(name))
            return std::unexpected(std::format("invalid effect name '{}'", name));

        SEffectSource e;
        e.name = name;

        const auto userFrag = userEffectsDir() / std::format("{}.frag", name);
        if (auto body = readFile(userFrag)) {
            e.origin = userFrag.string();
            e.body   = stripVersion(*body);

            const auto userMeta = userEffectsDir() / std::format("{}.toml", name);
            if (auto meta = readFile(userMeta)) {
                if (auto r = applyMeta(e, *meta, userMeta.string()); !r)
                    return std::unexpected(r.error());
            } else if (auto builtinMeta = assets::find(std::format("effects/{}.toml", name))) {
                // overridden built-in keeps its defaults
                if (auto r = applyMeta(e, *builtinMeta, "builtin"); !r)
                    return std::unexpected(r.error());
            }
            return e;
        }

        if (auto body = assets::find(std::format("effects/{}.frag", name))) {
            e.origin = "builtin";
            e.body   = stripVersion(std::string{*body});
            if (auto meta = assets::find(std::format("effects/{}.toml", name))) {
                if (auto r = applyMeta(e, *meta, "builtin"); !r)
                    return std::unexpected(r.error());
            }
            return e;
        }

        return std::unexpected(std::format("effect '{}' not found in {} or built-ins", name, userEffectsDir().string()));
    }

    std::vector<SEffectListing> listEffects() {
        std::vector<SEffectListing> out;
        std::set<std::string>       seen;

        std::error_code             ec;
        for (const auto& entry : std::filesystem::directory_iterator(userEffectsDir(), ec)) {
            if (entry.path().extension() != ".frag")
                continue;
            auto name = entry.path().stem().string();
            if (!isValidEffectName(name) || !seen.insert(name).second)
                continue;
            out.push_back({name, entry.path().string()});
        }

        for (const auto& a : assets::all()) {
            if (!a.path.starts_with("effects/") || !a.path.ends_with(".frag"))
                continue;
            auto name = std::string{a.path.substr(8, a.path.size() - 8 - 5)};
            if (seen.insert(name).second)
                out.push_back({name, "builtin"});
        }

        std::ranges::sort(out, {}, &SEffectListing::name);
        return out;
    }

    SResolvedEffect resolveEffect(const SEffectSource& src, const SConfig& cfg, std::optional<int> durationOverride) {
        SResolvedEffect r{.source = src, .durationMs = src.durationMs, .padding = src.padding, .params = src.params};

        if (auto it = cfg.effects.find(src.name); it != cfg.effects.end()) {
            if (it->second.durationMs)
                r.durationMs = *it->second.durationMs;
            if (it->second.padding)
                r.padding = *it->second.padding;
            for (const auto& [k, v] : it->second.params)
                r.params[k] = v;
        }

        if (durationOverride)
            r.durationMs = *durationOverride;

        r.durationMs = std::clamp(r.durationMs, 50, 5000);
        r.padding    = std::clamp(r.padding, 0.0, 500.0);
        return r;
    }
}
