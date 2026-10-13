# Mirrors __hyprland_api_get_client_hash() (PluginAPI.hpp) from the headers we build against.

function(hwa_abi_string VERSION_H OUT_VAR)
    if(NOT EXISTS "${VERSION_H}")
        message(FATAL_ERROR "Hyprland version.h not found at ${VERSION_H}")
    endif()

    file(READ "${VERSION_H}" text)

    macro(_hwa_define name var)
        string(REGEX MATCH "#define ${name} +\"([^\"]*)\"" _m "${text}")
        if(NOT _m)
            message(FATAL_ERROR "${name} missing in ${VERSION_H}")
        endif()
        set(${var} "${CMAKE_MATCH_1}")
    endmacro()

    macro(_hwa_strip_patch var)
        string(REGEX REPLACE "\\.[^.]*$" "" ${var} "${${var}}")
    endmacro()

    _hwa_define(GIT_COMMIT_HASH commit)
    _hwa_define(AQUAMARINE_VERSION aq)
    _hwa_define(HYPRUTILS_VERSION hu)
    _hwa_define(HYPRGRAPHICS_VERSION hg)
    _hwa_define(HYPRCURSOR_VERSION hc)
    _hwa_define(HYPRLANG_VERSION hlg)

    foreach(v aq hu hg hc hlg)
        _hwa_strip_patch(${v})
    endforeach()

    set(${OUT_VAR} "${commit}_aq_${aq}_hu_${hu}_hg_${hg}_hc_${hc}_hlg_${hlg}" PARENT_SCOPE)
endfunction()
