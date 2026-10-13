#pragma once

#include <hyprland/src/desktop/DesktopTypes.hpp>
#include <hyprland/src/desktop/state/Fadeout.hpp>

#include <optional>
#include <string>
#include <vector>

class CEventLoopTimer;

namespace hwa::plugin {
    struct SPendingClose {
        PHLWINDOWREF        window;
        bool                closeSent = false;
        bool                hadFocus  = false;
        bool                preview   = false; // restore instead of closing
        SP<CEventLoopTimer> timer;
    };

    class CCloseController {
      public:
        // true: we send the real close later
        bool        onCloseRequest(PHLWINDOW w, std::optional<std::string> effectOverride = std::nullopt);
        // true: suppress the built-in fade-out
        bool        onFadeoutCreate(PHLWINDOW w, SP<Render::IFramebuffer> snapshot);
        bool        preview(PHLWINDOW w, std::optional<std::string> effect);

        void        onWindowOpen(PHLWINDOW w);
        void        onRenderPre();
        void        onMonitorRemoved(PHLMONITOR monitor);
        // Unload: unhide held windows, send pending closes
        void        flushAll();

        bool        isPending(PHLWINDOW w) const;
        size_t      count() const;
        std::string describe() const;

      private:
        bool                    hold(PHLWINDOW w, std::optional<std::string> effect, bool preview);
        void                    onEffectEnd(SP<SPendingClose> p);
        void                    revert(SP<SPendingClose> p, bool refocus);
        void                    refocusAway(PHLWINDOW held);
        PHLWINDOW               pickCandidate(PHLWINDOW held) const;
        SP<SPendingClose>       find(PHLWINDOW w) const;
        void                    erase(SP<SPendingClose> p);

        std::vector<SP<SPendingClose>> m_pending;
        std::vector<std::pair<PHLWINDOWREF, Time::steady_tp>> m_skipFadeout; // picked "none" at close request
    };

    CCloseController& closer();

    Desktop::eFadeoutPlane planeFor(PHLWINDOW w);
}
