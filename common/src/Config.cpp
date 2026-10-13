#include "hwa/Config.hpp"

#include <toml++/toml.hpp>

#include <algorithm>
#include <cctype>
#include <charconv>
#include <format>
#include <fstream>
#include <random>
#include <sstream>

namespace hwa {
    namespace {
        std::string where(const toml::node& n) {
            const auto& src = n.source();
            return std::format("{}:{}", src.begin.line, src.begin.column);
        }

        std::expected<ParamValue, std::string> parseParam(const toml::node& n, std::string_view key) {
            if (const auto* b = n.as_boolean())
                return b->get();
            if (const auto* i = n.as_integer())
                return i->get();
            if (const auto* f = n.as_floating_point())
                return f->get();
            if (const auto* s = n.as_string()) {
                auto col = parseColor(s->get());
                if (!col)
                    return std::unexpected(std::format("{}: param '{}': {}", where(n), key, col.error()));
                return *col;
            }
            if (const auto* arr = n.as_array()) {
                std::vector<double> out;
                for (const auto& el : *arr) {
                    if (const auto* i = el.as_integer())
                        out.push_back(static_cast<double>(i->get()));
                    else if (const auto* f = el.as_floating_point())
                        out.push_back(f->get());
                    else
                        return std::unexpected(std::format("{}: param '{}': arrays may only contain numbers", where(n), key));
                }
                if (out.size() < 2 || out.size() > 4)
                    return std::unexpected(std::format("{}: param '{}': arrays must have 2..4 numbers", where(n), key));
                return out;
            }
            return std::unexpected(std::format("{}: param '{}': unsupported type", where(n), key));
        }

        template <typename T>
        std::expected<std::optional<T>, std::string> getOpt(const toml::table& t, std::string_view key) {
            const auto* n = t.get(key);
            if (!n)
                return std::optional<T>{};
            if constexpr (std::is_same_v<T, int>) {
                if (const auto* i = n->as_integer())
                    return std::optional<T>{static_cast<int>(i->get())};
                return std::unexpected(std::format("{}: '{}' must be an integer", where(*n), key));
            } else if constexpr (std::is_same_v<T, double>) {
                if (const auto* i = n->as_integer())
                    return std::optional<T>{static_cast<double>(i->get())};
                if (const auto* f = n->as_floating_point())
                    return std::optional<T>{f->get()};
                return std::unexpected(std::format("{}: '{}' must be a number", where(*n), key));
            } else if constexpr (std::is_same_v<T, bool>) {
                if (const auto* b = n->as_boolean())
                    return std::optional<T>{b->get()};
                return std::unexpected(std::format("{}: '{}' must be true or false", where(*n), key));
            } else {
                if (const auto* s = n->as_string())
                    return std::optional<T>{s->get()};
                return std::unexpected(std::format("{}: '{}' must be a string", where(*n), key));
            }
        }

        template <typename T>
        bool read(const toml::table& t, std::string_view key, T& out, std::string& err) {
            auto v = getOpt<T>(t, key);
            if (!v) {
                err = v.error();
                return false;
            }
            if (*v)
                out = **v;
            return true;
        }

        template <typename T>
        bool read(const toml::table& t, std::string_view key, std::optional<T>& out, std::string& err) {
            auto v = getOpt<T>(t, key);
            if (!v) {
                err = v.error();
                return false;
            }
            out = *v;
            return true;
        }

        // A name or a list of names
        bool readPool(const toml::table& t, std::string_view key, std::optional<EffectPool>& out, std::string& err) {
            const auto* n = t.get(key);
            if (!n)
                return true;

            EffectPool pool;
            if (const auto* s = n->as_string())
                pool.push_back(s->get());
            else if (const auto* arr = n->as_array()) {
                for (const auto& el : *arr) {
                    const auto* s = el.as_string();
                    if (!s) {
                        err = std::format("{}: '{}' must list effect names", where(el), key);
                        return false;
                    }
                    pool.push_back(s->get());
                }
            } else {
                err = std::format("{}: '{}' must be an effect name or a list of them", where(*n), key);
                return false;
            }

            if (pool.empty()) {
                err = std::format("{}: '{}' must not be empty", where(*n), key);
                return false;
            }
            for (const auto& name : pool) {
                if (!isValidEffectName(name)) {
                    err = std::format("{}: invalid effect name '{}' in '{}'", where(*n), name, key);
                    return false;
                }
            }
            out = std::move(pool);
            return true;
        }

        bool readPool(const toml::table& t, std::string_view key, EffectPool& out, std::string& err) {
            std::optional<EffectPool> pool;
            if (!readPool(t, key, pool, err))
                return false;
            if (pool)
                out = std::move(*pool);
            return true;
        }

        void warnUnknown(const toml::table& t, std::initializer_list<std::string_view> known, std::string_view section, std::vector<std::string>& warnings) {
            for (const auto& [k, v] : t) {
                if (std::ranges::find(known, k.str()) == known.end())
                    warnings.push_back(std::format("{}: unknown key '{}' in {}", where(v), k.str(), section));
            }
        }

        std::expected<std::regex, std::string> compileRegex(const std::string& pattern, const toml::node& n) {
            try {
                return std::regex{pattern, std::regex::ECMAScript};
            } catch (const std::regex_error& e) {
                return std::unexpected(std::format("{}: invalid regex '{}': {}", where(n), pattern, e.what()));
            }
        }

        std::expected<SEffectSettings, std::string> parseEffectTable(const toml::table& t, std::string_view name, std::vector<std::string>& warnings) {
            SEffectSettings s;
            std::string     err;
            if (!read(t, "duration_ms", s.durationMs, err) || !read(t, "padding", s.padding, err))
                return std::unexpected(err);

            if (const auto* p = t.get("params")) {
                const auto* pt = p->as_table();
                if (!pt)
                    return std::unexpected(std::format("{}: effects.{}.params must be a table", where(*p), name));
                for (const auto& [k, v] : *pt) {
                    auto val = parseParam(v, k.str());
                    if (!val)
                        return std::unexpected(val.error());
                    s.params[uniformNameFor(k.str())] = *val;
                }
            }

            warnUnknown(t, {"duration_ms", "padding", "params"}, std::format("effects.{}", name), warnings);
            return s;
        }

        std::expected<int, std::string> parseNumber(std::string_view s) {
            int  v       = 0;
            auto trimmed = s;
            while (!trimmed.empty() && std::isspace(static_cast<unsigned char>(trimmed.front())))
                trimmed.remove_prefix(1);
            while (!trimmed.empty() && std::isspace(static_cast<unsigned char>(trimmed.back())))
                trimmed.remove_suffix(1);
            auto [ptr, ec] = std::from_chars(trimmed.data(), trimmed.data() + trimmed.size(), v);
            if (ec != std::errc{} || ptr != trimmed.data() + trimmed.size())
                return std::unexpected(std::format("'{}' is not an integer", trimmed));
            return v;
        }
    }

    std::expected<std::vector<double>, std::string> parseColor(std::string_view str) {
        auto hexByte = [](std::string_view h) -> std::optional<double> {
            unsigned v     = 0;
            auto [ptr, ec] = std::from_chars(h.data(), h.data() + h.size(), v, 16);
            if (ec != std::errc{} || ptr != h.data() + h.size())
                return std::nullopt;
            return v / 255.0;
        };

        if (str.starts_with('#') || str.starts_with("0x")) {
            const bool hash = str.starts_with('#');
            auto       hex  = str.substr(hash ? 1 : 2);
            if (hex.size() != 6 && hex.size() != 8)
                return std::unexpected(std::format("bad hex color '{}'", str));

            std::vector<double> c;
            for (size_t i = 0; i < hex.size(); i += 2) {
                auto b = hexByte(hex.substr(i, 2));
                if (!b)
                    return std::unexpected(std::format("bad hex color '{}'", str));
                c.push_back(*b);
            }
            if (c.size() == 3)
                c.push_back(1.0);
            else if (!hash) // 0xAARRGGBB, Hyprland style
                std::rotate(c.begin(), c.begin() + 1, c.end());
            return c;
        }

        const bool rgba = str.starts_with("rgba(");
        if ((rgba || str.starts_with("rgb(")) && str.ends_with(')')) {
            auto                     inner = str.substr(rgba ? 5 : 4);
            std::vector<std::string> parts;
            std::stringstream        ss{std::string{inner.substr(0, inner.size() - 1)}};
            for (std::string part; std::getline(ss, part, ',');)
                parts.push_back(part);

            if (parts.size() != (rgba ? 4U : 3U))
                return std::unexpected(std::format("bad color '{}'", str));

            std::vector<double> c;
            for (size_t i = 0; i < 3; ++i) {
                auto v = parseNumber(parts[i]);
                if (!v || *v < 0 || *v > 255)
                    return std::unexpected(std::format("bad color component in '{}'", str));
                c.push_back(*v / 255.0);
            }

            double a = 1.0;
            if (rgba) {
                try {
                    a = std::stod(parts[3]);
                } catch (...) { return std::unexpected(std::format("bad alpha in '{}'", str)); }
                if (a > 1.0)
                    a /= 255.0;
            }
            c.push_back(std::clamp(a, 0.0, 1.0));
            return c;
        }

        return std::unexpected(std::format("unknown color format '{}' (use rgb(), rgba(), #rrggbb or 0xAARRGGBB)", str));
    }

    std::string uniformNameFor(std::string_view key) {
        if (key.size() >= 2 && key[0] == 'u' && std::isupper(static_cast<unsigned char>(key[1])))
            return std::string{key};

        std::string out = "u";
        bool        up  = true;
        for (char c : key) {
            if (c == '_' || c == '-') {
                up = true;
                continue;
            }
            out += up ? static_cast<char>(std::toupper(static_cast<unsigned char>(c))) : c;
            up = false;
        }
        return out;
    }

    bool isValidEffectName(std::string_view name) {
        if (name.empty() || name.size() > 64 || name.front() == '.')
            return false;
        return std::ranges::all_of(name, [](char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' || c == '.'; });
    }

    std::string describe(const ParamValue& v) {
        return std::visit(
            [](const auto& x) -> std::string {
                using T = std::decay_t<decltype(x)>;
                if constexpr (std::is_same_v<T, bool>)
                    return x ? "true" : "false";
                else if constexpr (std::is_same_v<T, std::vector<double>>) {
                    std::string s = "[";
                    for (size_t i = 0; i < x.size(); ++i)
                        s += std::format("{}{}", i ? ", " : "", x[i]);
                    return s + "]";
                } else
                    return std::format("{}", x);
            },
            v);
    }

    std::expected<SConfig, std::string> parseConfig(std::string_view text, std::string_view source) {
        toml::table root;
        try {
            root = toml::parse(text, source);
        } catch (const toml::parse_error& e) {
            const auto& b = e.source().begin;
            return std::unexpected(std::format("{}:{}:{}: {}", source, b.line, b.column, e.description()));
        }

        SConfig     cfg;
        std::string err;

        if (const auto* g = root.get("general")) {
            const auto* gt = g->as_table();
            if (!gt)
                return std::unexpected(std::format("{}: [general] must be a table", where(*g)));

            auto& G = cfg.general;
            if (!read(*gt, "enabled", G.enabled, err) || !readPool(*gt, "default_effect", G.defaultEffect, err) || !read(*gt, "hold_layout", G.holdLayout, err) ||
                !read(*gt, "app_close_effects", G.appCloseEffects, err) || !read(*gt, "close_timeout_ms", G.closeTimeoutMs, err) ||
                !read(*gt, "max_concurrent", G.maxConcurrent, err) || !read(*gt, "open_effects", G.openEffects, err) || !readPool(*gt, "open_effect", G.openEffect, err))
                return std::unexpected(err);

            G.closeTimeoutMs = std::clamp(G.closeTimeoutMs, 100, 30000);
            G.maxConcurrent  = std::clamp(G.maxConcurrent, 1, 32);

            warnUnknown(*gt, {"enabled", "default_effect", "hold_layout", "app_close_effects", "close_timeout_ms", "max_concurrent", "open_effects", "open_effect"}, "[general]",
                        cfg.warnings);
        }

        if (const auto* e = root.get("effects")) {
            const auto* et = e->as_table();
            if (!et)
                return std::unexpected(std::format("{}: [effects] must be a table", where(*e)));
            for (const auto& [name, node] : *et) {
                const auto* t = node.as_table();
                if (!t)
                    return std::unexpected(std::format("{}: effects.{} must be a table", where(node), name.str()));
                auto s = parseEffectTable(*t, name.str(), cfg.warnings);
                if (!s)
                    return std::unexpected(s.error());
                cfg.effects[std::string{name.str()}] = std::move(*s);
            }
        }

        if (const auto* r = root.get("rules")) {
            const auto* arr = r->as_array();
            if (!arr || !arr->is_array_of_tables())
                return std::unexpected(std::format("{}: rules must be an array of tables ([[rules]])", where(*r)));

            for (const auto& node : *arr) {
                const auto& t = *node.as_table();
                SRule       rule;
                if (!read(t, "class", rule.classPattern, err) || !read(t, "title", rule.titlePattern, err) || !read(t, "floating", rule.floating, err) ||
                    !read(t, "xwayland", rule.xwayland, err) || !readPool(t, "effect", rule.effect, err) || !read(t, "duration_ms", rule.durationMs, err) ||
                    !read(t, "hold", rule.hold, err) || !readPool(t, "open_effect", rule.openEffect, err) || !read(t, "open_duration_ms", rule.openDurationMs, err))
                    return std::unexpected(err);

                if (!rule.classPattern.empty()) {
                    auto re = compileRegex(rule.classPattern, *t.get("class"));
                    if (!re)
                        return std::unexpected(re.error());
                    rule.classRe = std::move(*re);
                }
                if (!rule.titlePattern.empty()) {
                    auto re = compileRegex(rule.titlePattern, *t.get("title"));
                    if (!re)
                        return std::unexpected(re.error());
                    rule.titleRe = std::move(*re);
                }
                warnUnknown(t, {"class", "title", "floating", "xwayland", "effect", "duration_ms", "hold", "open_effect", "open_duration_ms"}, "[[rules]]", cfg.warnings);
                cfg.rules.push_back(std::move(rule));
            }
        }

        warnUnknown(root, {"general", "effects", "rules"}, "the top level", cfg.warnings);
        return cfg;
    }

    std::expected<SConfig, std::string> loadConfigFile(const std::filesystem::path& path) {
        std::error_code ec;
        if (!std::filesystem::exists(path, ec))
            return SConfig{};

        std::ifstream in{path};
        if (!in)
            return std::unexpected(std::format("cannot read {}", path.string()));

        std::stringstream ss;
        ss << in.rdbuf();
        return parseConfig(ss.str(), path.string());
    }

    std::string pickEffect(const EffectPool& pool, eAnim anim) {
        if (pool.empty())
            return "none";
        if (pool.size() == 1)
            return pool.front();

        static std::mt19937                       rng{std::random_device{}()};
        static std::map<std::pair<eAnim, EffectPool>, std::string> last; // opens and closes are separate streams

        auto&                                     prev = last[{anim, pool}];
        std::vector<const std::string*>           candidates;
        for (const auto& name : pool) {
            if (name != prev)
                candidates.push_back(&name);
        }
        if (candidates.empty()) // every entry is the same name
            return pool.front();

        prev = *candidates[std::uniform_int_distribution<size_t>{0, candidates.size() - 1}(rng)];
        return prev;
    }

    std::set<std::string> configuredEffects(const SConfig& cfg) {
        std::set<std::string> names{cfg.general.defaultEffect.begin(), cfg.general.defaultEffect.end()};
        if (cfg.general.openEffects)
            names.insert(cfg.general.openEffect.begin(), cfg.general.openEffect.end());
        for (const auto& r : cfg.rules) {
            for (const auto& pool : {r.effect, r.openEffect}) {
                if (pool)
                    names.insert(pool->begin(), pool->end());
            }
        }
        names.erase("none");
        return names;
    }

    std::string describePool(const EffectPool& pool) {
        if (pool.size() == 1)
            return pool.front();

        std::string out = "random of ";
        for (size_t i = 0; i < pool.size(); ++i)
            out += (i ? ", " : "") + pool[i];
        return out;
    }

    bool mayAnimate(const EffectPool& pool) {
        return std::ranges::any_of(pool, [](const auto& n) { return n != "none"; });
    }

    SDecision decide(const SConfig& cfg, const SWindowFacts& facts, eAnim anim) {
        const bool open = anim == eAnim::OPEN;
        SDecision  d{.effects = {"none"}, .durationMs = std::nullopt, .hold = cfg.general.holdLayout};
        if (!cfg.general.enabled || (open && !cfg.general.openEffects))
            return d;

        d.effects = open ? cfg.general.openEffect : cfg.general.defaultEffect;
        for (const auto& r : cfg.rules) {
            if (r.classRe && !std::regex_search(facts.cls, *r.classRe))
                continue;
            if (r.titleRe && !std::regex_search(facts.title, *r.titleRe))
                continue;
            if (r.floating && *r.floating != facts.floating)
                continue;
            if (r.xwayland && *r.xwayland != facts.xwayland)
                continue;

            if (const auto& pool = open ? r.openEffect : r.effect)
                d.effects = *pool;
            d.durationMs = open ? r.openDurationMs : r.durationMs;
            if (r.hold)
                d.hold = *r.hold && cfg.general.holdLayout;
            break;
        }
        return d;
    }
}
