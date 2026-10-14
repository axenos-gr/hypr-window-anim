#include "EffectPassElement.hpp"
#include "Plugin.hpp"

#include <hyprland/src/helpers/math/Math.hpp>
#include <hyprland/src/output/Monitor.hpp>
#include <hyprland/src/render/OpenGL.hpp>
#include <hyprland/src/render/Renderer.hpp>

namespace hwa::plugin {
    // Mirrors CHyprOpenGLImpl::renderTextureInternal
    void drawEffectQuad(SEffect& e, SP<Render::ITexture> tex, const CBox& box, bool flipEndFrame, bool applyRenderModif, const SBlurDraw& blur) {
        auto&       rd  = g_pHyprRenderer->m_renderData;
        const auto  mon = rd.pMonitor.lock();
        const auto& gl  = Render::GL::g_pHyprOpenGL;

        if (!e.program || !e.program->shader || !tex || !tex->ok() || !mon || !gl)
            return;

        const auto PX = mon->m_pixelSize;
        if (PX.x <= 0 || PX.y <= 0 || box.w <= 0 || box.h <= 0)
            return;

        const auto INVERTED = Math::wlTransformToHyprutils(Math::invertTransform(mon->m_transform));
        const CBox texBox   = flipEndFrame ? box.copy().transform(INVERTED, mon->m_transformedSize.x, mon->m_transformedSize.y) : box;

        CBox       drawBox = box;
        if (applyRenderModif)
            rd.renderModif.applyToBox(drawBox);

        CRegion damage = rd.damage.copy().intersect(drawBox.x, drawBox.y, drawBox.w, drawBox.h);
        if (damage.empty())
            return;

        // count frames, not draws: planes and blur mattes draw several times per frame
        const auto NOW = Time::steadyNow();
        if (e.frames == 0 || NOW - e.lastFrame > std::chrono::milliseconds{1}) {
            e.frames++;
            e.lastFrame = NOW;
        }

        g_pHyprRenderer->pushMonitorTransformEnabled(flipEndFrame);

        const auto TRANSFORM = flipEndFrame ? Math::composeTransform(INVERTED, tex->m_transform) : tex->m_transform;
        const auto MATRIX    = g_pHyprRenderer->projectBoxToTarget(drawBox, TRANSFORM);

        auto       shader = gl->useShader(e.program->shader).lock();
        shader->setUniformMatrix3fv(SHADER_PROJ, 1, GL_TRUE, MATRIX.getMatrix());

        glActiveTexture(GL_TEXTURE0);
        tex->bind();
        tex->setTexParameter(GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        tex->setTexParameter(GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        tex->setTexParameter(GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        tex->setTexParameter(GL_TEXTURE_MIN_FILTER, GL_LINEAR);

        const auto& P   = *e.program;
        auto        set = [&P](const char* name, auto&& apply) {
            if (const auto loc = P.location(name); loc != -1)
                apply(loc);
        };

        set("uTexture", [](GLint l) { glUniform1i(l, 0); });
        set("uProgress", [&](GLint l) { glUniform1f(l, e.progress()); });
        set("uDuration", [&](GLint l) { glUniform1f(l, e.durationS); });
        set("uTime", [&](GLint l) { glUniform1f(l, e.elapsed()); });
        set("uSeed", [&](GLint l) { glUniform1f(l, e.seed); });
        set("uPadding", [&](GLint l) { glUniform1f(l, e.paddingLogical * mon->m_scale); });
        set("uMonitorScale", [&](GLint l) { glUniform1f(l, mon->m_scale); });
        set("uSize", [&](GLint l) { glUniform2f(l, box.w, box.h); });
        set("uEffectRect", [&](GLint l) { glUniform4f(l, texBox.x / PX.x, texBox.y / PX.y, texBox.w / PX.x, texBox.h / PX.y); });
        set("uForOpening", [&](GLint l) { glUniform1i(l, e.forOpening ? 1 : 0); });
        set("uIsFullscreen", [&](GLint l) { glUniform1i(l, e.fullscreen ? 1 : 0); });

        set("hwaBlur", [&](GLint l) { glUniform1i(l, blur.tex ? 1 : 0); });
        if (blur.tex) {
            glActiveTexture(GL_TEXTURE1);
            blur.tex->bind();
            glActiveTexture(GL_TEXTURE0);
            set("hwaBlurTex", [](GLint l) { glUniform1i(l, 1); });
            set("hwaBlurRect", [&](GLint l) { glUniform4f(l, blur.rect.x, blur.rect.y, blur.rect.w, blur.rect.h); });
            set("hwaBlurRound", [&](GLint l) { glUniform2f(l, blur.radius, blur.power); });
            set("hwaBlurAlpha", [&](GLint l) { glUniform1f(l, blur.alpha); });
        }

        for (const auto& [name, value] : e.params)
            P.setParam(name, value);

        gl->blend(true);
        glBindVertexArray(shader->getUniformLocation(SHADER_SHADER_VAO));

        damage.forEachRect([&](const auto& RECT) {
            gl->scissor(&RECT, rd.transformDamage);
            glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        });

        gl->scissor(nullptr);
        glBindVertexArray(0);
        tex->unbind();
        if (blur.tex) {
            glActiveTexture(GL_TEXTURE1);
            blur.tex->unbind();
            glActiveTexture(GL_TEXTURE0);
        }

        g_pHyprRenderer->popMonitorTransformEnabled();
    }

    CEffectPassElement::CEffectPassElement(SP<SEffect> effect) : m_effect(std::move(effect)) {}

    std::vector<UP<IPassElement>> CEffectPassElement::draw() {
        guarded("draw", [this] {
            const auto mon = g_pHyprRenderer->m_renderData.pMonitor.lock();
            if (!m_effect || !m_effect->snapshot || !mon || g_pHyprRenderer->m_bRenderingSnapshot)
                return;

            // the snapshot has no blur (Hyprland skips it for snapshots): add it back
            auto       blurTex = blurTextureFor(m_effect->blur, mon, g_pHyprRenderer->m_renderData.currentFB);
            const CBox local   = m_effect->box.copy().translate(-mon->m_position).scale(mon->m_scale);
            drawEffectQuad(*m_effect, m_effect->snapshot->getTexture(), local, true, true, blurDrawFor(m_effect->blur, std::move(blurTex), m_effect->box, mon->m_scale));
        });
        return {};
    }

    bool CEffectPassElement::needsLiveBlur() {
        return false;
    }

    bool CEffectPassElement::needsPrecomputeBlur() {
        return false;
    }

    const char* CEffectPassElement::passName() {
        return EFFECT_PASS_NAME;
    }

    ePassElementType CEffectPassElement::type() {
        return EK_CUSTOM;
    }

    std::optional<CBox> CEffectPassElement::boundingBox() {
        return std::nullopt; // slides move us; draw() clips itself
    }

    CRegion CEffectPassElement::opaqueRegion() {
        return {};
    }
}
