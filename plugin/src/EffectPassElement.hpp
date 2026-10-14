#pragma once

#include "EffectManager.hpp"

#include <hyprland/src/render/Texture.hpp>
#include <hyprland/src/render/pass/PassElement.hpp>

namespace hwa::plugin {
    inline constexpr const char* EFFECT_PASS_NAME = "CHwaEffectPassElement";

    // box: monitor-local px; flipEndFrame for snapshot textures
    void drawEffectQuad(SEffect& e, SP<Render::ITexture> tex, const CBox& box, bool flipEndFrame, bool applyRenderModif, const SBlurDraw& blur);

    class CEffectPassElement : public IPassElement {
      public:
        explicit CEffectPassElement(SP<SEffect> effect);
        virtual ~CEffectPassElement() = default;

        virtual std::vector<UP<IPassElement>> draw() override;
        virtual bool                          needsLiveBlur() override;
        virtual bool                          needsPrecomputeBlur() override;
        virtual const char*                   passName() override;
        virtual ePassElementType              type() override;
        virtual std::optional<CBox>           boundingBox() override;
        virtual CRegion                       opaqueRegion() override;

      private:
        SP<SEffect> m_effect;
    };
}
