#pragma once

#include <array>
#include <optional>
#include <span>
#include <string_view>

namespace hwa::assets {
    struct SAsset {
        std::string_view path;
        std::string_view data;
    };

    std::span<const SAsset>         all();
    std::optional<std::string_view> find(std::string_view path);
}
