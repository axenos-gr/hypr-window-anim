cmake_minimum_required(VERSION 3.27)

project(hypr-window-anim
    VERSION 0.1.0
    DESCRIPTION "Shader close animations for Hyprland"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

option(HWA_BUILD_PLUGIN "Build the Hyprland plugin" ON)
option(HWA_BUILD_TESTS "Build tests" ON)
option(HWA_BUILD_TOOLS "Build hwa-sheet, the offline effect renderer" OFF)

include(GNUInstallDirs)
find_package(PkgConfig REQUIRED)
pkg_check_modules(tomlpp REQUIRED IMPORTED_TARGET tomlplusplus)

set(HWA_PLUGIN_DIR "${CMAKE_INSTALL_LIBDIR}/hypr-window-anim") # relative: honours --prefix
set(HWA_PLUGIN_NAME "hypr-window-anim.so")

# ---------------------------------------------------------------- embedded assets
set(HWA_ASSETS
    shaders/prelude.glsl
    shaders/bmw-common.glsl
    config/default.toml
)
set(HWA_EFFECTS fade tv-glitch incinerate disintegrate shatter slice doom black-hole hexagon energize debug-uv)
foreach(effect IN LISTS HWA_EFFECTS)
    list(APPEND HWA_ASSETS effects/${effect}.frag effects/${effect}.toml)
endforeach()
list(TRANSFORM HWA_ASSETS PREPEND "${CMAKE_SOURCE_DIR}/" OUTPUT_VARIABLE HWA_ASSET_PATHS)
set(HWA_EMBEDDED "${CMAKE_BINARY_DIR}/generated/EmbeddedAssets.cpp")
string(REPLACE ";" "|" HWA_ASSET_ARG "${HWA_ASSETS}")
add_custom_command(
    OUTPUT ${HWA_EMBEDDED}
    COMMAND ${CMAKE_COMMAND} -DROOT=${CMAKE_SOURCE_DIR} -DOUT=${HWA_EMBEDDED} "-DFILES=${HWA_ASSET_ARG}" -P ${CMAKE_SOURCE_DIR}/cmake/EmbedAssets.cmake
    DEPENDS ${HWA_ASSET_PATHS} ${CMAKE_SOURCE_DIR}/cmake/EmbedAssets.cmake
    COMMENT "Embedding shaders and default config"
    VERBATIM
)

# ---------------------------------------------------------------- common
add_library(hwa-common STATIC
    common/src/Assets.cpp
    common/src/Config.cpp
    common/src/Effect.cpp
    common/src/Paths.cpp
    common/src/ShaderAssembly.cpp
    ${HWA_EMBEDDED}
)
set_target_properties(hwa-common PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(hwa-common PUBLIC common/include)
target_link_libraries(hwa-common PUBLIC PkgConfig::tomlpp)
target_compile_definitions(hwa-common PUBLIC
    HWA_VERSION="${PROJECT_VERSION}"
    HWA_PLUGIN_PATH="${CMAKE_INSTALL_PREFIX}/${HWA_PLUGIN_DIR}/${HWA_PLUGIN_NAME}"
)
target_compile_options(hwa-common PRIVATE -Wall -Wextra)
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_compile_options(hwa-common PRIVATE -fno-gnu-unique) # linked into the plugin; UNIQUE symbols block dlclose
endif()

# ---------------------------------------------------------------- cli
add_executable(hypr-window-anim
    cli/src/main.cpp
    cli/src/HyprSocket.cpp
    cli/src/Commands.cpp
)
target_link_libraries(hypr-window-anim PRIVATE hwa-common)
target_compile_options(hypr-window-anim PRIVATE -Wall -Wextra)
install(TARGETS hypr-window-anim RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

# ---------------------------------------------------------------- plugin
if(HWA_BUILD_PLUGIN)
    pkg_check_modules(hyprland REQUIRED IMPORTED_TARGET
        hyprland
        libdrm
        libinput
        libudev
        pixman-1
        wayland-server
        xkbcommon
    )
    pkg_get_variable(HYPRLAND_PREFIX hyprland prefix)

    include(cmake/AbiString.cmake)
    set(HYPRLAND_VERSION_H "${HYPRLAND_PREFIX}/hyprland/src/version.h")
    hwa_abi_string("${HYPRLAND_VERSION_H}" HWA_ABI)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${HYPRLAND_VERSION_H}")
    message(STATUS "Building plugin for Hyprland ABI ${HWA_ABI}")

    add_library(hwa-plugin MODULE
        plugin/src/main.cpp
        plugin/src/Hooks.cpp
        plugin/src/CloseController.cpp
        plugin/src/EffectManager.cpp
        plugin/src/EffectPassElement.cpp
        plugin/src/ShaderCache.cpp
        plugin/src/ConfigWatcher.cpp
        plugin/src/OpenTransformer.cpp
        plugin/src/WindowBlur.cpp
    )
    set_target_properties(hwa-plugin PROPERTIES PREFIX "" OUTPUT_NAME "hypr-window-anim")
    target_link_libraries(hwa-plugin PRIVATE hwa-common PkgConfig::hyprland)
    target_compile_definitions(hwa-plugin PRIVATE HWA_BUILD_ABI="${HWA_ABI}")
    # No -fvisibility=hidden: Hyprland's inline globals must bind to the compositor's copies
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        target_compile_options(hwa-plugin PRIVATE -fno-gnu-unique)
    endif()
    target_compile_options(hwa-plugin PRIVATE -Wall -Wextra -Wno-unused-parameter)

    file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/${HWA_PLUGIN_NAME}.abi" CONTENT "${HWA_ABI}\n")
    install(TARGETS hwa-plugin LIBRARY DESTINATION ${HWA_PLUGIN_DIR})
    install(FILES "${CMAKE_BINARY_DIR}/${HWA_PLUGIN_NAME}.abi" DESTINATION ${HWA_PLUGIN_DIR})
endif()

# ---------------------------------------------------------------- dev tools
if(HWA_BUILD_TOOLS)
    pkg_check_modules(sheetdeps REQUIRED IMPORTED_TARGET egl glesv2 libpng)
    add_executable(hwa-sheet tools/sheet/main.cpp)
    target_link_libraries(hwa-sheet PRIVATE hwa-common PkgConfig::sheetdeps)
    target_compile_options(hwa-sheet PRIVATE -Wall -Wextra)
endif()

# ---------------------------------------------------------------- data
install(DIRECTORY effects/ DESTINATION ${CMAKE_INSTALL_DATADIR}/hypr-window-anim/effects)
install(FILES config/default.toml DESTINATION ${CMAKE_INSTALL_DATADIR}/hypr-window-anim)

# ---------------------------------------------------------------- tests
if(HWA_BUILD_TESTS)
    enable_testing()
    add_executable(hwa-tests tests/main.cpp)
    target_link_libraries(hwa-tests PRIVATE hwa-common)
    add_test(NAME common COMMAND hwa-tests)

    find_program(GLSLANG_VALIDATOR glslangValidator)
    if(GLSLANG_VALIDATOR)
        add_test(NAME shaders COMMAND hypr-window-anim check --offline --all)
        set_tests_properties(shaders PROPERTIES ENVIRONMENT "HWA_CONFIG_DIR=${CMAKE_BINARY_DIR}/test-config")
    endif()

    # needs a GPU: every effect must start on the window and end on nothing
    if(HWA_BUILD_TOOLS)
        foreach(effect IN LISTS HWA_EFFECTS)
            if(NOT effect STREQUAL "debug-uv")
                add_test(NAME endpoints-${effect} COMMAND hwa-sheet ${effect} --verify)
                set_tests_properties(endpoints-${effect} PROPERTIES ENVIRONMENT "HWA_CONFIG_DIR=${CMAKE_BINARY_DIR}/test-config")
            endif()
        endforeach()
    endif()
endif()
