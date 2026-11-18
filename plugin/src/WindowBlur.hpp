#pragma once

#include <hyprland/src/desktop/DesktopTypes.hpp>
#include <hyprland/src/helpers/math/Math.hpp>
#include <hyprland/src/render/Framebuffer.hpp>
#include <hyprland/src/render/Texture.hpp>

namespace Render {
    class CRenderContext;
}

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

    // Mirrors CWindow::shouldBlur and drawSurface; ignoreNoBlur skips our own opening override
    SWindowBlur windowBlurFor(PHLWINDOW w, bool ignoreNoBlur = false);

    struct SBlurSource {
        SP<Render::ITexture>     tex;
        SP<Render::IFramebuffer> keepAlive; // pooled work buffers get reused once unreferenced
    };

    // Monitor-sized, monitor-local px; behind = FB holding what's under the window, for live blur
    SBlurSource blurSourceFor(Render::CRenderContext& ctx, const SWindowBlur& b, PHLMONITOR mon, SP<Render::IFramebuffer> behind);

    struct SBlurDraw {
        SBlurSource src;
        CBox        texBox; // area src.tex covers, target px
        CBox        rect;   // window surface inside the effect box, px
        float       radius = 0.F, power = 2.F, alpha = 1.F;
    };

    // effectBox/surface: global logical; texBox: target px; null tex disables blur
    SBlurDraw blurDrawFor(const SWindowBlur& b, SBlurSource src, const CBox& texBox, const CBox& effectBox, double scale);
}
