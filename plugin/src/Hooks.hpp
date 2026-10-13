#pragma once

#include <hyprland/src/desktop/DesktopTypes.hpp>

#include <string>

namespace hwa::plugin::hooks {
    // false = required hook missing
    bool install(std::string& error);
    bool renderHookInstalled();

    // Real close, bypassing our hook
    void sendCloseOriginal(PHLWINDOW w);
}
