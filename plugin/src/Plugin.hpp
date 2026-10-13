#pragma once

#include <hyprland/src/plugins/PluginAPI.hpp>
#include <hyprland/src/desktop/DesktopTypes.hpp>
#include <hyprland/src/managers/eventLoop/EventLoopTimer.hpp>

#include "hwa/Config.hpp"

#include <chrono>
#include <functional>
#include <string>
#include <vector>

namespace hwa::plugin {
    inline HANDLE PHANDLE     = nullptr;
    inline bool   g_unloading = false;

    struct SState {
        SConfig     config;
        std::string configError; // last failed reload
        bool        hooksOk     = false;
        bool        needCompile = true;
    };

    SState&             state();

    void                log(const std::string& msg);
    void                logError(const std::string& msg);
    void                notify(const std::string& msg, bool error = false);

    // Cancelled on unload: callbacks live in our .so
    SP<CEventLoopTimer> addTimer(std::chrono::milliseconds delay, std::function<void()> fn);
    void                cancelTimer(SP<CEventLoopTimer>& timer);
    void                cancelAllTimers();

    // GL objects die with context current
    void                makeGLCurrent();

    // Exceptions must not reach Hyprland
    template <typename Fn>
    bool guarded(const char* where, Fn&& fn) {
        try {
            fn();
            return true;
        } catch (const std::exception& e) { logError(std::string{where} + ": " + e.what()); } catch (...) {
            logError(std::string{where} + ": unknown exception");
        }
        return false;
    }

    bool reloadConfig(bool notifyOnError);
}
