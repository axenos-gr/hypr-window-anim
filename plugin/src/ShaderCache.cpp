#include "ShaderCache.hpp"
#include "Plugin.hpp"

#include "hwa/ShaderAssembly.hpp"

#include <hyprland/src/render/OpenGL.hpp>

#include <format>

namespace hwa::plugin {
    CShaderCache& shaderCache() {
        static CShaderCache cache;
        return cache;
    }

    GLint SProgram::location(const std::string& n) const {
        auto it = uniforms.find(n);
        return it == uniforms.end() ? -1 : it->second.location;
    }

    void SProgram::setParam(const std::string& n, const ParamValue& v) const {
        auto it = uniforms.find(n);
        if (it == uniforms.end())
            return;

        const auto loc = it->second.location;

        auto       asFloats = [&]() -> std::vector<float> {
            return std::visit(
                [](const auto& x) -> std::vector<float> {
                    using T = std::decay_t<decltype(x)>;
                    if constexpr (std::is_same_v<T, std::vector<double>>)
                        return {x.begin(), x.end()};
                    else
                        return {static_cast<float>(x)};
                },
                v);
        };

        auto f = asFloats();
        f.resize(4, f.size() == 1 ? f[0] : 0.F); // scalars broadcast to vectors

        switch (it->second.type) {
            case GL_FLOAT: glUniform1f(loc, f[0]); break;
            case GL_FLOAT_VEC2: glUniform2f(loc, f[0], f[1]); break;
            case GL_FLOAT_VEC3: glUniform3f(loc, f[0], f[1], f[2]); break;
            case GL_FLOAT_VEC4: glUniform4f(loc, f[0], f[1], f[2], f[3]); break;
            case GL_INT:
            case GL_BOOL: glUniform1i(loc, static_cast<GLint>(f[0])); break;
            default: break;
        }
    }

    std::expected<SP<SProgram>, std::string> CShaderCache::compile(const SEffectSource& src) {
        if (!Render::GL::g_pHyprOpenGL || !Render::GL::g_pHyprOpenGL->m_shaders)
            return std::unexpected("OpenGL renderer is not ready");

        const auto frag = assembleFragment(src.body);

        // CShader logs errors only to hyprland.log
        GLuint      fs   = glCreateShader(GL_FRAGMENT_SHADER);
        const char* text = frag.c_str();
        glShaderSource(fs, 1, &text, nullptr);
        glCompileShader(fs);
        GLint ok = GL_FALSE;
        glGetShaderiv(fs, GL_COMPILE_STATUS, &ok);
        if (ok != GL_TRUE) {
            GLint len = 0;
            glGetShaderiv(fs, GL_INFO_LOG_LENGTH, &len);
            std::string log(std::max(len, 1), '\0');
            glGetShaderInfoLog(fs, len, nullptr, log.data());
            glDeleteShader(fs);
            while (!log.empty() && (log.back() == '\0' || log.back() == '\n'))
                log.pop_back();
            return std::unexpected(std::format("{} (source 1 = effect file, 2 = prelude, 3 = bmw-common):\n{}", src.name, log));
        }
        glDeleteShader(fs);

        auto prog    = makeShared<SProgram>();
        prog->name   = src.name;
        prog->shader = makeShared<CShader>();
        // dynamic: no RASSERT on failure; silent: no error bar
        if (!prog->shader->createProgram(Render::GL::g_pHyprOpenGL->m_shaders->TEXVERTSRC, frag, true, true))
            return std::unexpected(std::format("{}: program link failed, see hyprland.log", src.name));

        const GLuint program = prog->shader->program();
        GLint        count   = 0;
        glGetProgramiv(program, GL_ACTIVE_UNIFORMS, &count);
        for (GLint i = 0; i < count; ++i) {
            char    name[256];
            GLsizei len  = 0;
            GLint   size = 0;
            GLenum  type = 0;
            glGetActiveUniform(program, i, sizeof(name), &len, &size, &type, name);
            std::string n{name, static_cast<size_t>(len)};
            if (auto br = n.find('['); br != std::string::npos)
                n.resize(br);
            prog->uniforms[n] = {.location = glGetUniformLocation(program, name), .type = type};
        }

        for (const auto& [param, _] : src.params) {
            if (!prog->uniforms.contains(param))
                prog->warnings.push_back(std::format("param {} is not used by the shader", param));
        }

        return prog;
    }

    std::expected<SP<SProgram>, std::string> CShaderCache::get(const SEffectSource& src) {
        const auto key = std::format("{}#{:x}", src.name, std::hash<std::string>{}(src.body));
        if (auto it = m_cache.find(key); it != m_cache.end())
            return it->second;

        auto prog = compile(src);
        if (!prog)
            return prog;

        for (const auto& w : (*prog)->warnings)
            log(std::format("{}: {}", src.name, w));

        m_cache[key] = *prog;
        return *prog;
    }

    void CShaderCache::clear() {
        makeGLCurrent();
        m_cache.clear(); // running effects keep their own reference
    }
}
