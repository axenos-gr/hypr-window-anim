#include "OpenTransformer.hpp"
#include "EffectPassElement.hpp"
#include "Plugin.hpp"

#include <hyprland/src/desktop/rule/windowRule/WindowRuleApplicator.hpp>
#include <hyprland/src/desktop/view/window/Window.hpp>
#include <hyprland/src/desktop/view/window/WindowPresentation.hpp>
#include <hyprland/src/output/Monitor.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/render/pass/ClearPassElement.hpp>

#include <algorithm>
#include <format>

namespace hwa::plugin {
    COpenTransformer::COpenTransformer(SP<SEffect> effect) : m_effect(std::move(effect)) {}

    bool COpenTransformer::active() const {
        return m_active;
    }

    void COpenTransformer::deactivate() {
        m_active = false;
    }

    CBox COpenTransformer::transformedExtents(const CBox& currentBox) const {
        return m_active ? currentBox.copy().expand(m_effect->padding) : currentBox;
    }

    // Modeled on CWobbleTransformer::transform
    Render::SWindowTransformBuffer COpenTransformer::transform(Render::CRenderContext& ctx, const Render::SWindowTransformBuffer& in,
                                                               const Render::SWindowTransformContext& context) {
        Render::SWindowTransformBuffer result = in;

        guarded("open transform", [&] {
            const auto mon = context.monitor.lock();
            const auto w   = m_effect->window.lock();
            // a snapshot of an opening window should show the window, not the effect
            if (g_unloading || !m_active || !mon || !w || context.standalone || context.renderingSnapshot || ctx.m_renderingSnapshot || !in.framebuffer ||
                !in.framebuffer->getTexture() || in.box.empty())
                return;

            const double SCALE     = mon->m_scale;
            const CBox   OUTCANVAS = Render::pixelBoxForLogical(context.outputBox.copy().expand(1.0 / SCALE), SCALE);
            if (OUTCANVAS.empty())
                return;

            auto out = g_pHyprRenderer->getWorkBuffer(ctx, OUTCANVAS.size());
            if (!out) {
                result.success = false;
                return;
            }

            const CBox full          = w->getFullWindowBoundingBox();
            m_effect->paddingLogical = m_effect->padding + std::max(0.0, w->position(Desktop::View::IGeometric::GEOMETRIC_CURRENT).x - full.x);
            m_effect->fullscreen     = false;

            const CBox effectLocal = context.currentBox.copy().expand(m_effect->padding).scale(SCALE);
            const CBox effectBox   = effectLocal.copy().translate(-OUTCANVAS.pos());
            const CBox inBox       = in.box.copy().translate(-OUTCANVAS.pos());
            const CBox monitorBox  = CBox{{}, mon->m_transformedSize}.translate(-OUTCANVAS.pos());

            // our own blur underneath; Hyprland's is off for the effect (see startOpen)
            m_effect->blur = windowBlurFor(w, m_effect->ownsNoBlur);
            m_effect->blur.surface.translate(m_effect->liveOffset());
            auto behind = m_behind.lock();
            if (!behind)
                behind = ctx.m_data.mainFB;
            auto       blurSrc  = blurSourceFor(ctx, m_effect->blur, mon, behind);
            const auto blurDraw = blurDrawFor(m_effect->blur, std::move(blurSrc), monitorBox, CBox{effectLocal}.scale(1.0 / SCALE).translate(mon->m_position), SCALE);

            Render::GL::CFramebufferBindingGuard bindings{g_pHyprRenderer->glBackend()};
            auto                                 state = ctx.saveDrawState();
            auto&                                rd    = ctx.m_data;
            g_pHyprRenderer->bindFB(ctx, out);
            rd.damage          = CRegion{CBox{{}, OUTCANVAS.size()}};
            rd.fbSize          = OUTCANVAS.size();
            rd.transformDamage = false;
            rd.renderModif     = {};
            g_pHyprRenderer->setProjectionType(ctx, Render::RPT_EXPORT);

            g_pHyprRenderer->draw(ctx, CClearPassElement::SClearData{CHyprColor(0, 0, 0, 0)});
            drawEffectQuad(ctx, *m_effect, in.framebuffer->getTexture(), effectBox, inBox, false, blurDraw);

            result = {.framebuffer = out, .box = OUTCANVAS};
        });

        return result;
    }

    void COpenTransformer::preWindowRender(Render::CRenderContext& ctx, CSurfacePassElement::SRenderData* pRenderData) {
        m_behind = ctx.m_data.currentFB;
    }

    namespace {
        const char* openBlocker(PHLWINDOW w) {
            if (!Desktop::View::validMapped(w) || w->isHidden())
                return "not mapped or hidden";
            if (w->backend().traits().overrideRedirect)
                return "override-redirect";

            const auto ws = w->m_workspace;
            if (!ws || !ws->visible())
                return "workspace not visible";
            if (w->m_ruleApplicator->noAnim().valueOrDefault())
                return "no_anim rule";
            // covered by a fullscreen window
            if (w->presentation().alphaGoal(Desktop::View::WINDOW_ALPHA_FULLSCREEN) < 0.99F)
                return "behind fullscreen";
            if (!w->m_monitor.lock() || !g_pHyprRenderer->shouldRenderWindow(w))
                return "not rendered";
            return nullptr;
        }
    }

    void animateOpen(PHLWINDOW w) {
        if (g_unloading || !w)
            return;

        const auto d = decide(state().config, factsFor(w), eAnim::OPEN);
        if (!mayAnimate(d.effects))
            return;

        if (const auto why = openBlocker(w)) {
            log(std::format("not animating open of {}: {}", w->metadata().appID(), why));
            return;
        }

        const auto name = pickEffect(d.effects, eAnim::OPEN);
        if (name == "none" || !effects().startOpen(w, name, d.durationMs))
            return;

        // one animation at a time: finish popin/fade-in instantly
        w->positionAnimation()->warp();
        w->sizeAnimation()->warp();
        w->presentation().alpha(Desktop::View::WINDOW_ALPHA_FADE)->warp();
    }
}
