#include "OpenTransformer.hpp"
#include "EffectPassElement.hpp"
#include "Plugin.hpp"

#include <hyprland/src/desktop/Workspace.hpp>
#include <hyprland/src/desktop/rule/windowRule/WindowRuleApplicator.hpp>
#include <hyprland/src/desktop/view/Window.hpp>
#include <hyprland/src/output/Monitor.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/render/pass/ClearPassElement.hpp>

#include <algorithm>
#include <format>

namespace hwa::plugin {
    COpenTransformer::COpenTransformer(SP<SEffect> effect) : m_effect(std::move(effect)) {}

    COpenTransformer::~COpenTransformer() {
        makeGLCurrent();
        m_outs.clear();
    }

    SP<Render::IFramebuffer> COpenTransformer::outFor(const SP<Render::IFramebuffer>& in) {
        for (auto& [key, fb] : m_outs) {
            if (key == in.get())
                return fb;
        }

        if (m_outs.size() >= 3) // work buffers rotate; don't grow forever
            m_outs.erase(m_outs.begin());

        auto fb = g_pHyprRenderer->createFB("hwanim open");
        m_outs.emplace_back(in.get(), fb);
        return fb;
    }

    SP<Render::IFramebuffer> COpenTransformer::transform(SP<Render::IFramebuffer> in) {
        SP<Render::IFramebuffer> result = in;

        guarded("open transform", [&] {
            const auto mon = g_pHyprRenderer->m_renderData.pMonitor.lock();
            const auto w   = m_effect->window.lock();
            // a snapshot of an opening window should show the window, not the effect
            if (g_unloading || !mon || !w || !in || !in->getTexture() || g_pHyprRenderer->m_bRenderingSnapshot)
                return;

            const auto box = m_effect->liveBox();
            if (!box)
                return;

            auto out = outFor(in);
            if (!out->alloc(in->m_size.x, in->m_size.y, in->m_drmFormat))
                return;
            out->setImageDescription(in->imageDescription());

            const CBox full         = w->getFullWindowBoundingBox();
            m_effect->paddingLogical = m_effect->padding + std::max(0.0, w->position(Desktop::View::IGeometric::GEOMETRIC_CURRENT).x - full.x);
            m_effect->fullscreen     = false;

            const CBox local = box->copy().translate(-mon->m_position).scale(mon->m_scale);

            auto       guard = g_pHyprRenderer->bindTempFB(out);
            g_pHyprRenderer->draw(CClearPassElement::SClearData{CHyprColor(0, 0, 0, 0)});
            // the work buffer already holds the window where it is drawn; no render modifs
            drawEffectQuad(*m_effect, in->getTexture(), local, false, false);

            result = out;
        });

        return result;
    }

    namespace {
        const char* openBlocker(PHLWINDOW w) {
            if (!Desktop::View::validMapped(w) || w->isHidden())
                return "not mapped or hidden";
            if (w->isX11OverrideRedirect())
                return "override-redirect";

            const auto ws = w->m_workspace;
            if (!ws || !ws->isVisible())
                return "workspace not visible";
            if (w->m_ruleApplicator->noAnim().valueOrDefault())
                return "no_anim rule";
            // covered by a fullscreen window
            if (w->alphaGoal(Desktop::View::WINDOW_ALPHA_FULLSCREEN) < 0.99F)
                return "behind fullscreen";
            if (!w->m_monitor.lock() || !g_pHyprRenderer->shouldRenderWindow(w))
                return "not rendered";
            return nullptr;
        }
    }

    void animateOpen(PHLWINDOW w) {
        if (g_unloading || !w)
            return;

        const auto d = decide(state().config, {.cls = w->m_class, .title = w->m_title, .floating = w->m_isFloating, .xwayland = w->m_isX11}, eAnim::OPEN);
        if (!mayAnimate(d.effects))
            return;

        if (const auto why = openBlocker(w)) {
            log(std::format("not animating open of {}: {}", w->m_class, why));
            return;
        }

        const auto name = pickEffect(d.effects, eAnim::OPEN);
        if (name == "none" || !effects().startOpen(w, name, d.durationMs))
            return;

        // one animation at a time: finish popin/fade-in instantly
        w->positionAnimation()->warp();
        w->sizeAnimation()->warp();
        w->alpha(Desktop::View::WINDOW_ALPHA_FADE)->warp();
    }
}
