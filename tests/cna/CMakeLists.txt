# Regression tests for the libcna fork (see README.md in this directory).
#
# The library is compiled again here, separately from the `mojoshader` target, with every source
# profile on -- HLSL and Metal included, whatever the host -- and with the switches FNA3D builds
# MojoShader with, so each emitter is exercised on any machine that can run CMake and a C99
# compiler. The OpenGL glue is compiled (it needs no GL at build time); the Direct3D 11 glue only
# into cna_d3d11_tests, against a stand-in d3d11.h; the SDL_GPU glue only when an SDL3 package is
# found (see the end).

set(_cna_mojoshader_dir "${CMAKE_CURRENT_SOURCE_DIR}/../..")
set(_cna_mojoshader_sources
    ${_cna_mojoshader_dir}/mojoshader.c
    ${_cna_mojoshader_dir}/mojoshader_common.c
    ${_cna_mojoshader_dir}/mojoshader_effects.c
    ${_cna_mojoshader_dir}/mojoshader_opengl.c
    ${_cna_mojoshader_dir}/profiles/mojoshader_profile_arb1.c
    ${_cna_mojoshader_dir}/profiles/mojoshader_profile_bytecode.c
    ${_cna_mojoshader_dir}/profiles/mojoshader_profile_common.c
    ${_cna_mojoshader_dir}/profiles/mojoshader_profile_d3d.c
    ${_cna_mojoshader_dir}/profiles/mojoshader_profile_glsl.c
    ${_cna_mojoshader_dir}/profiles/mojoshader_profile_hlsl.c
    ${_cna_mojoshader_dir}/profiles/mojoshader_profile_metal.c
    ${_cna_mojoshader_dir}/profiles/mojoshader_profile_spirv.c
    cna_test.c
)
add_library(mojoshader_cna_tests STATIC ${_cna_mojoshader_sources})
set(_cna_mojoshader_profile_definitions)
# Not the SUPPORT_PROFILE_*=0 switches the top level derives from its PROFILE_* options (GLSLES3
# has no option there at all, so a standalone build always turns it off): every profile, here.
foreach(_profile D3D BYTECODE HLSL GLSL GLSL120 GLSLES GLSLES3 ARB1 ARB1_NV METAL SPIRV GLSPIRV)
    remove_definitions(-DSUPPORT_PROFILE_${_profile}=0)
    list(APPEND _cna_mojoshader_profile_definitions SUPPORT_PROFILE_${_profile}=1)
endforeach()
set(_cna_mojoshader_test_definitions
    MOJOSHADER_NO_VERSION_INCLUDE
    MOJOSHADER_EFFECT_SUPPORT
    MOJOSHADER_DEPTH_CLIPPING
    MOJOSHADER_FLIP_RENDERTARGET
    MOJOSHADER_XNA4_VERTEX_TEXTURES
    ${_cna_mojoshader_profile_definitions}
)
target_compile_definitions(mojoshader_cna_tests PUBLIC ${_cna_mojoshader_test_definitions})
target_include_directories(mojoshader_cna_tests PUBLIC "${_cna_mojoshader_dir}" "${CMAKE_CURRENT_SOURCE_DIR}")
if(UNIX)
    target_link_libraries(mojoshader_cna_tests PUBLIC m)
endif()

# The same library with MojoShader's own defaults (the top level's DEPTH_CLIPPING, FLIP_VIEWPORT
# and XNA4_VERTEXTEXTURE options are OFF), for the cases whose output depends on those switches.
add_library(mojoshader_cna_default STATIC ${_cna_mojoshader_sources})
target_compile_definitions(mojoshader_cna_default PUBLIC
    MOJOSHADER_NO_VERSION_INCLUDE MOJOSHADER_EFFECT_SUPPORT ${_cna_mojoshader_profile_definitions})
target_include_directories(mojoshader_cna_default PUBLIC "${_cna_mojoshader_dir}" "${CMAKE_CURRENT_SOURCE_DIR}")
if(UNIX)
    target_link_libraries(mojoshader_cna_default PUBLIC m)
endif()

# The OpenGL glue must also compile without the render-target flip, MojoShader's own default.
add_library(mojoshader_cna_opengl_noflip OBJECT ${_cna_mojoshader_dir}/mojoshader_opengl.c)
target_compile_definitions(mojoshader_cna_opengl_noflip PRIVATE
    MOJOSHADER_NO_VERSION_INCLUDE MOJOSHADER_EFFECT_SUPPORT)
target_include_directories(mojoshader_cna_opengl_noflip PRIVATE "${_cna_mojoshader_dir}")

# spirv-val, when found, checks every SPIR-V module a test produces (cna_spirv_validate()).
find_program(MOJOSHADER_SPIRV_VAL NAMES spirv-val)

# mojoshader_cna_test(<name> [<source> <library>]): ${name}.c against mojoshader_cna_tests, or a
# source built once more against another library.
function(mojoshader_cna_test name)
    set(_source ${name}.c)
    set(_library mojoshader_cna_tests)
    if(ARGC GREATER 2)
        set(_source ${ARGV1})
        set(_library ${ARGV2})
    endif()
    add_executable(${name} ${_source})
    target_link_libraries(${name} PRIVATE ${_library})
    add_test(NAME ${name} COMMAND ${name})
    set(_environment "MOJOSHADER_CNA_TEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/${name}.out")
    if(MOJOSHADER_SPIRV_VAL)
        list(APPEND _environment "MOJOSHADER_SPIRV_VAL=${MOJOSHADER_SPIRV_VAL}")
    endif()
    set_tests_properties(${name} PROPERTIES ENVIRONMENT "${_environment}")
endfunction()

# The standalone build's generated version header (see the top-level CMakeLists.txt).
add_library(mojoshader_cna_version_header OBJECT cna_version_header.c)
target_include_directories(mojoshader_cna_version_header PRIVATE
    "${_cna_mojoshader_dir}" "${PROJECT_BINARY_DIR}")

mojoshader_cna_test(cna_build_tests)
target_sources(cna_build_tests PRIVATE $<TARGET_OBJECTS:mojoshader_cna_version_header>)
# Compiled, not linked: the object library defines the same symbols as the test library's copy.
add_dependencies(cna_build_tests mojoshader_cna_opengl_noflip)

mojoshader_cna_test(cna_effect_tests)

mojoshader_cna_test(cna_spirv_tests)

mojoshader_cna_test(cna_ps14_tests)

mojoshader_cna_test(cna_glspirv_tests)
mojoshader_cna_test(cna_glspirv_default_tests cna_glspirv_tests.c mojoshader_cna_default)

mojoshader_cna_test(cna_parser_tests)

mojoshader_cna_test(cna_glsl_tests)

mojoshader_cna_test(cna_opengl_tests)

# The HLSL and Metal emitters. shaderc (glslang's HLSL front end), when its library is found,
# compiles every HLSL source the test produces; the test loads it at run time, so only the
# library is needed, not its headers. Without it those compilations are reported as skipped.
find_library(MOJOSHADER_SHADERC_LIBRARY NAMES shaderc_shared libshaderc.so.1 shaderc)
mojoshader_cna_test(cna_hlsl_metal_tests)
target_link_libraries(cna_hlsl_metal_tests PRIVATE ${CMAKE_DL_LIBS})
if(MOJOSHADER_SHADERC_LIBRARY)
    set_property(TEST cna_hlsl_metal_tests APPEND PROPERTY
        ENVIRONMENT "MOJOSHADER_SHADERC=${MOJOSHADER_SHADERC_LIBRARY}")
endif()

mojoshader_cna_test(cna_printfloat_tests)

# Every profile's translation while each of its allocations fails in turn.
mojoshader_cna_test(cna_alloc_tests)

# The Direct3D 11 glue, compiled against a stand-in d3d11.h (d3d11-stub/) and driven through a
# fake device and context; the real header and API only exist on Windows, where this is not built.
if(NOT WIN32)
    mojoshader_cna_test(cna_d3d11_tests)
    target_include_directories(cna_d3d11_tests BEFORE PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/d3d11-stub")
    target_link_libraries(cna_d3d11_tests PRIVATE ${CMAKE_DL_LIBS})
endif()

# The SDL_GPU glue, when find_package() finds an SDL3 package (CMAKE_PREFIX_PATH). It is built as
# FNA3D builds it on SDL3 -- USE_SDL3 and SDL's standard library (MOJOSHADER_USE_SDL_STDLIB) -- so
# the rest of the library is compiled once more with those switches; cna_sdlgpu_tests compiles
# mojoshader_sdlgpu.c itself, by including it. SDL_shadercross is the dynamic one, loaded with
# SDL_LoadObject() (no CNA_MOJOSHADER_SDL_GPU_SHADERCROSS_STATIC): on Linux a stand-in
# libSDL3_shadercross.so.0 is built and put first on the test's library path. The cases that need
# a GPU device run only with MOJOSHADER_CNA_SDLGPU_DEVICE=1 in the environment (cna_sdlgpu_tests.c).
find_package(SDL3 CONFIG QUIET)
if(TARGET SDL3::SDL3)
    add_library(mojoshader_cna_sdl STATIC
        ${_cna_mojoshader_dir}/mojoshader.c
        ${_cna_mojoshader_dir}/mojoshader_common.c
        ${_cna_mojoshader_dir}/mojoshader_effects.c
        ${_cna_mojoshader_dir}/profiles/mojoshader_profile_arb1.c
        ${_cna_mojoshader_dir}/profiles/mojoshader_profile_bytecode.c
        ${_cna_mojoshader_dir}/profiles/mojoshader_profile_common.c
        ${_cna_mojoshader_dir}/profiles/mojoshader_profile_d3d.c
        ${_cna_mojoshader_dir}/profiles/mojoshader_profile_glsl.c
        ${_cna_mojoshader_dir}/profiles/mojoshader_profile_hlsl.c
        ${_cna_mojoshader_dir}/profiles/mojoshader_profile_metal.c
        ${_cna_mojoshader_dir}/profiles/mojoshader_profile_spirv.c
        cna_test.c
    )
    target_compile_definitions(mojoshader_cna_sdl PUBLIC ${_cna_mojoshader_test_definitions}
        USE_SDL3 MOJOSHADER_USE_SDL_STDLIB)
    target_include_directories(mojoshader_cna_sdl PUBLIC "${_cna_mojoshader_dir}" "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(mojoshader_cna_sdl PUBLIC SDL3::SDL3)
    if(UNIX)
        target_link_libraries(mojoshader_cna_sdl PUBLIC m)
    endif()

    add_executable(cna_sdlgpu_tests cna_sdlgpu_tests.c)
    target_link_libraries(cna_sdlgpu_tests PRIVATE mojoshader_cna_sdl)
    add_test(NAME cna_sdlgpu_tests COMMAND cna_sdlgpu_tests)

    # The statically linked SDL_shadercross path (CNA_MOJOSHADER_SDL_GPU_SHADERCROSS_STATIC, as
    # FNA3D's MOJOSHADER_STATIC_SPIRVCROSS selects it), against a stand-in header
    # (shadercross-stub/) and ShaderCross functions the test defines itself. No device.
    add_executable(cna_sdlgpu_static_tests cna_sdlgpu_static_tests.c)
    target_compile_definitions(cna_sdlgpu_static_tests PRIVATE CNA_MOJOSHADER_SDL_GPU_SHADERCROSS_STATIC)
    target_include_directories(cna_sdlgpu_static_tests BEFORE PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/shadercross-stub")
    target_link_libraries(cna_sdlgpu_static_tests PRIVATE mojoshader_cna_sdl)
    add_test(NAME cna_sdlgpu_static_tests COMMAND cna_sdlgpu_static_tests)

    # A real libSDL3_shadercross.so.0 replaces the stand-in on the test's library path when
    # MOJOSHADER_CNA_SDL_SHADERCROSS_DIR names its directory; shadercross_links_on_device then
    # links through it on the device.
    set(MOJOSHADER_CNA_SDL_SHADERCROSS_DIR "" CACHE PATH
        "Directory of a real libSDL3_shadercross.so.0 for cna_sdlgpu_tests (default: a stand-in)")
    if(MOJOSHADER_CNA_SDL_SHADERCROSS_DIR)
        set_tests_properties(cna_sdlgpu_tests PROPERTIES ENVIRONMENT
            "LD_LIBRARY_PATH=${MOJOSHADER_CNA_SDL_SHADERCROSS_DIR}")
    elseif(UNIX AND NOT APPLE)
        set(_cna_shadercross_stub_dir "${CMAKE_CURRENT_BINARY_DIR}/shadercross-stub")
        add_library(cna_sdlgpu_shadercross_stub SHARED cna_sdlgpu_shadercross_stub.c)
        set_target_properties(cna_sdlgpu_shadercross_stub PROPERTIES
            OUTPUT_NAME SDL3_shadercross
            SOVERSION 0
            LIBRARY_OUTPUT_DIRECTORY "${_cna_shadercross_stub_dir}")
        target_link_libraries(cna_sdlgpu_shadercross_stub PRIVATE SDL3::SDL3)
        add_dependencies(cna_sdlgpu_tests cna_sdlgpu_shadercross_stub)
        set_tests_properties(cna_sdlgpu_tests PROPERTIES ENVIRONMENT
            "LD_LIBRARY_PATH=${_cna_shadercross_stub_dir};MOJOSHADER_CNA_SHADERCROSS_STUB=1")
    endif()
    message(STATUS "MojoShader CNA tests: SDL_GPU glue tests with SDL3 ${SDL3_VERSION}")
else()
    message(STATUS "MojoShader CNA tests: no SDL3 package, SDL_GPU glue tests not built")
endif()
