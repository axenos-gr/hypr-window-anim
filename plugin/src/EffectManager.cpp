#include "EffectManager.hpp"
#include "EffectPassElement.hpp"
#include "OpenTransformer.hpp"
#include "Plugin.hpp"

#include <hyprland/src/desktop/Workspace.hpp>
#include <hyprland/src/desktop/view/Window.hpp>
#include <hyprland/src/managers/fullscreen/FullscreenController.hpp>
#include <hyprland/src/output/Monitor.hpp>
#include <hyprland/src/render/Renderer.hpp>

#include <algorithm>
#include <format>
#include <random>
#include <set>

namespace hwa::plugin {
    CEffectManager& effects() {
        static CEffectManager mgr;
        return mgr;
    }

    float SEffect::elapsed() const {
        return std::chrono::duration<float>(Time::steadyNow() - start).count();
    }

    float SEffect::progress() const {
        return durationS > 0.F ? std::clamp(elapsed() / durationS, 0.F, 1.F) : 1.F;
    }

    // Matches the box Hyprland uses for transformed windows
    std::optional<CBox> SEffect::liveBox() const {
        const auto w = window.lock();
        if (!w)
            return std::nullopt;

        return w->getFullWindowBoundingBox().translate(liveOffset()).expand(padding);
    }

    Vector2D SEffect::liveOffset() const {
        const auto w = window.lock();
        if (!w)
            return {};
        const auto ws = w->m_workspace;
        return (w->m_pinned || !ws ? Vector2D{} : ws->m_renderOffset->value()) + w->m_floatingOffset;
    }

    std::expected<SP<SProgram>, std::string> CEffectManager::programFor(const std::string& name, SEffectSource& outSrc) {
        auto src = loadEffect(name);
        if (!src)
            return std::unexpected(src.error());

        auto prog = shaderCache().get(*src);
        if (prog)
            outSrc = std::move(*src);
        return prog;
    }

    std::optional<uint64_t> CEffectManager::start(const SEffectRequest& req) {
        const auto& cfg = state().config;
        const auto& w   = req.window;

        if (!w || !req.snapshot || !req.snapshot->getTexture())
            return std::nullopt;

        const auto mon = w->m_monitor.lock();
        if (!mon || std::cmp_greater_equal(m_effects.size(), cfg.general.maxConcurrent))
            return std::nullopt;

        makeGLCurrent();

        SEffectSource src;
        auto          prog = programFor(req.effect, src);
        if (!prog) {
            static std::set<std::string> reported;
            if (reported.insert(req.effect).second)
                notify(std::format("effect '{}' failed, using fade: {}", req.effect, prog.error().substr(0, 300)), true);
            logError(prog.error());

            prog = programFor("fade", src);
            if (!prog) {
                logError(prog.error());
                return std::nullopt;
            }
        }

        const auto resolved = resolveEffect(src, cfg, req.durationMs);
        const CBox full     = w->getFullWindowBoundingBox();
        const auto contentX = w->position(Desktop::View::IGeometric::GEOMETRIC_CURRENT).x;

        static std::mt19937 rng{std::random_device{}()};

        auto                e = makeShared<SEffect>();
        e->id                 = m_nextId++;
        e->monitor            = mon;
        e->workspace          = w->m_workspace;
        e->plane              = req.plane;
        e->snapshot           = req.snapshot;
        e->program            = *prog;
        e->params             = resolved.params;
        e->box                = full.copy().expand(resolved.padding);
        e->paddingLogical     = resolved.padding + std::max(0.0, contentX - full.x);
        e->fullscreen         = Fullscreen::controller()->isFullscreen(w);
        e->blur               = windowBlurFor(w);
        e->seed               = std::uniform_real_distribution<float>{0.F, 1.F}(rng);
        e->start              = Time::steadyNow();
        e->durationS          = resolved.durationMs / 1000.F;

        const auto ID = e->id;
        e->endTimer   = addTimer(std::chrono::milliseconds{resolved.durationMs + 30}, [ID] { effects().finish(ID); });

        m_effects.push_back(e);
        g_pHyprRenderer->damageBox(e->box);
        ensureTicking();

        log(std::format("effect {} '{}' started for {} ms", ID, src.name, resolved.durationMs));
        return ID;
    }

    std::optional<uint64_t> CEffectManager::startOpen(PHLWINDOW w, const std::string& effect, std::optional<int> durationMs) {
        const auto& cfg = state().config;
        const auto  mon = w ? w->m_monitor.lock() : nullptr;
        if (!mon || std::cmp_greater_equal(m_effects.size(), cfg.general.maxConcurrent))
            return std::nullopt;

        makeGLCurrent();

        SEffectSource src;
        auto          prog = programFor(effect, src);
        if (!prog) {
            logError(prog.error());
            return std::nullopt; // Hyprland's own windowsIn still plays
        }

        const auto resolved = resolveEffect(src, cfg, durationMs);

        static std::mt19937 rng{std::random_device{}()};

        auto                e = makeShared<SEffect>();
        e->id                 = m_nextId++;
        e->forOpening         = true;
        e->window             = w;
        e->monitor            = mon;
        e->workspace          = w->m_workspace;
        e->program            = *prog;
        e->params             = resolved.params;
        e->padding            = resolved.padding;
        e->seed               = std::uniform_real_distribution<float>{0.F, 1.F}(rng);
        e->start              = Time::steadyNow();
        e->durationS          = resolved.durationMs / 1000.F;
        e->box                = e->liveBox().value_or(CBox{});

        auto transformer = makeUnique<COpenTransformer>(e);
        e->transformer   = transformer.get();
        w->m_transformers.emplace_back(std::move(transformer));

        const auto ID = e->id;
        e->endTimer   = addTimer(std::chrono::milliseconds{resolved.durationMs + 30}, [ID] { effects().finish(ID); });

        m_effects.push_back(e);
        g_pHyprRenderer->damageBox(e->box);
        ensureTicking();

        log(std::format("open effect {} '{}' started for {} ms", ID, src.name, resolved.durationMs));
        return ID;
    }

    void CEffectManager::cancelOpen(PHLWINDOW w) {
        std::vector<uint64_t> ids;
        for (const auto& e : m_effects) {
            if (e->forOpening && e->window.lock() == w)
                ids.push_back(e->id);
        }
        for (auto id : ids)
            finish(id);
    }

    int CEffectManager::durationMsFor(uint64_t id) const {
        for (const auto& e : m_effects) {
            if (e->id == id)
                return static_cast<int>(e->durationS * 1000.F);
        }
        return 0;
    }

    void CEffectManager::finish(uint64_t id) {
        auto it = std::ranges::find(m_effects, id, &SEffect::id);
        if (it == m_effects.end())
            return;

        auto e = *it; // release with GL current
        m_effects.erase(it);
        cancelTimer(e->endTimer);
        recordStats(*e);
        makeGLCurrent();
        detach(*e);
        e.reset();
    }

    void CEffectManager::recordStats(const SEffect& e) {
        const auto ms = e.elapsed() * 1000.F;
        m_history.push_back(std::format("{} '{}': {} frames in {:.0f} ms ({:.0f} fps) on {}", e.forOpening ? "open" : "close", e.program ? e.program->name : "?", e.frames, ms,
                                        ms > 0 ? e.frames * 1000.F / ms : 0.F, e.monitor ? e.monitor->m_name : "?"));
        if (m_history.size() > 6)
            m_history.erase(m_history.begin());
    }

    // Damages the last area and drops our transformer: its code lives in this .so
    void CEffectManager::detach(SEffect& e) {
        g_pHyprRenderer->damageBox(e.box);

        if (!e.transformer)
            return;

        if (const auto w = e.window.lock()) {
            if (const auto live = e.liveBox())
                g_pHyprRenderer->damageBox(*live);
            std::erase_if(w->m_transformers, [&e](const auto& t) { return t.get() == e.transformer; });
        }
        e.transformer = nullptr;
    }

    // Like Hyprland's animation tick: damage from a timer, outside rendering, so frames keep coming
    void CEffectManager::ensureTicking() {
        if (!m_tick && !m_effects.empty())
            m_tick = addTimer(std::chrono::milliseconds{2}, [] { effects().onTick(); });
    }

    void CEffectManager::onTick() {
        m_tick.reset();

        std::vector<uint64_t>   orphaned;
        std::vector<PHLMONITOR> monitors;
        for (const auto& e : m_effects) {
            const auto mon = e->monitor.lock();
            if (!mon) {
                orphaned.push_back(e->id);
                continue;
            }

            if (e->forOpening) {
                // follows the window; old area too, in case it moved
                const auto live = e->liveBox();
                if (!live) {
                    orphaned.push_back(e->id);
                    continue;
                }
                g_pHyprRenderer->damageBox(e->box);
                e->box = *live;
            }

            g_pHyprRenderer->damageBox(e->box);
            if (std::ranges::find(monitors, mon) == monitors.end())
                monitors.push_back(mon);
        }

        for (const auto& m : monitors)
            m->scheduleFrame(Aquamarine::IOutput::AQ_SCHEDULE_ANIMATION);

        for (auto id : orphaned)
            finish(id);

        ensureTicking();
    }

    void CEffectManager::addPassElements(PHLMONITOR monitor, Desktop::eFadeoutPlane plane, PHLWORKSPACE workspace) {
        for (const auto& e : m_effects) {
            if (e->forOpening || e->monitor != monitor || e->plane != plane)
                continue;
            // same filter as IHyprRenderer::renderFadeouts
            if (const auto ws = e->workspace.lock(); ws && ws != workspace)
                continue;

            g_pHyprRenderer->m_renderPass.add(makeUnique<CEffectPassElement>(e));
        }
    }

    void CEffectManager::addPassElementsFallback(PHLMONITOR monitor) {
        for (const auto& e : m_effects) {
            if (e->forOpening || e->monitor != monitor)
                continue;
            if (const auto ws = e->workspace.lock(); ws && !ws->isVisible())
                continue;

            g_pHyprRenderer->addPassElement(makeUnique<CEffectPassElement>(e));
        }
    }

    void CEffectManager::onMonitorRemoved(PHLMONITOR monitor) {
        std::vector<uint64_t> ids;
        for (const auto& e : m_effects) {
            if (!e->monitor || e->monitor == monitor)
                ids.push_back(e->id);
        }
        for (auto id : ids)
            finish(id);
    }

    void CEffectManager::clear() {
        cancelTimer(m_tick);
        for (auto& e : m_effects)
            cancelTimer(e->endTimer);

        makeGLCurrent();
        for (const auto& e : m_effects)
            detach(*e);
        m_effects.clear();
    }

    size_t CEffectManager::count() const {
        return m_effects.size();
    }

    std::string CEffectManager::history() const {
        std::string out;
        for (const auto& h : m_history)
            out += "  " + h + "\n";
        return out;
    }

    std::string CEffectManager::describe() const {
        std::string out;
        for (const auto& e : m_effects) {
            out += std::format("  {} effect {} '{}' {:.0f}% on {}\n", e->forOpening ? "open" : "close", e->id, e->program ? e->program->name : "?",
                               e->progress() * 100.F, e->monitor ? e->monitor->m_name : "?");
        }
        return out;
    }

    void CEffectManager::precompile() {
        auto names = configuredEffects(state().config);
        names.insert("fade");

        makeGLCurrent();
        for (const auto& n : names) {
            SEffectSource src;
            if (auto prog = programFor(n, src); !prog) {
                logError(prog.error());
                notify(std::format("effect '{}' failed to compile (run: hypr-window-anim check)", n), true);
            }
        }
    }

    std::expected<void, std::string> CEffectManager::check(const std::string& name) {
        auto src = loadEffect(name);
        if (!src)
            return std::unexpected(src.error());

        makeGLCurrent();
        auto prog = shaderCache().compile(*src);
        if (!prog)
            return std::unexpected(prog.error());

        auto p = *prog;
        makeGLCurrent();
        p.reset();
        return {};
    }
}
