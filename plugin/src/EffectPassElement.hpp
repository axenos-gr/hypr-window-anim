#pragma once

#include "EffectManager.hpp"

#include <hyprland/src/render/Texture.hpp>
#include <hyprland/src/render/pass/PassElement.hpp>

namespace Render {
    class CRenderContext;
}

namespace hwa::plugin {
    inline constexpr const char* EFFECT_PASS_NAME = "CHwaEffectPassElement";

    // Boxes in current target px; texBox/blur.texBox: area each texture covers
    void drawEffectQuad(Render::CRenderContext& ctx, SEffect& e, SP<Render::ITexture> tex, const CBox& box, const CBox& texBox, bool applyRenderModif, const SBlurDraw& blur);

    class CEffectPassElement : public IPassElement {
      public:
        explicit CEffectPassElement(SP<SEffect> effect);
        virtual ~CEffectPassElement() = default;

        virtual std::vector<UP<IPassElement>> draw(Render::CRenderContext& ctx) override;
        virtual bool                          needsLiveBlur(Render::CRenderContext& ctx) override;
        virtual bool                          needsPrecomputeBlur(Render::CRenderContext& ctx) override;
        virtual const char*                   passName() override;
        virtual ePassElementType              type() override;
        virtual std::optional<CBox>           boundingBox(Render::CRenderContext& ctx) override;
        virtual CRegion                       opaqueRegion(Render::CRenderContext& ctx) override;

      private:
        SP<SEffect> m_effect;
    };
}
