#include "EffectPassElement.hpp"
#include "Plugin.hpp"

#include <hyprland/src/helpers/math/Math.hpp>
#include <hyprland/src/output/Monitor.hpp>
#include <hyprland/src/render/OpenGL.hpp>
#include <hyprland/src/render/Renderer.hpp>

namespace hwa::plugin {
    namespace {
        // Maps effect coords (0..1 over box) into a texture covering texBox
        std::array<float, 4> uvRect(const CBox& box, const CBox& texBox) {
            return {float((box.x - texBox.x) / texBox.w), float((box.y - texBox.y) / texBox.h), float(box.w / texBox.w), float(box.h / texBox.h)};
        }
    }

    // Mirrors CHyprOpenGLImpl::renderTextureInternal
    void drawEffectQuad(Render::CRenderContext& ctx, SEffect& e, SP<Render::ITexture> tex, const CBox& box, const CBox& texBox, bool applyRenderModif, const SBlurDraw& blur) {
        auto&       rd  = ctx.m_data;
        const auto  mon = rd.pMonitor.lock();
        const auto& gl  = Render::GL::g_pHyprOpenGL;

        if (!e.program || !e.program->shader || !tex || !tex->ok() || !mon || !gl)
            return;

        if (box.w <= 0 || box.h <= 0 || texBox.w <= 0 || texBox.h <= 0)
            return;

        CBox drawBox = box;
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

        const auto MATRIX = g_pHyprRenderer->projectBoxToTarget(ctx, drawBox, Math::invertTransform(tex->m_transform));

        auto       shader = gl->useShader(e.program->shader).lock();
        shader->setUniformMatrix3fv(SHADER_PROJ, 1, GL_TRUE, MATRIX.getMatrix());

        // Hyprland shadows the active unit; keep its cache in sync
        gl->setActiveTexture(GL_TEXTURE0);
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

        const auto EFFECT_UV = uvRect(box, texBox);
        set("uTexture", [](GLint l) { glUniform1i(l, 0); });
        set("uProgress", [&](GLint l) { glUniform1f(l, e.progress()); });
        set("uDuration", [&](GLint l) { glUniform1f(l, e.durationS); });
        set("uTime", [&](GLint l) { glUniform1f(l, e.elapsed()); });
        set("uSeed", [&](GLint l) { glUniform1f(l, e.seed); });
        set("uPadding", [&](GLint l) { glUniform1f(l, e.paddingLogical * mon->m_scale); });
        set("uMonitorScale", [&](GLint l) { glUniform1f(l, mon->m_scale); });
        set("uSize", [&](GLint l) { glUniform2f(l, box.w, box.h); });
        set("uEffectRect", [&](GLint l) { glUniform4f(l, EFFECT_UV[0], EFFECT_UV[1], EFFECT_UV[2], EFFECT_UV[3]); });
        set("uForOpening", [&](GLint l) { glUniform1i(l, e.forOpening ? 1 : 0); });
        set("uIsFullscreen", [&](GLint l) { glUniform1i(l, e.fullscreen ? 1 : 0); });

        const auto& blurTex = blur.src.tex;
        set("hwaBlur", [&](GLint l) { glUniform1i(l, blurTex ? 1 : 0); });
        if (blurTex) {
            gl->setActiveTexture(GL_TEXTURE1);
            blurTex->bind();
            gl->setActiveTexture(GL_TEXTURE0);
            const auto BLUR_UV = uvRect(box, blur.texBox);
            set("hwaBlurTex", [](GLint l) { glUniform1i(l, 1); });
            set("hwaBlurUV", [&](GLint l) { glUniform4f(l, BLUR_UV[0], BLUR_UV[1], BLUR_UV[2], BLUR_UV[3]); });
            set("hwaBlurRect", [&](GLint l) { glUniform4f(l, blur.rect.x, blur.rect.y, blur.rect.w, blur.rect.h); });
            set("hwaBlurRound", [&](GLint l) { glUniform2f(l, blur.radius, blur.power); });
            set("hwaBlurAlpha", [&](GLint l) { glUniform1f(l, blur.alpha); });
        }

        for (const auto& [name, value] : e.params)
            P.setParam(name, value);

        gl->blend(true);
        glBindVertexArray(shader->getUniformLocation(SHADER_SHADER_VAO));

        damage.forEachRect([&](const auto& RECT) {
            gl->scissor(ctx, &RECT, rd.transformDamage);
            glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        });

        gl->disableScissor();
        glBindVertexArray(0);
        tex->unbind();
        if (blurTex) {
            gl->setActiveTexture(GL_TEXTURE1);
            blurTex->unbind();
            gl->setActiveTexture(GL_TEXTURE0);
        }
    }

    CEffectPassElement::CEffectPassElement(SP<SEffect> effect) : m_effect(std::move(effect)) {}

    std::vector<UP<IPassElement>> CEffectPassElement::draw(Render::CRenderContext& ctx) {
        guarded("draw", [&] {
            const auto mon = ctx.m_data.pMonitor.lock();
            if (!m_effect || !m_effect->snapshot || !mon || ctx.m_renderingSnapshot)
                return;

            const auto tex = m_effect->snapshot->getTexture();
            if (!tex)
                return;

            // snapshots and the frame share monitor-local px since 0.57
            const CBox texBox = {{}, tex->m_size};
            const CBox local  = m_effect->box.copy().translate(-mon->m_position).scale(mon->m_scale);

            // the snapshot has no blur (Hyprland skips it for snapshots): add it back
            auto blurSrc = blurSourceFor(ctx, m_effect->blur, mon, ctx.m_data.currentFB);
            drawEffectQuad(ctx, *m_effect, tex, local, texBox, true, blurDrawFor(m_effect->blur, std::move(blurSrc), CBox{{}, mon->m_transformedSize}, m_effect->box, mon->m_scale));
        });
        return {};
    }

    bool CEffectPassElement::needsLiveBlur(Render::CRenderContext& ctx) {
        return false;
    }

    bool CEffectPassElement::needsPrecomputeBlur(Render::CRenderContext& ctx) {
        return false;
    }

    const char* CEffectPassElement::passName() {
        return EFFECT_PASS_NAME;
    }

    ePassElementType CEffectPassElement::type() {
        return EK_CUSTOM;
    }

    std::optional<CBox> CEffectPassElement::boundingBox(Render::CRenderContext& ctx) {
        return std::nullopt; // slides move us; draw() clips itself
    }

    CRegion CEffectPassElement::opaqueRegion(Render::CRenderContext& ctx) {
        return {};
    }
}
