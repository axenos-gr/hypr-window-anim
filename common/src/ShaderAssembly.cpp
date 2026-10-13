#include "hwa/ShaderAssembly.hpp"

#include "hwa/Assets.hpp"

#include <format>

namespace hwa {
    std::string assembleFragment(std::string_view effectBody) {
        const auto prelude = assets::find("shaders/prelude.glsl").value_or("");
        const auto common  = assets::find("shaders/bmw-common.glsl").value_or("");

        return std::format("#version 300 es\n"
                           "precision highp float;\n"
                           "precision highp int;\n"
                           "#define HYPRLAND 1\n"
                           "#define texture2D texture\n"
                           "#line 1 2\n{}\n"
                           "#line 1 3\n{}\n"
                           "#line 1 1\n{}\n",
                           prelude, common, effectBody);
    }
}
