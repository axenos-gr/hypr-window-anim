#pragma once

#include "EffectManager.hpp"

#include <hyprland/src/render/transformer/Transformer.hpp>

#include <utility>
#include <vector>

namespace hwa::plugin {
    // Runs the window's live main pass through the effect shader while it opens
    class COpenTransformer : public Render::IWindowTransformer {
      public:
        explicit COpenTransformer(SP<SEffect> effect);
        virtual ~COpenTransformer();

        virtual SP<Render::IFramebuffer> transform(SP<Render::IFramebuffer> in) override;

      private:
        SP<Render::IFramebuffer> outFor(const SP<Render::IFramebuffer>& in);

        SP<SEffect>              m_effect;
        // keyed by input: Hyprland calls us twice per frame for blurred windows (content + matte)
        std::vector<std::pair<Render::IFramebuffer*, SP<Render::IFramebuffer>>> m_outs;
    };

    // window.openLate: replaces Hyprland's windowsIn/fadeIn with our effect
    void animateOpen(PHLWINDOW w);
}
