#include "CloseController.hpp"
#include "ConfigWatcher.hpp"
#include "EffectManager.hpp"
#include "EffectPassElement.hpp"
#include "Hooks.hpp"
#include "OpenTransformer.hpp"
#include "Plugin.hpp"
#include "ShaderCache.hpp"

#include "hwa/Effect.hpp"
#include "hwa/Paths.hpp"

#include <hyprland/src/desktop/state/FocusState.hpp>
#include <hyprland/src/desktop/view/Window.hpp>
#include <hyprland/src/event/EventBus.hpp>
#include <hyprland/src/managers/eventLoop/EventLoopManager.hpp>
#include <hyprland/src/output/Monitor.hpp>
#include <hyprland/src/render/OpenGL.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/state/MonitorState.hpp>

#include <dlfcn.h>
#include <sys/stat.h>

#include <format>
#include <set>
#include <sstream>

using namespace hwa;
using namespace hwa::plugin;

namespace hwa::plugin {
    namespace {
        std::vector<SP<CEventLoopTimer>>  g_timers;
        std::vector<CHyprSignalListener>  g_listeners;
        SP<SHyprCtlCommand>               g_command;

        std::string jsonEscape(std::string_view s) {
            std::string out;
            for (char c : s) {
                switch (c) {
                    case '"': out += "\\\""; break;
                    case '\\': out += "\\\\"; break;
                    case '\n': out += "\\n"; break;
                    default: out += c;
                }
            }
            return out;
        }

        std::string pluginPath() {
            Dl_info info{};
            if (dladdr(reinterpret_cast<void*>(&jsonEscape), &info) && info.dli_fname)
                return info.dli_fname;
            return "";
        }

        // mtime of the .so we were loaded from; lets the CLI spot a newer install
        int64_t g_loadedMtime = 0;

        int64_t fileMtime(const std::string& path) {
            struct stat st{};
            return stat(path.c_str(), &st) == 0 ? static_cast<int64_t>(st.st_mtime) : 0;
        }
    }

    SState& state() {
        static SState s;
        return s;
    }

    void log(const std::string& msg) {
        Log::logger->log(Log::DEBUG, "[hwanim] {}", msg);
    }

    void logError(const std::string& msg) {
        Log::logger->log(Log::ERR, "[hwanim] {}", msg);
    }

    void notify(const std::string& msg, bool error) {
        HyprlandAPI::addNotification(PHANDLE, "[hypr-window-anim] " + msg, error ? CHyprColor{1.0, 0.35, 0.35, 1.0} : CHyprColor{0.4, 0.7, 1.0, 1.0}, error ? 8000 : 4000);
    }

    SP<CEventLoopTimer> addTimer(std::chrono::milliseconds delay, std::function<void()> fn) {
        auto timer = makeShared<CEventLoopTimer>(
            delay,
            [fn = std::move(fn)](SP<CEventLoopTimer> self, void*) {
                std::erase(g_timers, self);
                g_pEventLoopManager->removeTimer(self);
                if (!g_unloading)
                    guarded("timer", fn);
            },
            nullptr);
        g_timers.push_back(timer);
        g_pEventLoopManager->addTimer(timer);
        return timer;
    }

    void cancelTimer(SP<CEventLoopTimer>& timer) {
        if (!timer)
            return;
        timer->cancel();
        std::erase(g_timers, timer);
        g_pEventLoopManager->removeTimer(timer);
        timer.reset();
    }

    void cancelAllTimers() {
        for (auto& t : g_timers) {
            t->cancel();
            g_pEventLoopManager->removeTimer(t);
        }
        g_timers.clear();
    }

    void makeGLCurrent() {
        if (Render::GL::g_pHyprOpenGL)
            Render::GL::g_pHyprOpenGL->makeEGLCurrent();
    }

    bool reloadConfig(bool notifyOnError) {
        auto cfg = loadConfigFile(configFile());
        if (!cfg) {
            state().configError = cfg.error();
            logError(cfg.error());
            if (notifyOnError)
                notify("config error, keeping the previous one: " + cfg.error(), true);
            return false;
        }

        for (const auto& w : cfg->warnings)
            log(w);

        state().config = std::move(*cfg);
        state().configError.clear();
        state().needCompile = true; // compile on next frame
        shaderCache().clear();
        watcher::start();           // pick up new dirs
        log(std::format("config loaded from {}", configFile().string()));
        return true;
    }
}

namespace {
    std::string statusText() {
        const auto& s = state();
        const auto& g = s.config.general;
        return std::format("hypr-window-anim {}\n"
                           "enabled: {}\n"
                           "hooks: {}, render hook: {}\n"
                           "config: {}{}\n"
                           "default effect: {}, hold layout: {}, app closes: {}, close timeout: {} ms\n"
                           "open effect: {}\n"
                           "running effects: {}\n{}"
                           "held windows: {}\n{}"
                           "recent effects:\n{}",
                           HWA_VERSION, g.enabled ? "yes" : "no", s.hooksOk ? "ok" : "FAILED", hooks::renderHookInstalled() ? "ok" : "fallback", configFile().string(),
                           s.configError.empty() ? "" : "\n  error: " + s.configError, describePool(g.defaultEffect), g.holdLayout ? "yes" : "no", g.appCloseEffects ? "yes" : "no",
                           g.closeTimeoutMs, g.openEffects ? describePool(g.openEffect) : "off", effects().count(), effects().describe(), closer().count(), closer().describe(), effects().history());
    }

    std::string handleCommand(eHyprCtlOutputFormat format, const std::string& request) {
        std::stringstream        ss{request};
        std::vector<std::string> args;
        for (std::string tok; ss >> tok;)
            args.push_back(tok);

        const std::string sub = args.size() > 1 ? args[1] : "status";
        const std::string arg = args.size() > 2 ? args[2] : "";

        if (sub == "status")
            return statusText();

        if (sub == "version") {
            if (format == FORMAT_JSON)
                return std::format(R"({{"version": "{}", "abi": "{}", "runtimeAbi": "{}", "path": "{}", "loadedMtime": {}}})", HWA_VERSION, HWA_BUILD_ABI,
                                   jsonEscape(__hyprland_api_get_hash()), jsonEscape(pluginPath()), g_loadedMtime);
            return std::format("hypr-window-anim {}\nbuilt for {}\nrunning {}\npath {}", HWA_VERSION, HWA_BUILD_ABI, __hyprland_api_get_hash(), pluginPath());
        }

        if (sub == "reload")
            return reloadConfig(false) ? "ok" : "error: " + state().configError;

        if (sub == "list") {
            std::string out;
            for (const auto& e : listEffects())
                out += std::format("{}\t{}\n", e.name, e.origin);
            return out;
        }

        if (sub == "check") {
            // Default: every effect the config uses
            std::vector<std::string> names{args.begin() + std::min<size_t>(2, args.size()), args.end()};
            if (names.empty()) {
                auto all = configuredEffects(state().config);
                all.insert("fade");
                names.assign(all.begin(), all.end());
            }

            std::string out = state().configError.empty() ? std::format("config: ok ({})\n", configFile().string()) :
                                                             std::format("config: ERROR {}\n", state().configError);
            bool        ok  = state().configError.empty();
            for (const auto& n : names) {
                auto r = effects().check(n);
                out += r ? std::format("effect {}: ok (GPU)\n", n) : std::format("effect {}: ERROR {}\n", n, r.error());
                ok = ok && r.has_value();
            }
            return (ok ? "ok\n" : "error\n") + out;
        }

        if (sub == "preview") {
            const auto w = Desktop::focusState()->window();
            if (!w)
                return "error: no focused window";
            const auto effect = arg.empty() ? std::optional<std::string>{} : std::optional<std::string>{arg};
            return closer().preview(w, effect) ? "ok" : "error: window not eligible (fullscreen, grouped, pinned, animating or disabled)";
        }

        return std::format("error: unknown subcommand '{}'", sub);
    }

    std::string onHyprctl(eHyprCtlOutputFormat format, std::string request) {
        std::string out;
        guarded("hyprctl", [&] { out = handleCommand(format, request); });
        return out.empty() ? "error: internal error, see hyprland.log" : out;
    }

    SDispatchResult dispatchClose(std::string arg) {
        const auto w = Desktop::focusState()->window();
        if (!w)
            return {.success = false, .error = "no focused window"};

        bool handled = false;
        guarded("hwanim:close", [&] {
            const auto effect = arg.empty() ? std::optional<std::string>{} : std::optional<std::string>{arg};
            handled           = closer().onCloseRequest(w, effect);
        });
        if (!handled)
            hooks::sendCloseOriginal(w);
        return {};
    }

    SDispatchResult dispatchPreview(std::string arg) {
        const auto w = Desktop::focusState()->window();
        if (!w)
            return {.success = false, .error = "no focused window"};

        bool ok = false;
        guarded("hwanim:preview", [&] { ok = closer().preview(w, arg.empty() ? std::optional<std::string>{} : std::optional<std::string>{arg}); });
        return ok ? SDispatchResult{} : SDispatchResult{.success = false, .error = "window not eligible"};
    }

    // Also runs on failed init: Hyprland skips pluginExit then
    void shutdown() {
        g_unloading = true;

        watcher::stop();
        guarded("shutdown", [] { closer().flushAll(); });
        guarded("shutdown", [] {
            // Last frame's pass is cleared only on next beginRender
            makeGLCurrent();
            g_pHyprRenderer->m_renderPass.removeAllOfType(EFFECT_PASS_NAME);
            effects().clear();
            shaderCache().clear();
        });

        g_listeners.clear();
        cancelAllTimers();

        if (g_command) {
            HyprlandAPI::unregisterHyprCtlCommand(PHANDLE, g_command);
            g_command.reset();
        }

        for (const auto& m : State::monitorState()->monitors())
            g_pHyprRenderer->damageMonitor(m);
    }

    void init() {
        g_unloading = false;
        reloadConfig(true);

        std::string error;
        if (!hooks::install(error))
            throw std::runtime_error(error);
        state().hooksOk = true;

        auto& ev = Event::bus()->m_events;
        g_listeners.push_back(ev.render.pre.listen([](PHLMONITOR m) {
            if (g_unloading)
                return;
            guarded("render.pre", [&] {
                if (state().needCompile) {
                    state().needCompile = false;
                    effects().precompile();
                }
                closer().onRenderPre();
            });
        }));

        if (!hooks::renderHookInstalled()) {
            g_listeners.push_back(ev.render.stage.listen([](eRenderStage stage) {
                if (g_unloading || stage != RENDER_POST_WINDOWS || g_pHyprRenderer->m_bRenderingSnapshot)
                    return;
                guarded("render.stage", [] {
                    if (const auto m = g_pHyprRenderer->m_renderData.pMonitor.lock())
                        effects().addPassElementsFallback(m);
                });
            }));
        }

        g_listeners.push_back(ev.window.open.listen([](PHLWINDOW w) {
            if (!g_unloading)
                guarded("window.open", [&] { closer().onWindowOpen(w); });
        }));

        // after mapWindow started windowsIn, so it can be replaced
        g_listeners.push_back(ev.window.openLate.listen([](PHLWINDOW w) {
            if (!g_unloading)
                guarded("window.openLate", [&] { animateOpen(w); });
        }));

        // fires before Hyprland's close snapshot, which must show the real window
        g_listeners.push_back(ev.window.close.listen([](PHLWINDOW w) {
            if (!g_unloading)
                guarded("window.close", [&] { effects().cancelOpen(w); });
        }));

        g_listeners.push_back(ev.monitor.removed.listen([](PHLMONITOR m) {
            if (g_unloading)
                return;
            guarded("monitor.removed", [&] {
                closer().onMonitorRemoved(m);
                effects().onMonitorRemoved(m);
            });
        }));

        g_command = HyprlandAPI::registerHyprCtlCommand(PHANDLE, SHyprCtlCommand{.name = "hwanim", .exact = false, .fn = onHyprctl});
        HyprlandAPI::addDispatcherV2(PHANDLE, "hwanim:close", dispatchClose);
        HyprlandAPI::addDispatcherV2(PHANDLE, "hwanim:preview", dispatchPreview);
    }
}

APICALL EXPORT std::string PLUGIN_API_VERSION() {
    return HYPRLAND_API_VERSION;
}

APICALL EXPORT PLUGIN_DESCRIPTION_INFO PLUGIN_INIT(HANDLE handle) {
    PHANDLE = handle;

    // Check before touching internals of a foreign ABI
    const std::string HASH        = __hyprland_api_get_hash();
    const std::string CLIENT_HASH = __hyprland_api_get_client_hash();
    if (HASH != CLIENT_HASH) {
        notify(std::format("not loaded: built for Hyprland {}, running {}; rebuild the plugin", CLIENT_HASH, HASH), true);
        throw std::runtime_error("version mismatch");
    }

    if (!g_pHyprRenderer || g_pHyprRenderer->type() != Render::IHyprRenderer::RT_GL || !Render::GL::g_pHyprOpenGL) {
        notify("not loaded: only the OpenGL renderer is supported", true);
        throw std::runtime_error("unsupported renderer");
    }

    try {
        init();
    } catch (const std::exception& e) {
        shutdown();
        notify(std::format("not loaded: {}", e.what()), true);
        throw;
    }

    g_loadedMtime = fileMtime(pluginPath());
    log(std::format("loaded {} from {}", HWA_VERSION, pluginPath()));
    return {"hypr-window-anim", "Shader close animations with delayed layout reflow", "axenos", HWA_VERSION};
}

APICALL EXPORT void PLUGIN_EXIT() {
    shutdown();
}
