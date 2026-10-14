#include "WindowBlur.hpp"

#include <hyprland/src/config/ConfigValue.hpp>
#include <hyprland/src/desktop/rule/windowRule/WindowRuleApplicator.hpp>
#include <hyprland/src/desktop/view/WLSurface.hpp>
#include <hyprland/src/desktop/view/Window.hpp>
#include <hyprland/src/managers/fullscreen/FullscreenController.hpp>
#include <hyprland/src/output/Monitor.hpp>
#include <hyprland/src/output/MonitorResources.hpp>
#include <hyprland/src/protocols/core/Compositor.hpp>
#include <hyprland/src/render/Renderer.hpp>

namespace hwa::plugin {
    SWindowBlur windowBlurFor(PHLWINDOW w, bool ignoreNoBlur) {
        SWindowBlur b;
        if (!w)
            return b;

        CConfigValue<Config::INTEGER> enabled("decoration:blur:enabled");
        CConfigValue<Config::INTEGER> ignoreOpacity("decoration:blur:ignore_opacity");
        CConfigValue<Config::INTEGER> newOptimizations("decoration:blur:new_optimizations");
        CConfigValue<Config::INTEGER> xray("decoration:blur:xray");
        if (!*enabled)
            return b;

        // IHyprRenderer::shouldBlur (protected)
        const auto& rules = w->m_ruleApplicator;
        if ((!ignoreNoBlur && rules->noBlur().valueOrDefault()) || rules->RGBX().valueOrDefault() || w->opaque())
            return b;

        const auto surface = w->wlSurface();
        if (surface && surface->m_hasBackgroundEffect && surface->m_blurRegion.empty())
            return b;

        // drawSurface only blurs see-through windows
        const auto  resource = surface ? surface->resource() : nullptr;
        const auto  tex      = resource ? resource->m_current.texture : nullptr;
        const float alpha    = (rules->opaque().valueOrDefault() ? 1.F : w->alphaValue(Desktop::View::WINDOW_ALPHA_ACTIVE)) * (surface ? surface->m_alphaModifier : 1.F);
        const float overall  = surface ? surface->m_overallOpacity : 1.F;
        if (tex && tex->m_opaque && alpha >= 1.F && overall >= 1.F)
            return b;

        // IHyprRenderer::shouldUseNewBlurOptimizations, minus its blur texture check
        const auto ruleXray = rules->xray();
        if (ruleXray.hasValue() && !ruleXray.valueOrDefault())
            b.precomputed = false;
        else
            b.precomputed = (*newOptimizations && !w->m_isFloating && !w->onSpecialWorkspace()) || *xray || ruleXray.valueOrDefault();

        const bool dontRound = Fullscreen::controller()->getFullscreenModes(w).internal == Fullscreen::FSMODE_FULLSCREEN;
        b.enabled            = true;
        b.surface            = {w->position(Desktop::View::IGeometric::GEOMETRIC_CURRENT), w->size(Desktop::View::IGeometric::GEOMETRIC_CURRENT)};
        b.rounding           = dontRound ? 0.F : w->rounding();
        b.roundingPower      = dontRound ? 2.F : w->roundingPower();
        b.alpha              = (*ignoreOpacity ? 1.F : alpha) * overall;
        b.strength           = alpha;
        return b;
    }

    SP<Render::ITexture> blurTextureFor(const SWindowBlur& b, PHLMONITOR mon, SP<Render::IFramebuffer> behind) {
        // uv mapping assumes an untransformed monitor
        if (!b.enabled || !mon || mon->m_transform != WL_OUTPUT_TRANSFORM_NORMAL)
            return nullptr;

        if (b.precomputed) {
            // a weak ref to a unique ptr: use it, never lock() it (hyprutils asserts)
            if (const auto res = mon->resources(); res && res->m_blurFB) {
                if (auto tex = res->m_blurFB->getTexture(); tex && tex->ok())
                    return tex;
            }
        }

        if (!behind || !behind->getTexture())
            return nullptr;

        CRegion damage{CBox{b.surface}.translate(-mon->m_position).scale(mon->m_scale).expand(2).round()};
        auto    guard = g_pHyprRenderer->bindTempFB(behind); // blurFramebuffer messes with FB bindings
        return g_pHyprRenderer->blurFramebuffer(behind, b.strength, &damage);
    }

    SBlurDraw blurDrawFor(const SWindowBlur& b, SP<Render::ITexture> tex, const CBox& effectBox, double scale) {
        if (!tex)
            return {};

        // drawSurface takes one pixel off the radius
        const CBox rect = CBox{b.surface.pos() - effectBox.pos(), b.surface.size()}.scale(scale).round();
        return {.tex = std::move(tex), .rect = rect, .radius = std::max(0.F, b.rounding * float(scale) - 1.F), .power = b.roundingPower, .alpha = b.alpha};
    }
}
