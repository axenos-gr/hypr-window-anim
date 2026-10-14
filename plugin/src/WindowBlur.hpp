#pragma once

#include <hyprland/src/desktop/DesktopTypes.hpp>
#include <hyprland/src/helpers/math/Math.hpp>
#include <hyprland/src/render/Framebuffer.hpp>
#include <hyprland/src/render/Texture.hpp>

namespace hwa::plugin {
    // How Hyprland blurs behind a window, so effects can keep it
    struct SWindowBlur {
        bool  enabled       = false;
        bool  precomputed   = true; // new_optimizations/xray; else live blur of what's behind
        CBox  surface;              // global logical, same space as getFullWindowBoundingBox()
        float rounding      = 0.F;  // logical
        float roundingPower = 2.F;
        float alpha         = 1.F; // blur opacity
        float strength      = 1.F; // live blur radius factor; Hyprland passes the window alpha
    };

    // Mirrors shouldBlur and drawSurface; ignoreNoBlur skips our own opening override
    SWindowBlur windowBlurFor(PHLWINDOW w, bool ignoreNoBlur = false);

    // During rendering; behind = FB holding what's under the window, for live blur
    SP<Render::ITexture> blurTextureFor(const SWindowBlur& b, PHLMONITOR mon, SP<Render::IFramebuffer> behind);

    struct SBlurDraw {
        SP<Render::ITexture> tex;
        CBox                 rect; // window surface inside the effect box, px
        float                radius = 0.F, power = 2.F, alpha = 1.F;
    };

    // effectBox/surface: global logical; null tex disables blur
    SBlurDraw blurDrawFor(const SWindowBlur& b, SP<Render::ITexture> tex, const CBox& effectBox, double scale);
}
