#pragma once

#include "ShaderCache.hpp"

#include <hyprland/src/desktop/DesktopTypes.hpp>
#include <hyprland/src/desktop/state/Fadeout.hpp>
#include <hyprland/src/helpers/time/Time.hpp>
#include <hyprland/src/render/Framebuffer.hpp>

#include <optional>
#include <string>
#include <vector>

class CEventLoopTimer;
namespace Render {
    class IWindowTransformer;
}

namespace hwa::plugin {
    struct SEffect {
        uint64_t                 id = 0;
        PHLMONITORREF            monitor;
        PHLWORKSPACEREF          workspace;
        Desktop::eFadeoutPlane   plane = Desktop::FADEOUT_PLANE_WINDOW_TILED;
        SP<Render::IFramebuffer> snapshot;
        SP<SProgram>             program;
        ParamMap                 params;
        CBox                     box; // global logical, padded
        double                   paddingLogical = 0.0;
        bool                     fullscreen     = false;
        float                    seed           = 0.F;
        Time::steady_tp          start;
        float                    durationS = 0.6F;
        SP<CEventLoopTimer>      endTimer;

        // opening: live content via a window transformer instead of a snapshot
        bool                         forOpening = false;
        PHLWINDOWREF                 window;
        Render::IWindowTransformer*  transformer = nullptr;
        double                       padding     = 0.0;

        uint32_t                 frames = 0; // diagnostics
        Time::steady_tp          lastFrame;

        float                    progress() const;
        float                    elapsed() const;
        std::optional<CBox>      liveBox() const; // opening only, global logical
    };

    struct SEffectRequest {
        PHLWINDOW                window; // may already be unmapped
        SP<Render::IFramebuffer> snapshot;
        Desktop::eFadeoutPlane   plane = Desktop::FADEOUT_PLANE_WINDOW_TILED;
        std::string              effect;
        std::optional<int>       durationMs;
    };

    class CEffectManager {
      public:
        // nullopt: caller falls back to Hyprland
        std::optional<uint64_t> start(const SEffectRequest& req);
        std::optional<uint64_t> startOpen(PHLWINDOW w, const std::string& effect, std::optional<int> durationMs);
        void                    cancelOpen(PHLWINDOW w);
        int                     durationMsFor(uint64_t id) const;

        void                    addPassElements(PHLMONITOR monitor, Desktop::eFadeoutPlane plane, PHLWORKSPACE workspace);
        void                    addPassElementsFallback(PHLMONITOR monitor);
        void                    onMonitorRemoved(PHLMONITOR monitor);
        void                    clear();

        size_t                  count() const;
        std::string             describe() const;
        std::string             history() const;

        // Avoids a stutter on first close
        void                    precompile();
        std::expected<void, std::string> check(const std::string& name);

      private:
        void                       finish(uint64_t id);
        void                       detach(SEffect& e);
        void                       ensureTicking();
        void                       onTick();
        void                       recordStats(const SEffect& e);
        std::expected<SP<SProgram>, std::string> programFor(const std::string& name, SEffectSource& outSrc);

        std::vector<SP<SEffect>> m_effects;
        SP<CEventLoopTimer>      m_tick;
        std::vector<std::string> m_history;
        uint64_t                 m_nextId = 1;
    };

    CEffectManager& effects();
}
