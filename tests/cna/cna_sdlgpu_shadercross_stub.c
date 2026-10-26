/**
 * A stand-in for SDL3_shadercross's shared library, for cna_sdlgpu_tests.
 *
 * mojoshader_sdlgpu.c, built without CNA_MOJOSHADER_SDL_GPU_SHADERCROSS_STATIC, loads
 * libSDL3_shadercross.so.0 with SDL_LoadObject() for a device that cannot take SPIR-V, and calls
 * the three functions below through pointers it looks up by name. This library exports exactly
 * those, with SDL_shadercross's signatures, so the tests can exercise that loading path -- and
 * the lifetime of the pointers -- where no real SDL_shadercross is installed. It translates
 * nothing: reflection returns an empty block and compilation always fails with a recognizable
 * error, which the tests read back from MOJOSHADER_sdlGetError().
 *
 * This file is part of the libcna fork of MojoShader; see tests/cna/README.md.
 */

#include <SDL3/SDL.h>

typedef enum SDL_ShaderCross_ShaderStage
{
    SDL_SHADERCROSS_SHADERSTAGE_VERTEX,
    SDL_SHADERCROSS_SHADERSTAGE_FRAGMENT,
    SDL_SHADERCROSS_SHADERSTAGE_COMPUTE
} SDL_ShaderCross_ShaderStage;

typedef struct SDL_ShaderCross_SPIRV_Info
{
    const Uint8 *bytecode;
    size_t bytecode_size;
    const char *entrypoint;
    SDL_ShaderCross_ShaderStage shader_stage;
    SDL_PropertiesID props;
} SDL_ShaderCross_SPIRV_Info;

/*
 * The tests' own SDL_ShaderCross_GraphicsShaderMetadata; the caller only passes it on and
 * SDL_free()s it. The functions take and return it by that name, as the glue's pointer types do:
 * a call through a pointer of another function type is undefined, and clang's -fsanitize=function
 * stops on it (a `void *` return here did).
 */
typedef struct SDL_ShaderCross_GraphicsShaderMetadata
{
    Uint32 reflected_bytes;
} SDL_ShaderCross_GraphicsShaderMetadata;

SDL_DECLSPEC SDL_GPUShaderFormat SDLCALL SDL_ShaderCross_GetSPIRVShaderFormats(void);
SDL_DECLSPEC SDL_ShaderCross_GraphicsShaderMetadata *SDLCALL SDL_ShaderCross_ReflectGraphicsSPIRV(
    const Uint8 *bytecode, size_t bytecode_size, SDL_PropertiesID props);
SDL_DECLSPEC SDL_GPUShader *SDLCALL SDL_ShaderCross_CompileGraphicsShaderFromSPIRV(
    SDL_GPUDevice *device, const SDL_ShaderCross_SPIRV_Info *info,
    const SDL_ShaderCross_GraphicsShaderMetadata *metadata, SDL_PropertiesID props);

SDL_GPUShaderFormat SDLCALL SDL_ShaderCross_GetSPIRVShaderFormats(void)
{
    /* What a real ShaderCross without DXC offers; the tests only need it to be non-zero. */
    return SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_MSL;
}

SDL_ShaderCross_GraphicsShaderMetadata *SDLCALL SDL_ShaderCross_ReflectGraphicsSPIRV(
    const Uint8 *bytecode, size_t bytecode_size, SDL_PropertiesID props)
{
    SDL_ShaderCross_GraphicsShaderMetadata *metadata;
    (void) props;
    if ((bytecode == NULL) || (bytecode_size < 20))
    {
        SDL_SetError("cna shadercross stub: no SPIR-V to reflect");
        return NULL;
    }
    metadata = (SDL_ShaderCross_GraphicsShaderMetadata *) SDL_calloc(1, sizeof (*metadata));
    if (metadata != NULL)
        metadata->reflected_bytes = (Uint32) bytecode_size;
    return metadata;
}

SDL_GPUShader *SDLCALL SDL_ShaderCross_CompileGraphicsShaderFromSPIRV(
    SDL_GPUDevice *device, const SDL_ShaderCross_SPIRV_Info *info,
    const SDL_ShaderCross_GraphicsShaderMetadata *metadata, SDL_PropertiesID props)
{
    (void) device;
    (void) metadata;
    (void) props;
    SDL_SetError("cna shadercross stub: compiled no %s shader",
                 ((info != NULL) && (info->shader_stage == SDL_SHADERCROSS_SHADERSTAGE_VERTEX))
                     ? "vertex" : "fragment");
    return NULL;
}
