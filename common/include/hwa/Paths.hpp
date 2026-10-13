#pragma once

#include <filesystem>

namespace hwa {
    // $HWA_CONFIG_DIR, then XDG, then ~/.config
    std::filesystem::path configDir();
    std::filesystem::path configFile();
    std::filesystem::path userEffectsDir();
}
