/**
 * A stand-in for <SDL3_shadercross/SDL_shadercross.h>, for cna_sdlgpu_static_tests only: the part
 * of SDL_shadercross's API that mojoshader_sdlgpu.c calls when it is built with
 * CNA_MOJOSHADER_SDL_GPU_SHADERCROSS_STATIC, with SDL_shadercross's own types and signatures,
 * so that path builds and links without SDL_shadercross. cna_sdlgpu_static_tests.c defines the
 * functions. Nothing else may include it.
 *
 * This file is part of the libcna fork of MojoShader; see tests/cna/README.md.
 */

#ifndef MOJOSHADER_CNA_SDL_SHADERCROSS_STUB_H
#define MOJOSHADER_CNA_SDL_SHADERCROSS_STUB_H

#include <SDL3/SDL.h>

typedef enum SDL_ShaderCross_ShaderStage
{
    SDL_SHADERCROSS_SHADERSTAGE_VERTEX,
    SDL_SHADERCROSS_SHADERSTAGE_FRAGMENT,
    SDL_SHADERCROSS_SHADERSTAGE_COMPUTE
} SDL_ShaderCross_ShaderStage;

typedef struct SDL_ShaderCross_GraphicsShaderResourceInfo
{
    Uint32 num_samplers;
    Uint32 num_storage_textures;
    Uint32 num_storage_buffers;
    Uint32 num_uniform_buffers;
} SDL_ShaderCross_GraphicsShaderResourceInfo;

typedef struct SDL_ShaderCross_GraphicsShaderMetadata
{
    SDL_ShaderCross_GraphicsShaderResourceInfo resource_info;
    Uint32 num_inputs;
    void *inputs;
    Uint32 num_outputs;
    void *outputs;
} SDL_ShaderCross_GraphicsShaderMetadata;

typedef struct SDL_ShaderCross_SPIRV_Info
{
    const Uint8 *bytecode;
    size_t bytecode_size;
    const char *entrypoint;
    SDL_ShaderCross_ShaderStage shader_stage;
    SDL_PropertiesID props;
} SDL_ShaderCross_SPIRV_Info;

extern bool SDLCALL SDL_ShaderCross_Init(void);
extern void SDLCALL SDL_ShaderCross_Quit(void);
extern SDL_GPUShaderFormat SDLCALL SDL_ShaderCross_GetSPIRVShaderFormats(void);
extern SDL_ShaderCross_GraphicsShaderMetadata * SDLCALL SDL_ShaderCross_ReflectGraphicsSPIRV(
    const Uint8 *bytecode, size_t bytecode_size, SDL_PropertiesID props);
extern SDL_GPUShader * SDLCALL SDL_ShaderCross_CompileGraphicsShaderFromSPIRV(
    SDL_GPUDevice *device, const SDL_ShaderCross_SPIRV_Info *info,
    const SDL_ShaderCross_GraphicsShaderResourceInfo *resource_info, SDL_PropertiesID props);

#endif /* MOJOSHADER_CNA_SDL_SHADERCROSS_STUB_H */
