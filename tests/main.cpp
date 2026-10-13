#include "hwa/Assets.hpp"
#include "hwa/Config.hpp"
#include "hwa/Effect.hpp"
#include "hwa/ShaderAssembly.hpp"

#include <cmath>
#include <cstdlib>
#include <map>
#include <set>
#include <print>
#include <source_location>

static int g_failures = 0;

static void expect(bool cond, std::string_view what, std::source_location loc = std::source_location::current()) {
    if (cond)
        return;
    ++g_failures;
    std::println(stderr, "FAIL {}:{}: {}", loc.file_name(), loc.line(), what);
}

static bool near(double a, double b) {
    return std::abs(a - b) < 1e-3;
}

static void testColors() {
    auto c = hwa::parseColor("rgb(100, 160, 255)");
    expect(c && c->size() == 4 && near((*c)[0], 100 / 255.0) && near((*c)[3], 1.0), "rgb()");

    c = hwa::parseColor("rgba(255,0,0,0.5)");
    expect(c && near((*c)[0], 1.0) && near((*c)[3], 0.5), "rgba()");

    c = hwa::parseColor("#336699");
    expect(c && near((*c)[1], 0x66 / 255.0) && near((*c)[3], 1.0), "#rrggbb");

    c = hwa::parseColor("0x80ff0000");
    expect(c && near((*c)[0], 1.0) && near((*c)[3], 0x80 / 255.0), "0xAARRGGBB");

    expect(!hwa::parseColor("rgb(1,2)"), "rgb with 2 parts rejected");
    expect(!hwa::parseColor("blue"), "named colors rejected");
}

static void testUniformNames() {
    expect(hwa::uniformNameFor("strength") == "uStrength", "strength");
    expect(hwa::uniformNameFor("uColor") == "uColor", "uColor passthrough");
    expect(hwa::uniformNameFor("my_param") == "uMyParam", "snake case");
    expect(hwa::isValidEffectName("tv-glitch"), "valid name");
    expect(!hwa::isValidEffectName("../etc/passwd"), "path traversal rejected");
    expect(!hwa::isValidEffectName(""), "empty rejected");
}

static void testConfig() {
    auto def = hwa::parseConfig(hwa::assets::find("config/default.toml").value_or(""));
    expect(def.has_value(), def ? "default config" : def.error());
    if (def) {
        expect(def->general.defaultEffect == hwa::EffectPool{"tv-glitch"}, "default effect");
        expect(def->warnings.empty(), "default config has no warnings");
        expect(def->effects.contains("tv-glitch") && def->effects["tv-glitch"].params.contains("uColor"), "params mapped to uniforms");
    }

    auto cfg = hwa::parseConfig(R"(
[general]
default_effect = "fade"
close_timeout_ms = 10

[[rules]]
class = "^(kitty)$"
duration_ms = 500

[[rules]]
class = "rofi"
effect = "none"

[[rules]]
floating = true
hold = false

[[rules]]
class = "^(firefox)$"
open_effect = "none"
open_duration_ms = 300
)");
    expect(cfg.has_value(), cfg ? "rules config" : cfg.error());
    if (cfg) {
        expect(cfg->general.closeTimeoutMs == 100, "timeout clamped");

        using hwa::eAnim;
        using Pool = hwa::EffectPool;

        auto d = hwa::decide(*cfg, {.cls = "kitty"}, eAnim::CLOSE);
        expect(d.effects == Pool{"fade"} && d.durationMs == 500 && d.hold, "kitty rule");

        d = hwa::decide(*cfg, {.cls = "rofi", .floating = true}, eAnim::CLOSE);
        expect(d.effects == Pool{"none"} && !hwa::mayAnimate(d.effects), "rofi disabled, first match wins");

        d = hwa::decide(*cfg, {.cls = "foot", .floating = true}, eAnim::CLOSE);
        expect(d.effects == Pool{"fade"} && !d.hold, "floating rule disables hold");

        d = hwa::decide(*cfg, {.cls = "foot"}, eAnim::CLOSE);
        expect(!d.durationMs && d.hold, "no rule matched");
        d = hwa::decide(*cfg, {.cls = "foot"}, eAnim::OPEN);
        expect(d.effects == Pool{"tv-glitch"} && !d.durationMs, "open effect defaults");

        d = hwa::decide(*cfg, {.cls = "firefox"}, eAnim::OPEN);
        expect(d.effects == Pool{"none"} && d.durationMs == 300, "open rule");
        d = hwa::decide(*cfg, {.cls = "firefox"}, eAnim::CLOSE);
        expect(d.effects == Pool{"fade"} && !d.durationMs, "open rule only touches opening");
    }

    auto noOpen = hwa::parseConfig("[general]\nopen_effects = false\n[[rules]]\nclass = \"x\"\nopen_effect = \"fade\"\n");
    expect(noOpen && !hwa::mayAnimate(hwa::decide(*noOpen, {.cls = "x"}, hwa::eAnim::OPEN).effects), "open_effects=false wins over rules");

    expect(!hwa::parseConfig("[general]\nenabled = 3\n"), "type errors are reported");
    expect(!hwa::parseConfig("[[rules]]\nclass = \"([\"\n"), "bad regex reported");
    expect(!hwa::parseConfig("[general\n"), "syntax error reported");

    auto unknown = hwa::parseConfig("[general]\nenable = true\n");
    expect(unknown && unknown->warnings.size() == 1, "unknown keys warn");

    auto off = hwa::parseConfig("[general]\nenabled = false\n");
    expect(off && !hwa::mayAnimate(hwa::decide(*off, {}, hwa::eAnim::CLOSE).effects), "disabled globally");
}

// Picks a close (or open) effect the way the plugin does
static std::string play(const hwa::SConfig& cfg, std::string cls, hwa::eAnim anim = hwa::eAnim::CLOSE) {
    return hwa::pickEffect(hwa::decide(cfg, {.cls = std::move(cls)}, anim).effects, anim);
}

static void testPools() {
    using hwa::eAnim;

    auto cfg = hwa::parseConfig(R"(
[general]
default_effect = ["fade", "tv-glitch", "doom"]
open_effect = ["hexagon", "none"]

[[rules]]
class = "^(kitty)$"
effect = ["shatter", "slice"]
open_effect = "fade"
)");
    expect(cfg.has_value(), cfg ? "pool config" : cfg.error());
    if (!cfg)
        return;

    std::map<std::string, int> seen;
    std::string                prevClose, prevOpen;
    bool                       repeated = false;
    for (int i = 0; i < 300; ++i) {
        // opens and closes interleave like in a session
        const auto c = play(*cfg, "foot"), o = play(*cfg, "foot", eAnim::OPEN);
        repeated |= c == prevClose || o == prevOpen;
        prevClose = c;
        prevOpen  = o;
        seen[c]++;
        expect(o == "hexagon" || o == "none", "open pick from its pool");
    }
    expect(seen.size() == 3 && seen["fade"] > 50 && seen["tv-glitch"] > 50 && seen["doom"] > 50, "every pool entry gets picked");
    expect(!repeated, "no effect twice in a row, per stream");

    for (int i = 0; i < 20; ++i) {
        const auto c = play(*cfg, "kitty");
        expect((c == "shatter" || c == "slice") && play(*cfg, "kitty", eAnim::OPEN) == "fade", "rule pools");
    }

    // the same list for opening and closing: each stream still alternates
    auto shared = hwa::parseConfig("[general]\ndefault_effect = [\"fade\", \"doom\"]\nopen_effect = [\"fade\", \"doom\"]\n");
    if (shared) {
        std::string c1 = play(*shared, "x"), o1 = play(*shared, "x", eAnim::OPEN);
        std::string c2 = play(*shared, "x"), o2 = play(*shared, "x", eAnim::OPEN);
        expect(c1 != c2 && o1 != o2, "open and close streams are independent");
    }

    const auto names = hwa::configuredEffects(*cfg);
    expect(names == std::set<std::string>{"doom", "fade", "hexagon", "shatter", "slice", "tv-glitch"}, "configured effects, no 'none'");
    expect(hwa::describePool(cfg->general.defaultEffect) == "random of fade, tv-glitch, doom", "describe pool");
    expect(hwa::mayAnimate({"none", "fade"}) && !hwa::mayAnimate({"none"}), "mayAnimate");

    // duplicates are harmless: two distinct names alternate
    auto dup = hwa::parseConfig("[general]\ndefault_effect = [\"fade\", \"fade\", \"doom\"]\n");
    if (dup) {
        const auto a = play(*dup, "x"), b = play(*dup, "x");
        expect(a != b, "duplicates still alternate");
    }
    auto same = hwa::parseConfig("[general]\ndefault_effect = [\"fade\", \"fade\"]\n");
    expect(same && play(*same, "x") == "fade" && play(*same, "x") == "fade", "same name twice");

    expect(!hwa::parseConfig("[general]\ndefault_effect = []\n"), "empty pool rejected");
    expect(!hwa::parseConfig("[general]\ndefault_effect = [\"fade\", 3]\n"), "non-string entry rejected");
    expect(!hwa::parseConfig("[general]\nopen_effect = [\"../x\"]\n"), "bad name in pool rejected");
    expect(!hwa::parseConfig("[[rules]]\nclass = \"x\"\neffect = 5\n"), "bad rule effect type rejected");
}

static void testEffects() {
    setenv("HWA_CONFIG_DIR", "/nonexistent/hwa-test", 1);

    auto tv = hwa::loadEffect("tv-glitch");
    expect(tv.has_value(), tv ? "tv-glitch" : tv.error());
    if (tv) {
        expect(tv->origin == "builtin" && tv->durationMs == 750, "tv-glitch meta");
        expect(tv->params.contains("uStrength"), "tv-glitch params");

        hwa::SConfig cfg;
        cfg.effects["tv-glitch"].params["uStrength"] = 5.0;
        auto r = hwa::resolveEffect(*tv, cfg, 9000);
        expect(r.durationMs == 5000, "duration clamped");
        expect(std::get<double>(r.params["uStrength"]) == 5.0, "config overrides params");
    }

    expect(!hwa::loadEffect("does-not-exist"), "missing effect");

    const auto all = hwa::listEffects();
    expect(all.size() >= 11, "built-ins listed");
    for (const auto& e : all) {
        auto loaded = hwa::loadEffect(e.name);
        expect(loaded.has_value(), loaded ? e.name : loaded.error());
        if (loaded) {
            const auto r = hwa::resolveEffect(*loaded, hwa::SConfig{}, std::nullopt);
            expect(r.durationMs == loaded->durationMs && r.padding == loaded->padding, e.name + " defaults within limits");
        }
    }

    auto src = hwa::assembleFragment("void main() {}");
    expect(src.starts_with("#version 300 es\n"), "version first");
    expect(src.find("#line 1 1\nvoid main") != std::string::npos, "effect body is source string 1");
}

int main() {
    testColors();
    testUniformNames();
    testConfig();
    testPools();
    testEffects();

    if (g_failures)
        std::println(stderr, "{} failure(s)", g_failures);
    else
        std::println("all tests passed");
    return g_failures ? 1 : 0;
}
