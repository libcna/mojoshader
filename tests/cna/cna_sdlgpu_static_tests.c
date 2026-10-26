/**
 * SDL_GPU glue regressions with a statically linked SDL_shadercross
 * (CNA_MOJOSHADER_SDL_GPU_SHADERCROSS_STATIC), without SDL_shadercross or a GPU device.
 *
 * The glue is compiled into this file against tests/cna/shadercross-stub/, and the ShaderCross
 * functions it calls are defined below. As in SDL_shadercross, only SDL_ShaderCross_Init() makes
 * GetSPIRVShaderFormats() offer DXBC (it loads d3dcompiler); Init and Quit are counted.
 *
 * This file is part of the libcna fork of MojoShader; see tests/cna/README.md.
 */

/* The glue's types and statics; take the translation unit whole, as cna_sdlgpu_tests does. */
#include "../../mojoshader_sdlgpu.c"

#include <stdio.h>
#include "cna_test.h"

/* -- A statically linked SDL_shadercross --------------------------------------------------- */

static int shadercross_inits = 0;
static int shadercross_quits = 0;
static int shadercross_initialized = 0;

bool SDLCALL SDL_ShaderCross_Init(void)
{
    shadercross_inits++;
    shadercross_initialized = 1;
    return true;
}

void SDLCALL SDL_ShaderCross_Quit(void)
{
    shadercross_quits++;
    shadercross_initialized = 0;
}

SDL_GPUShaderFormat SDLCALL SDL_ShaderCross_GetSPIRVShaderFormats(void)
{
    /* SDL_shadercross without DXC: DXBC only once d3dcompiler is loaded. */
    SDL_GPUShaderFormat formats = SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_MSL;
    if (shadercross_initialized)
        formats |= SDL_GPU_SHADERFORMAT_DXBC;
    return formats;
}

SDL_ShaderCross_GraphicsShaderMetadata * SDLCALL SDL_ShaderCross_ReflectGraphicsSPIRV(
    const Uint8 *bytecode, size_t bytecode_size, SDL_PropertiesID props)
{
    (void) bytecode; (void) bytecode_size; (void) props;
    SDL_SetError("cna static shadercross stub: reflects nothing");
    return NULL;
}

SDL_GPUShader * SDLCALL SDL_ShaderCross_CompileGraphicsShaderFromSPIRV(
    SDL_GPUDevice *device, const SDL_ShaderCross_SPIRV_Info *info,
    const SDL_ShaderCross_GraphicsShaderResourceInfo *resource_info, SDL_PropertiesID props)
{
    (void) device; (void) info; (void) resource_info; (void) props;
    SDL_SetError("cna static shadercross stub: compiles nothing");
    return NULL;
}

/* -- Cases --------------------------------------------------------------------------------- */

/*
 * R1-05 (2026-10-26 remediation review): SDL_ShaderCross_Init() and _Quit() act on the whole
 * process and are not reference-counted, so the glue must leave them to the application: it
 * calls neither. DXBC is then offered only once the application has initialized ShaderCross,
 * and a context the static path refuses says so, naming SDL_ShaderCross_Init().
 */
static void test_shadercross_init_is_the_applications(void)
{
    MOJOSHADER_sdlContext *ctx;
    unsigned int formats = MOJOSHADER_sdlGetShaderFormats();

    CNA_CHECK((formats & SDL_GPU_SHADERFORMAT_SPIRV) && (formats & SDL_GPU_SHADERFORMAT_MSL),
              "the formats include SPIR-V and ShaderCross's own");
    CNA_CHECK(!(formats & SDL_GPU_SHADERFORMAT_DXBC), "no DXBC before SDL_ShaderCross_Init()");

    /* No device takes no format: the static path refuses the context. */
    ctx = MOJOSHADER_sdlCreateContext(NULL, NULL, NULL, NULL);
    CNA_CHECK(ctx == NULL, "a context ShaderCross cannot serve is refused");
    if (ctx != NULL)
        MOJOSHADER_sdlDestroyContext(ctx);
    CNA_CHECK(strstr(MOJOSHADER_sdlGetError(NULL), "SDL_ShaderCross_Init()") != NULL,
              "... naming SDL_ShaderCross_Init()");

    /* The application initializes ShaderCross, as CNA's SDL_GPU renderer does. */
    SDL_ShaderCross_Init();
    formats = MOJOSHADER_sdlGetShaderFormats();
    CNA_CHECK(formats & SDL_GPU_SHADERFORMAT_DXBC, "DXBC after the application's Init");
    ctx = MOJOSHADER_sdlCreateContext(NULL, NULL, NULL, NULL);
    if (ctx != NULL)
        MOJOSHADER_sdlDestroyContext(ctx);
    CNA_CHECK(shadercross_initialized, "MojoShader leaves ShaderCross initialized");
    SDL_ShaderCross_Quit();

    CNA_CHECK((shadercross_inits == 1) && (shadercross_quits == 1),
              "MojoShader called neither SDL_ShaderCross_Init() nor SDL_ShaderCross_Quit()");
} // test_shadercross_init_is_the_applications

int main(void)
{
    cna_test_run("shadercross_init_is_the_applications", test_shadercross_init_is_the_applications);
    return cna_test_finish();
} // main
