#include "Hooks.hpp"
#include "CloseController.hpp"
#include "EffectManager.hpp"
#include "Plugin.hpp"

#include <hyprland/src/desktop/state/WindowFadeout.hpp>
#include <hyprland/src/desktop/view/Window.hpp>
#include <hyprland/src/managers/XWaylandManager.hpp>
#include <hyprland/src/render/Renderer.hpp>

#include <format>

namespace hwa::plugin::hooks {
    namespace {
        using FnSendClose      = void (*)(CHyprXWaylandManager*, PHLWINDOW);
        using FnFadeoutCreate  = SP<Desktop::CWindowFadeout> (*)(PHLWINDOW, SP<Render::IFramebuffer>, float);
        using FnRenderFadeouts = void (*)(Render::IHyprRenderer*, PHLMONITOR, Desktop::eFadeoutPlane, PHLWORKSPACE);

        CFunctionHook* g_sendClose      = nullptr;
        CFunctionHook* g_fadeoutCreate  = nullptr;
        CFunctionHook* g_renderFadeouts = nullptr;

        void hkSendCloseWindow(CHyprXWaylandManager* self, PHLWINDOW w) {
            bool handled = false;
            if (!g_unloading)
                guarded("sendCloseWindow", [&] { handled = closer().onCloseRequest(w); });

            if (!handled)
                ((FnSendClose)g_sendClose->m_original)(self, w);
        }

        SP<Desktop::CWindowFadeout> hkFadeoutCreate(PHLWINDOW w, SP<Render::IFramebuffer> snapshot, float alpha) {
            bool suppress = false;
            if (!g_unloading)
                guarded("CWindowFadeout::create", [&] { suppress = closer().onFadeoutCreate(w, snapshot); });

            // fadingOutState()->add() ignores nullptr
            if (suppress)
                return nullptr;
            return ((FnFadeoutCreate)g_fadeoutCreate->m_original)(w, snapshot, alpha);
        }

        void hkRenderFadeouts(Render::IHyprRenderer* self, PHLMONITOR monitor, Desktop::eFadeoutPlane plane, PHLWORKSPACE workspace) {
            ((FnRenderFadeouts)g_renderFadeouts->m_original)(self, monitor, plane, workspace);

            if (!g_unloading && monitor)
                guarded("renderFadeouts", [&] { effects().addPassElements(monitor, plane, workspace); });
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
        g_fadeoutCreate = hookExact("_ZN7Desktop14CWindowFadeout6createEN9Hyprutils6Memory14CSharedPointerINS_4View7CWindowEEENS3_IN6Render12IFramebufferEEEf",
                                    (void*)&hkFadeoutCreate, error);
        if (!g_fadeoutCreate)
            return false;

        g_renderFadeouts = hookExact(
            "_ZN6Render13IHyprRenderer14renderFadeoutsEN9Hyprutils6Memory14CSharedPointerIN7Monitor8CMonitorEEEN7Desktop13eFadeoutPlaneENS3_I10CWorkspaceEE",
            (void*)&hkRenderFadeouts, error);
        if (!g_renderFadeouts) {
            logError(error + "; drawing effects after all windows instead");
            error.clear();
        }

        g_sendClose = hookExact("_ZN20CHyprXWaylandManager15sendCloseWindowEN9Hyprutils6Memory14CSharedPointerIN7Desktop4View7CWindowEEE", (void*)&hkSendCloseWindow,
                                error);
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
        if (g_sendClose)
            ((FnSendClose)g_sendClose->m_original)(g_pXWaylandManager.get(), w);
        else
            g_pXWaylandManager->sendCloseWindow(w);
    }
}
