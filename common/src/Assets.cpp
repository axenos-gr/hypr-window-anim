#include "hwa/Assets.hpp"

namespace hwa::assets {
    std::optional<std::string_view> find(std::string_view path) {
        for (const auto& a : all()) {
            if (a.path == path)
                return a.data;
        }
        return std::nullopt;
    }
}
