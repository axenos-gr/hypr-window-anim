#pragma once

#include "hwa/Effect.hpp"

#include <hyprland/src/render/Shader.hpp>

#include <expected>
#include <map>
#include <string>

namespace hwa::plugin {
    struct SUniformInfo {
        GLint  location = -1;
        GLenum type     = 0;
    };

    struct SProgram {
        std::string                         name;
        SP<CShader>                         shader;
        std::map<std::string, SUniformInfo> uniforms;
        std::vector<std::string>            warnings;

        GLint                               location(const std::string& name) const;
        void                                setParam(const std::string& name, const ParamValue& v) const;
    };

    class CShaderCache {
      public:
        // Needs the EGL context current
        std::expected<SP<SProgram>, std::string> get(const SEffectSource& src);
        // Uncached, for `check`
        std::expected<SP<SProgram>, std::string> compile(const SEffectSource& src);
        void                                     clear();

      private:
        std::map<std::string, SP<SProgram>> m_cache; // key: name + body hash
    };

    CShaderCache& shaderCache();
}
