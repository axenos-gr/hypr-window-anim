#pragma once

#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace hwa::cli {
    struct SInstance {
        std::string           signature;
        std::filesystem::path socket;
    };

    std::filesystem::path                 runtimeHyprDir();
    std::vector<std::string>              listInstances();
    // Explicit signature, else $HYPRLAND_INSTANCE_SIGNATURE
    std::expected<SInstance, std::string> resolveInstance(const std::string& explicitSignature);
    std::expected<std::string, std::string> request(const SInstance& inst, const std::string& req);

    // First string value of key, flat JSON only
    std::optional<std::string> jsonString(const std::string& json, const std::string& key);
}
