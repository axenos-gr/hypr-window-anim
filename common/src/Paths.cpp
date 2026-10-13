#include "hwa/Paths.hpp"

#include <cstdlib>

namespace hwa {
    std::filesystem::path configDir() {
        if (const char* dir = std::getenv("HWA_CONFIG_DIR"); dir && *dir)
            return dir;

        if (const char* xdg = std::getenv("XDG_CONFIG_HOME"); xdg && *xdg)
            return std::filesystem::path{xdg} / "hypr-window-anim";

        const char* home = std::getenv("HOME");
        return std::filesystem::path{home ? home : "/tmp"} / ".config" / "hypr-window-anim";
    }

    std::filesystem::path configFile() {
        return configDir() / "config.toml";
    }

    std::filesystem::path userEffectsDir() {
        return configDir() / "effects";
    }
}
