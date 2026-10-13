#include "CloseController.hpp"
#include "EffectManager.hpp"
#include "Hooks.hpp"
#include "Plugin.hpp"

#include <hyprland/src/config/ConfigValue.hpp>
#include <hyprland/src/config/shared/animation/AnimationTree.hpp>
#include <hyprland/src/desktop/Workspace.hpp>
#include <hyprland/src/desktop/history/WindowHistoryTracker.hpp>
#include <hyprland/src/desktop/rule/windowRule/WindowRuleApplicator.hpp>
#include <hyprland/src/desktop/state/FocusState.hpp>
#include <hyprland/src/desktop/state/ViewState.hpp>
#include <hyprland/src/desktop/state/WindowState.hpp>
#include <hyprland/src/desktop/view/Window.hpp>
#include <hyprland/src/layout/target/Target.hpp>
#include <hyprland/src/managers/fullscreen/FullscreenController.hpp>
#include <hyprland/src/managers/input/InputManager.hpp>
#include <hyprland/src/render/Renderer.hpp>

#include <algorithm>
#include <format>
#include <ranges>

using Desktop::View::WINDOW_ALPHA_FADE;

namespace hwa::plugin {
    CCloseController& closer() {
        static CCloseController c;
        return c;
    }

    Desktop::eFadeoutPlane planeFor(PHLWINDOW w) {
        if (!w->m_isFloating)
            return Desktop::FADEOUT_PLANE_WINDOW_TILED;
        if (w->shouldRenderOverFullscreen() && w->m_workspace && Fullscreen::controller()->hasFullscreen(w->m_workspace, true))
            return Desktop::FADEOUT_PLANE_WINDOW_OVER_FULLSCREEN;
        return Desktop::FADEOUT_PLANE_WINDOW_FLOATING;
    }

    namespace {
        SWindowFacts factsFor(PHLWINDOW w) {
            return {.cls = w->m_class, .title = w->m_title, .floating = w->m_isFloating, .xwayland = w->m_isX11};
        }

        // Reason to skip holding, nullptr if eligible
        const char* holdBlocker(PHLWINDOW w) {
            if (!Desktop::View::validMapped(w) || w->isHidden())
                return "not mapped or hidden";
            if (w->m_group || w->m_pinned)
                return "grouped or pinned";
            if (w->isX11OverrideRedirect())
                return "override-redirect";

            const auto ws = w->m_workspace;
            if (!ws || !ws->isVisible())
                return "workspace not visible";
            if (ws->m_renderOffset->isBeingAnimated() || ws->m_alpha->isBeingAnimated())
                return "workspace animating";
            if (Fullscreen::controller()->isFullscreen(w))
                return "fullscreen";

            // Close gesture lowers the goal; fade-in does not
            if (w->alphaGoal(WINDOW_ALPHA_FADE) < 0.99F)
                return "close gesture in progress";
            if (w->m_ruleApplicator->noAnim().valueOrDefault())
                return "no_anim rule";
            if (!w->m_monitor.lock() || !g_pHyprRenderer->shouldRenderWindow(w))
                return "not rendered";
            return nullptr;
        }
    }

    SP<SPendingClose> CCloseController::find(PHLWINDOW w) const {
        for (const auto& p : m_pending) {
            if (p->window.lock() == w)
                return p;
        }
        return nullptr;
    }

    void CCloseController::erase(SP<SPendingClose> p) {
        cancelTimer(p->timer);
        std::erase(m_pending, p);
    }

    bool CCloseController::isPending(PHLWINDOW w) const {
        return find(w) != nullptr;
    }

    size_t CCloseController::count() const {
        return m_pending.size();
    }

    std::string CCloseController::describe() const {
        std::string out;
        for (const auto& p : m_pending) {
            const auto w = p->window.lock();
            out += std::format("  holding {} [{}]{}{}\n", w ? w->m_class : "<gone>", w ? w->m_title : "", p->closeSent ? " close sent" : "", p->preview ? " preview" : "");
        }
        return out;
    }

    bool CCloseController::onCloseRequest(PHLWINDOW w, std::optional<std::string> effectOverride) {
        if (g_unloading || !w)
            return false;

        if (find(w))
            return true; // swallow repeated requests

        return hold(w, std::move(effectOverride), false);
    }

    bool CCloseController::preview(PHLWINDOW w, std::optional<std::string> effect) {
        if (g_unloading || !w || find(w))
            return false;
        return hold(w, std::move(effect), true);
    }

    bool CCloseController::hold(PHLWINDOW w, std::optional<std::string> effect, bool preview) {
        const auto& cfg = state().config;
        auto        d   = decide(cfg, factsFor(w), eAnim::CLOSE);

        if (effect)
            d.effects = {*effect};
        if (preview)
            d.hold = true;
        if (!cfg.general.enabled && !effect && !preview)
            return false;
        if (!mayAnimate(d.effects) || !d.hold)
            return false;
        if (const auto why = holdBlocker(w)) {
            log(std::format("not holding {}: {}", w->m_class, why));
            return false;
        }

        const auto name = pickEffect(d.effects, eAnim::CLOSE);
        if (name == "none") {
            m_skipFadeout.emplace_back(w, Time::steadyNow()); // the unmap must not pick again
            return false;
        }

        effects().cancelOpen(w);

        // Hidden windows snapshot as empty
        auto snapshot = g_pHyprRenderer->makeSnapshotFB(w);
        if (!snapshot)
            return false;

        const auto id = effects().start({.window = w, .snapshot = snapshot, .plane = planeFor(w), .effect = name, .durationMs = d.durationMs});
        if (!id)
            return false;

        auto p      = makeShared<SPendingClose>();
        p->window   = w;
        p->preview  = preview;
        p->hadFocus = Desktop::focusState()->window() == w;
        m_pending.push_back(p);

        w->setHidden(true); // keeps the layout slot
        g_pHyprRenderer->damageWindow(w, true);

        if (!preview)
            refocusAway(w);

        WP<SPendingClose> weak = p;
        p->timer               = addTimer(std::chrono::milliseconds{effects().durationMsFor(*id)}, [this, weak] {
            if (auto p = weak.lock())
                onEffectEnd(p);
        });

        log(std::format("holding {} for '{}'{}", w->m_class, name, preview ? " (preview)" : ""));
        return true;
    }

    void CCloseController::onEffectEnd(SP<SPendingClose> p) {
        cancelTimer(p->timer);

        const auto w = p->window.lock();
        if (!w || !w->m_isMapped) {
            erase(p);
            return;
        }

        if (p->preview) {
            revert(p, false);
            return;
        }

        p->closeSent = true;
        hooks::sendCloseOriginal(w);

        // App may refuse, e.g. unsaved changes
        WP<SPendingClose> weak = p;
        p->timer               = addTimer(std::chrono::milliseconds{state().config.general.closeTimeoutMs}, [this, weak] {
            if (auto p = weak.lock()) {
                log("close timed out, restoring window");
                revert(p, true);
            }
        });
    }

    void CCloseController::revert(SP<SPendingClose> p, bool refocus) {
        erase(p);

        const auto w = p->window.lock();
        if (!w || !w->m_isMapped)
            return;

        w->setHidden(false);
        w->layoutTarget()->recalc();
        w->updateDecorationValues();
        w->sendWindowSize(true);

        auto& alpha = w->alpha(WINDOW_ALPHA_FADE);
        alpha->setConfig(Config::animationTree()->getAnimationPropertyConfig("fadeIn"));
        alpha->setValueAndWarp(0.F);
        *alpha = 1.F;

        g_pHyprRenderer->damageWindow(w, true);

        if (refocus && p->hadFocus && !Desktop::focusState()->window())
            Desktop::focusState()->fullWindowFocus(w, Desktop::FOCUS_REASON_OTHER);
    }

    PHLWINDOW CCloseController::pickCandidate(PHLWINDOW held) const {
        auto usable = [&](const PHLWINDOW& c) {
            return c && c != held && Desktop::View::validMapped(c) && !c->isHidden() && c->m_workspace == held->m_workspace && !isPending(c);
        };

        // Honour input:focus_on_close
        CConfigValue<Config::INTEGER> focusOnClose("input:focus_on_close");
        if (focusOnClose.good() && *focusOnClose == 1) {
            const auto c = Desktop::viewState()->hitTest().windowAt(g_pInputManager->getMouseCoordsInternal(),
                                                                    Desktop::View::RESERVED_EXTENTS | Desktop::View::INPUT_EXTENTS | Desktop::View::ALLOW_FLOATING);
            if (usable(c))
                return c;
        }

        if (!held->m_isFloating) {
            PHLWINDOW  best;
            double     bestDist = 0;
            const auto center   = held->middle();
            for (const auto& c : Desktop::windowState()->windows()) {
                if (!usable(c) || c->m_isFloating)
                    continue;
                const auto dist = c->middle().distance(center);
                if (!best || dist < bestDist) {
                    best     = c;
                    bestDist = dist;
                }
            }
            if (best)
                return best;
        }

        for (const auto& ref : Desktop::History::windowTracker()->fullHistory() | std::views::reverse) {
            if (const auto c = ref.lock(); usable(c))
                return c;
        }
        return nullptr;
    }

    // setHidden() keeps focus; layout would pick the held window
    void CCloseController::refocusAway(PHLWINDOW held) {
        if (Desktop::focusState()->window() != held)
            return;

        const auto c = pickCandidate(held);
        if (c)
            Desktop::focusState()->fullWindowFocus(c, held->m_isFloating ? Desktop::FOCUS_REASON_UNMAP_WINDOW_FLOATING : Desktop::FOCUS_REASON_UNMAP_WINDOW_TILING);
        else
            Desktop::focusState()->fullWindowFocus(nullptr, Desktop::FOCUS_REASON_OTHER);

        g_pInputManager->sendMotionEventsToFocused();
    }

    bool CCloseController::onFadeoutCreate(PHLWINDOW w, SP<Render::IFramebuffer> snapshot) {
        if (g_unloading || !w)
            return false;

        if (auto p = find(w)) {
            erase(p); // already animated
            return true;
        }

        bool       skip  = false;
        const auto limit = std::chrono::milliseconds{state().config.general.closeTimeoutMs};
        std::erase_if(m_skipFadeout, [&](const auto& entry) {
            const auto other = entry.first.lock();
            const bool stale = !other || Time::steadyNow() - entry.second > limit; // app refused, closed later
            skip |= !stale && other == w;
            return stale || other == w;
        });
        if (skip)
            return false; // "none" was picked when the close was requested

        const auto& cfg = state().config;
        if (!cfg.general.enabled || !cfg.general.appCloseEffects || !snapshot || w->isX11OverrideRedirect())
            return false;

        const auto d = decide(cfg, factsFor(w), eAnim::CLOSE);
        if (!mayAnimate(d.effects))
            return false;

        const auto name = pickEffect(d.effects, eAnim::CLOSE);
        if (name == "none")
            return false;

        return effects().start({.window = w, .snapshot = snapshot, .plane = planeFor(w), .effect = name, .durationMs = d.durationMs}).has_value();
    }

    void CCloseController::onWindowOpen(PHLWINDOW w) {
        for (const auto& p : std::vector{m_pending}) {
            const auto held = p->window.lock();
            if (!p->closeSent || !held || held == w)
                continue;

            const bool child  = w->parent() == held;
            const bool sibling = w->m_isFloating && held->getPID() > 0 && w->getPID() == held->getPID();
            if (!child && !sibling)
                continue;

            // Likely a save dialog; defer out of mapWindow
            WP<SPendingClose> weak = p;
            cancelTimer(p->timer);
            p->timer = addTimer(std::chrono::milliseconds{0}, [this, weak] {
                if (auto p = weak.lock())
                    revert(p, false);
            });
        }
    }

    void CCloseController::onRenderPre() {
        // Hyprland un-hides on layout switch, resize, X11 configure
        for (const auto& p : m_pending) {
            if (const auto w = p->window.lock(); w && w->m_isMapped && !w->isHidden())
                w->setHidden(true);
        }
    }

    void CCloseController::onMonitorRemoved(PHLMONITOR monitor) {
        for (const auto& p : std::vector{m_pending}) {
            const auto w = p->window.lock();
            if (w && w->m_monitor && w->m_monitor != monitor)
                continue;

            const bool sendClose = !p->preview && !p->closeSent;
            revert(p, false);
            if (sendClose && w && w->m_isMapped)
                hooks::sendCloseOriginal(w);
        }
    }

    void CCloseController::flushAll() {
        for (const auto& p : std::vector{m_pending}) {
            erase(p);

            const auto w = p->window.lock();
            if (!w || !w->m_isMapped)
                continue;

            w->setHidden(false);
            w->layoutTarget()->recalc();
            g_pHyprRenderer->damageWindow(w, true);

            if (!p->preview && !p->closeSent)
                hooks::sendCloseOriginal(w);
        }
    }
}
