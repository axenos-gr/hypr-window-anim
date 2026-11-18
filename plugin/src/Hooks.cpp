#include "Hooks.hpp"
#include "CloseController.hpp"
#include "EffectManager.hpp"
#include "Plugin.hpp"

#include <hyprland/src/desktop/state/WindowFadeout.hpp>
#include <hyprland/src/desktop/view/window/Window.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/render/scene/SceneSelection.hpp>

#include <format>

namespace hwa::plugin::hooks {
    namespace {
        using FnSendClose      = void (*)(Desktop::View::CWindow*);
        using FnFadeoutCreate  = SP<Desktop::CWindowFadeout> (*)(PHLWINDOW, SP<Render::IFramebuffer>, float, SP<Render::IFramebuffer>);
        using FnRenderFadeouts = void (*)(Render::IHyprRenderer*, Render::CRenderContext&, PHLMONITOR, Desktop::eFadeoutPlane, PHLWORKSPACE, Render::eSceneMode);

        CFunctionHook* g_sendClose      = nullptr;
        CFunctionHook* g_fadeoutCreate  = nullptr;
        CFunctionHook* g_renderFadeouts = nullptr;

        void hkSendClose(Desktop::View::CWindow* self) {
            bool handled = false;
            if (!g_unloading && self)
                guarded("sendClose", [&] { handled = closer().onCloseRequest(self->m_self.lock()); });

            if (!handled)
                ((FnSendClose)g_sendClose->m_original)(self);
        }

        SP<Desktop::CWindowFadeout> hkFadeoutCreate(PHLWINDOW w, SP<Render::IFramebuffer> snapshot, float alpha, SP<Render::IFramebuffer> workspaceSnapshot) {
            bool suppress = false;
            if (!g_unloading)
                guarded("CWindowFadeout::create", [&] { suppress = closer().onFadeoutCreate(w, snapshot); });

            // fadingOutState()->add() ignores nullptr
            if (suppress)
                return nullptr;
            return ((FnFadeoutCreate)g_fadeoutCreate->m_original)(w, snapshot, alpha, workspaceSnapshot);
        }

        void hkRenderFadeouts(Render::IHyprRenderer* self, Render::CRenderContext& ctx, PHLMONITOR monitor, Desktop::eFadeoutPlane plane, PHLWORKSPACE workspace,
                              Render::eSceneMode mode) {
            ((FnRenderFadeouts)g_renderFadeouts->m_original)(self, ctx, monitor, plane, workspace, mode);

            // workspace scenes are offscreen captures (screenshare)
            if (!g_unloading && monitor && mode == Render::eSceneMode::MONITOR)
                guarded("renderFadeouts", [&] { effects().addPassElements(ctx, monitor, plane, workspace); });
        }

        // Exact mangled match: never hook a lookalike
        CFunctionHook* hookExact(const std::string& mangled, void* destination, std::string& error) {
            const auto matches = HyprlandAPI::findFunctionsByName(PHANDLE, mangled);

            const SFunctionMatch* found = nullptr;
            for (const auto& m : matches) {
                if (m.signature == mangled) {
                    found = &m;
                    break;
                }
            }

            if (!found) {
                error = std::format("symbol {} not found (Hyprland internals changed?)", mangled);
                return nullptr;
            }

            auto* hook = HyprlandAPI::createFunctionHook(PHANDLE, found->address, destination);
            if (!hook || !hook->hook()) {
                error = std::format("failed to hook {}", found->demangled);
                if (hook)
                    HyprlandAPI::removeFunctionHook(PHANDLE, hook);
                return nullptr;
            }

            log(std::format("hooked {}", found->demangled));
            return hook;
        }
    }

    bool install(std::string& error) {
        // Fade-out hook first: holds rely on it
        g_fadeoutCreate = hookExact("_ZN7Desktop14CWindowFadeout6createEN9Hyprutils6Memory14CSharedPointerINS_4View7CWindowEEENS3_IN6Render12IFramebufferEEEfS9_",
                                    (void*)&hkFadeoutCreate, error);
        if (!g_fadeoutCreate)
            return false;

        // the scene-mode overload: every plane of every frame goes through it
        g_renderFadeouts = hookExact("_ZN6Render13IHyprRenderer14renderFadeoutsERNS_14CRenderContextEN9Hyprutils6Memory14CSharedPointerIN7Monitor8CMonitorEEEN7Desktop13eFadeoutPl"
                                     "aneENS5_IN9Workspace12CHLWorkspaceEEENS_10eSceneModeE",
                                     (void*)&hkRenderFadeouts, error);
        if (!g_renderFadeouts) {
            logError(error + "; drawing effects after all windows instead");
            error.clear();
        }

        g_sendClose = hookExact("_ZN7Desktop4View7CWindow9sendCloseEv", (void*)&hkSendClose, error);
        if (!g_sendClose) {
            logError(error + "; closes will not wait for the effect");
            error.clear();
        }

        return true;
    }

    bool renderHookInstalled() {
        return g_renderFadeouts != nullptr;
    }

    void sendCloseOriginal(PHLWINDOW w) {
        if (!w)
            return;
        if (g_sendClose)
            ((FnSendClose)g_sendClose->m_original)(w.get());
        else
            w->sendClose();
    }
}
