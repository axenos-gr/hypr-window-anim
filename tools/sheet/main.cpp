// Offline effect renderer: contact sheets and GPU timings without a compositor.
#include "hwa/Config.hpp"
#include "hwa/Effect.hpp"
#include "hwa/ShaderAssembly.hpp"

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>
#include <png.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <print>
#include <random>
#include <string>
#include <vector>

namespace {
    constexpr std::string_view USAGE = R"(usage: hwa-sheet <effect> [options]

  --out PATH          contact sheet PNG (default: <effect>.png)
  --image PATH        window image (default: synthetic window)
  --size WxH          synthetic window size (default 900x560)
  --frames N          frames on the sheet (default 8)
  --cols N            sheet columns (default 4)
  --progress a,b,...  explicit progress values
  --open              render with uForOpening
  --scale S           monitor scale (default 1)
  --seed S            uSeed (default 0.37)
  --bench             time the effect on a 2560x1440 window instead
  --verify            check that the ends match the window (open) and nothing (close)

Effects load like the plugin's: $HWA_CONFIG_DIR/effects, then built-ins.
)";

    struct SImage {
        int                  w = 0, h = 0;
        std::vector<uint8_t> px; // RGBA8, premultiplied
    };

    bool readPng(const std::string& path, SImage& out) {
        png_image img{};
        img.version = PNG_IMAGE_VERSION;
        if (!png_image_begin_read_from_file(&img, path.c_str()))
            return false;
        img.format = PNG_FORMAT_RGBA;
        out.w      = img.width;
        out.h      = img.height;
        out.px.resize(PNG_IMAGE_SIZE(img));
        if (!png_image_finish_read(&img, nullptr, out.px.data(), 0, nullptr))
            return false;
        for (size_t i = 0; i < out.px.size(); i += 4) {
            for (int c = 0; c < 3; ++c)
                out.px[i + c] = out.px[i + c] * out.px[i + 3] / 255;
        }
        return true;
    }

    bool writePng(const std::string& path, const SImage& img) {
        png_image p{};
        p.version = PNG_IMAGE_VERSION;
        p.width   = img.w;
        p.height  = img.h;
        p.format  = PNG_FORMAT_RGBA;
        return png_image_write_to_file(&p, path.c_str(), 0, img.px.data(), 0, nullptr);
    }

    // Terminal-ish window: border, header, coloured "code" lines
    SImage syntheticWindow(int w, int h) {
        SImage img{w, h, std::vector<uint8_t>(size_t(w) * h * 4)};
        std::mt19937 rng{7};
        const float  R = 10.F;

        std::vector<int> lineLen(h / 18 + 1);
        for (auto& l : lineLen)
            l = std::uniform_int_distribution<int>{0, w - 60}(rng);
        const uint8_t palette[5][3] = {{220, 223, 228}, {97, 175, 239}, {152, 195, 121}, {229, 192, 123}, {198, 120, 221}};

        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                const float dx = std::max({R - x - 0.5F, x + 0.5F - (w - R), 0.F});
                const float dy = std::max({R - y - 0.5F, y + 0.5F - (h - R), 0.F});
                const float d  = std::sqrt(dx * dx + dy * dy);
                const float a  = std::clamp(R - d + 0.5F, 0.F, 1.F);

                float       r = 30, g = 33, b = 40;
                if (y < 36) {
                    r = 44, g = 48, b = 58;
                    if (std::abs(x - 24) < 7 && std::abs(y - 18) < 7)
                        r = 236, g = 95, b = 87;
                }
                const int line = (y - 44) / 18, iy = (y - 44) % 18;
                if (y >= 44 && iy > 3 && iy < 14 && x > 20 && x < 20 + lineLen[line] && ((x / 9 + line) % 7 != 0)) {
                    const auto* c = palette[(x / 63 + line) % 5];
                    r = c[0], g = c[1], b = c[2];
                }
                if (d > R - 2.5F || x < 2 || y < 2 || x >= w - 2 || y >= h - 2)
                    r = 51, g = 204, b = 255; // border

                auto* p = &img.px[(size_t(y) * w + x) * 4];
                p[0]    = uint8_t(r * a);
                p[1]    = uint8_t(g * a);
                p[2]    = uint8_t(b * a);
                p[3]    = uint8_t(255 * a);
            }
        }
        return img;
    }

    constexpr const char* VERT = R"(#version 300 es
in vec2 pos;
out vec2 v_texcoord;
void main() {
  v_texcoord  = pos;
  gl_Position = vec4(pos * 2.0 - 1.0, 0.0, 1.0);
})";

    GLuint compile(GLenum type, const std::string& src) {
        GLuint s       = glCreateShader(type);
        const char* p  = src.c_str();
        glShaderSource(s, 1, &p, nullptr);
        glCompileShader(s);
        GLint ok = 0;
        glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
        if (!ok) {
            char log[8192];
            glGetShaderInfoLog(s, sizeof(log), nullptr, log);
            std::println(stderr, "{}", log);
            return 0;
        }
        return s;
    }

    void setParam(GLuint prog, const std::string& name, const hwa::ParamValue& v) {
        const GLint loc = glGetUniformLocation(prog, name.c_str());
        if (loc < 0)
            return;

        GLint n = 0;
        glGetProgramiv(prog, GL_ACTIVE_UNIFORMS, &n);
        GLenum type = GL_FLOAT;
        for (GLint i = 0; i < n; ++i) {
            char    buf[128];
            GLint   size;
            GLenum  t;
            glGetActiveUniform(prog, i, sizeof(buf), nullptr, &size, &t, buf);
            if (name == buf)
                type = t;
        }

        std::vector<float> f = std::visit(
            [](const auto& x) -> std::vector<float> {
                using T = std::decay_t<decltype(x)>;
                if constexpr (std::is_same_v<T, std::vector<double>>)
                    return {x.begin(), x.end()};
                else
                    return {static_cast<float>(x)};
            },
            v);
        f.resize(4, f.size() == 1 ? f[0] : 0.F);

        switch (type) {
            case GL_FLOAT: glUniform1f(loc, f[0]); break;
            case GL_FLOAT_VEC2: glUniform2f(loc, f[0], f[1]); break;
            case GL_FLOAT_VEC3: glUniform3f(loc, f[0], f[1], f[2]); break;
            case GL_FLOAT_VEC4: glUniform4f(loc, f[0], f[1], f[2], f[3]); break;
            default: glUniform1i(loc, GLint(f[0])); break;
        }
    }

    bool initEGL() {
        auto getDisplay = reinterpret_cast<PFNEGLGETPLATFORMDISPLAYEXTPROC>(eglGetProcAddress("eglGetPlatformDisplayEXT"));
        EGLDisplay dpy  = getDisplay ? getDisplay(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, nullptr) : EGL_NO_DISPLAY;
        if (dpy == EGL_NO_DISPLAY || !eglInitialize(dpy, nullptr, nullptr) || !eglBindAPI(EGL_OPENGL_ES_API))
            return false;

        EGLConfig    cfg = EGL_NO_CONFIG_KHR; // surfaceless: no window configs

        const EGLint ctxAttrs[] = {EGL_CONTEXT_MAJOR_VERSION, 3, EGL_NONE};
        EGLContext   ctx        = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, ctxAttrs);
        return ctx != EGL_NO_CONTEXT && eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx);
    }

    struct SOpts {
        std::string        effect, out, image;
        int                w = 900, h = 560, frames = 8, cols = 4;
        std::vector<float> progress;
        bool               open = false, bench = false, verify = false;
        float              scale = 1.F, seed = 0.37F;
    };
}

int main(int argc, char** argv) {
    SOpts o;
    for (int i = 1; i < argc; ++i) {
        const std::string a    = argv[i];
        auto              next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
        if (a == "--out")
            o.out = next();
        else if (a == "--image")
            o.image = next();
        else if (a == "--size")
            std::sscanf(next().c_str(), "%dx%d", &o.w, &o.h);
        else if (a == "--frames")
            o.frames = std::max(1, std::stoi(next()));
        else if (a == "--cols")
            o.cols = std::max(1, std::stoi(next()));
        else if (a == "--progress") {
            std::string s = next();
            for (size_t p = 0; p < s.size();) {
                size_t e = s.find(',', p);
                o.progress.push_back(std::stof(s.substr(p, e - p)));
                p = e == std::string::npos ? s.size() : e + 1;
            }
        } else if (a == "--open")
            o.open = true;
        else if (a == "--bench")
            o.bench = true;
        else if (a == "--verify")
            o.verify = true;
        else if (a == "--scale")
            o.scale = std::stof(next());
        else if (a == "--seed")
            o.seed = std::stof(next());
        else if (a == "-h" || a == "--help") {
            std::print("{}", USAGE);
            return 0;
        } else
            o.effect = a;
    }
    if (o.effect.empty()) {
        std::print(stderr, "{}", USAGE);
        return 1;
    }
    if (o.out.empty())
        o.out = o.effect + ".png";
    if (o.progress.empty()) {
        for (int i = 0; i < o.frames; ++i)
            o.progress.push_back(o.frames == 1 ? 0.5F : float(i) / float(o.frames - 1));
    }

    auto src = hwa::loadEffect(o.effect);
    if (!src) {
        std::println(stderr, "{}", src.error());
        return 1;
    }
    const auto eff = hwa::resolveEffect(*src, hwa::SConfig{}, std::nullopt);

    if (!initEGL()) {
        std::println(stderr, "EGL init failed");
        return 1;
    }

    SImage win;
    if (o.bench)
        win = syntheticWindow(2560, 1440);
    else if (!o.image.empty()) {
        if (!readPng(o.image, win)) {
            std::println(stderr, "can't read {}", o.image);
            return 1;
        }
    } else
        win = syntheticWindow(int(o.w * o.scale), int(o.h * o.scale));

    // effect box = window + padding on every side, like the plugin
    const int pad = int(std::round(eff.padding * o.scale));
    const int BW = win.w + 2 * pad, BH = win.h + 2 * pad;

    std::vector<uint8_t> boxPx(size_t(BW) * BH * 4, 0);
    for (int y = 0; y < win.h; ++y)
        std::memcpy(&boxPx[(size_t(y + pad) * BW + pad) * 4], &win.px[size_t(y) * win.w * 4], size_t(win.w) * 4);

    GLuint tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, BW, BH, 0, GL_RGBA, GL_UNSIGNED_BYTE, boxPx.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    GLuint rt, fbo;
    glGenTextures(1, &rt);
    glBindTexture(GL_TEXTURE_2D, rt);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, BW, BH, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, rt, 0);

    const GLuint vs = compile(GL_VERTEX_SHADER, VERT);
    const GLuint fs = compile(GL_FRAGMENT_SHADER, hwa::assembleFragment(eff.source.body));
    if (!vs || !fs)
        return 1;
    const GLuint prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glBindAttribLocation(prog, 0, "pos");
    glLinkProgram(prog);
    GLint linked = 0;
    glGetProgramiv(prog, GL_LINK_STATUS, &linked);
    if (!linked) {
        std::println(stderr, "link failed");
        return 1;
    }
    glUseProgram(prog);

    const float quad[] = {0, 0, 1, 0, 0, 1, 1, 1};
    GLuint      vbo;
    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);

    auto u = [&](const char* n) { return glGetUniformLocation(prog, n); };
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex);
    glUniform1i(u("uTexture"), 0);
    glUniform1f(u("uDuration"), eff.durationMs / 1000.F);
    glUniform1f(u("uSeed"), o.seed);
    glUniform1f(u("uPadding"), float(pad));
    glUniform1f(u("uMonitorScale"), o.scale);
    glUniform2f(u("uSize"), float(BW), float(BH));
    glUniform4f(u("uEffectRect"), 0, 0, 1, 1);
    glUniform1i(u("uForOpening"), o.open);
    glUniform1i(u("uIsFullscreen"), 0);
    for (const auto& [name, value] : eff.params)
        setParam(prog, name, value);

    glViewport(0, 0, BW, BH);
    glDisable(GL_BLEND);

    auto drawAt = [&](float p) {
        glUniform1f(u("uProgress"), p);
        glUniform1f(u("uTime"), p * eff.durationMs / 1000.F);
        glClearColor(0, 0, 0, 0);
        glClear(GL_COLOR_BUFFER_BIT);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    };

    if (o.verify) {
        std::vector<uint8_t> frame(size_t(BW) * BH * 4);
        auto                 maxDiff = [&](float p, bool open, bool vsWindow) {
            glUniform1i(u("uForOpening"), open);
            drawAt(p);
            glReadPixels(0, 0, BW, BH, GL_RGBA, GL_UNSIGNED_BYTE, frame.data());
            int worst = 0;
            for (size_t i = 0; i < frame.size(); ++i)
                worst = std::max(worst, std::abs(int(frame[i]) - (vsWindow ? int(boxPx[i]) : 0)));
            return worst;
        };
        // close starts on the window and ends on nothing; open is the reverse
        const int r[4] = {maxDiff(0.F, false, true), maxDiff(1.F, false, false), maxDiff(0.F, true, false), maxDiff(1.F, true, true)};
        const bool ok  = std::ranges::all_of(r, [](int d) { return d <= 2; });
        std::println("{} {}: close start {} end {}, open start {} end {} (max channel error)", ok ? "ok  " : "FAIL", o.effect, r[0], r[1], r[2], r[3]);
        return ok ? 0 : 1;
    }

    if (o.bench) {
        std::println("{}: {}x{} box ({} padding) on {}", o.effect, BW, BH, pad, reinterpret_cast<const char*>(glGetString(GL_RENDERER)));
        for (int i = 0; i < 60; ++i) // let the GPU clock up
            drawAt(float(i % 10) / 10.F);
        glFinish();

        double total = 0;
        for (float p : {0.1F, 0.3F, 0.5F, 0.7F, 0.9F}) {
            const auto t0 = std::chrono::steady_clock::now();
            for (int i = 0; i < 30; ++i)
                drawAt(p);
            glFinish();
            const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() / 30;
            total += ms;
            std::println("  p={:.1f}: {:.2f} ms", p, ms);
        }
        std::println("  mean {:.2f} ms/frame", total / 5);
        return 0;
    }

    const int N = int(o.progress.size()), cols = std::min(o.cols, N), rows = (N + cols - 1) / cols, GAP = 8;
    SImage    sheet{cols * BW + (cols + 1) * GAP, rows * BH + (rows + 1) * GAP, {}};
    sheet.px.assign(size_t(sheet.w) * sheet.h * 4, 255);
    for (int y = 0; y < sheet.h; ++y) {
        for (int x = 0; x < sheet.w; ++x) {
            const uint8_t v = ((x / 16 + y / 16) % 2) ? 38 : 50; // checker shows alpha
            auto*         p = &sheet.px[(size_t(y) * sheet.w + x) * 4];
            p[0] = p[1] = p[2] = v;
        }
    }

    std::vector<uint8_t> frame(size_t(BW) * BH * 4);
    for (int i = 0; i < N; ++i) {
        drawAt(o.progress[i]);
        glReadPixels(0, 0, BW, BH, GL_RGBA, GL_UNSIGNED_BYTE, frame.data());

        const int ox = GAP + (i % cols) * (BW + GAP), oy = GAP + (i / cols) * (BH + GAP);
        for (int y = 0; y < BH; ++y) {
            for (int x = 0; x < BW; ++x) {
                const auto* s = &frame[(size_t(y) * BW + x) * 4];
                auto*       d = &sheet.px[(size_t(oy + y) * sheet.w + ox + x) * 4];
                for (int c = 0; c < 3; ++c)
                    d[c] = uint8_t(std::min(255, s[c] + d[c] * (255 - s[3]) / 255)); // premultiplied over
            }
        }
    }

    if (!writePng(o.out, sheet)) {
        std::println(stderr, "can't write {}", o.out);
        return 1;
    }
    std::println("{}: {} frames, {}x{} box -> {}", o.effect, N, BW, BH, o.out);
    return 0;
}
