#pragma once

#include "EffectManager.hpp"

#include <hyprland/src/render/transformer/Transformer.hpp>

namespace hwa::plugin {
    // Runs the window's live main pass through the effect shader while it opens
    class COpenTransformer : public Render::IWindowTransformer {
      public:
        explicit COpenTransformer(SP<SEffect> effect);
        virtual ~COpenTransformer() = default;

        virtual Render::SWindowTransformBuffer transform(Render::CRenderContext& ctx, const Render::SWindowTransformBuffer& in,
                                                         const Render::SWindowTransformContext& context) override;
        virtual bool                           active() const override;
        virtual CBox                           transformedExtents(const CBox& currentBox) const override;
        virtual void                           preWindowRender(Render::CRenderContext& ctx, CSurfacePassElement::SRenderData* pRenderData) override;

        // Hyprland's list owns us; inactive ones are dropped on removeInactive()
        void deactivate();

      private:
        SP<SEffect>              m_effect;
        bool                     m_active = true;
        WP<Render::IFramebuffer> m_behind; // the frame's FB: what's under the window, for live blur
    };

    // window.openLate: replaces Hyprland's windowsIn/fadeIn with our effect
    void animateOpen(PHLWINDOW w);
}
