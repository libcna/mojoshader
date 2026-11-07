/* SPDX-License-Identifier: Zlib */
/* Included only by cna_sdlgpu_tests.c, after its context/allocator helpers.
 * Coordinates come from D3D9 TEXREG2AR/GB/RGB and _bx2 contracts. For nearest
 * volume reads choose floor(4*coordinate), then clamp. Cube face/coordinates:
 * https://github.khronos.org/Vulkan-Site/spec/latest/chapters/textures.html#textures-cube-map-face-selection
 * SDL layer order: https://wiki.libsdl.org/SDL3/SDL_GPUCubeMapFace
 * Source RGBA is (32,96,160,224)/255. RGB selects +Z at (2,0); _bx2 selects
 * -X at (2,2). Volume selects (0,1,2), or (0,0,1) with _bx2. None is on a tie. */

#define TEXREG_WIDTH 8
#define TEXREG_HEIGHT 8
typedef struct TexRegPixels
{
    SDL_GPUTexture *source, *lookup[3], *target;
    SDL_GPUBuffer *vertices;
    SDL_GPUTransferBuffer *upload, *download;
    SDL_GPUSampler *sampler;
} TexRegPixels;

static SDL_GPUTexture *texreg_texture(SDL_GPUTextureType type, Uint32 size, Uint32 depth,
                                      SDL_GPUTextureUsageFlags usage)
{
    SDL_GPUTextureCreateInfo info;
    SDL_zero(info);
    info.type = type;
    info.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    info.width = info.height = size;
    info.layer_count_or_depth = depth;
    info.num_levels = 1;
    info.sample_count = SDL_GPU_SAMPLECOUNT_1;
    info.usage = usage;
    return SDL_CreateGPUTexture(gpu_device, &info);
}

static void texreg_upload(SDL_GPUCopyPass *pass, SDL_GPUTransferBuffer *buffer, Uint32 offset,
                          SDL_GPUTexture *texture, Uint32 layer, Uint32 size, Uint32 depth)
{
    SDL_GPUTextureTransferInfo from;
    SDL_GPUTextureRegion to;
    SDL_zero(from);
    SDL_zero(to);
    from.transfer_buffer = buffer;
    from.offset = offset;
    to.texture = texture;
    to.layer = layer;
    to.w = to.h = size;
    to.d = depth;
    SDL_UploadToGPUTexture(pass, &from, &to, false);
}

static int texreg_pixels_start(TexRegPixels *fixture, const float coordinates[4])
{
    float vertices[3][8] = {{-1, -1, 0, 1, .5f, .5f, 0, 1},
                            {3, -1, 0, 1, .5f, .5f, 0, 1},
                            {-1, 3, 0, 1, .5f, .5f, 0, 1}};
    SDL_GPUTransferBufferCreateInfo transfer;
    SDL_GPUBufferCreateInfo vertex;
    SDL_GPUSamplerCreateInfo sampler;
    SDL_GPUTransferBufferLocation from;
    SDL_GPUBufferRegion to;
    SDL_GPUCommandBuffer *command;
    SDL_GPUCopyPass *copy;
    uint8_t *bytes;
    int x, y, z, face;
    if (coordinates != NULL)
        for (x = 0; x < 3; x++)
            SDL_memcpy(vertices[x] + 4, coordinates, 4 * sizeof(float));
    SDL_zero(*fixture);
    SDL_zero(transfer);
    SDL_zero(vertex);
    SDL_zero(sampler);
    fixture->source = texreg_texture(SDL_GPU_TEXTURETYPE_2D, 1, 1, SDL_GPU_TEXTUREUSAGE_SAMPLER);
    fixture->lookup[0] = texreg_texture(SDL_GPU_TEXTURETYPE_2D, 4, 1, SDL_GPU_TEXTUREUSAGE_SAMPLER);
    fixture->lookup[1] =
        texreg_texture(SDL_GPU_TEXTURETYPE_CUBE, 4, 6, SDL_GPU_TEXTUREUSAGE_SAMPLER);
    fixture->lookup[2] = texreg_texture(SDL_GPU_TEXTURETYPE_3D, 4, 4, SDL_GPU_TEXTUREUSAGE_SAMPLER);
    fixture->target =
        texreg_texture(SDL_GPU_TEXTURETYPE_2D, TEXREG_WIDTH, 1, SDL_GPU_TEXTUREUSAGE_COLOR_TARGET);
    vertex.usage = SDL_GPU_BUFFERUSAGE_VERTEX;
    vertex.size = sizeof(vertices);
    fixture->vertices = SDL_CreateGPUBuffer(gpu_device, &vertex);
    transfer.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    transfer.size = 4096;
    fixture->upload = SDL_CreateGPUTransferBuffer(gpu_device, &transfer);
    transfer.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
    transfer.size = TEXREG_WIDTH * TEXREG_HEIGHT * 4;
    fixture->download = SDL_CreateGPUTransferBuffer(gpu_device, &transfer);
    sampler.min_filter = sampler.mag_filter = SDL_GPU_FILTER_NEAREST;
    sampler.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
    sampler.address_mode_u = sampler.address_mode_v = sampler.address_mode_w =
        SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    fixture->sampler = SDL_CreateGPUSampler(gpu_device, &sampler);
    if (!fixture->source || !fixture->lookup[0] || !fixture->lookup[1] || !fixture->lookup[2] ||
        !fixture->target || !fixture->vertices || !fixture->upload || !fixture->download ||
        !fixture->sampler)
        return 0;
    bytes = SDL_MapGPUTransferBuffer(gpu_device, fixture->upload, false);
    if (bytes == NULL)
        return 0;
    SDL_memset(bytes, 0, 4096);
    bytes[0] = 32;
    bytes[1] = 96;
    bytes[2] = 160;
    bytes[3] = 224;
    for (y = 0; y < 4; y++)
        for (x = 0; x < 4; x++)
        {
            uint8_t *p = bytes + 256 + (y * 4 + x) * 4;
            p[0] = (uint8_t)((x + 1) * 40);
            p[1] = (uint8_t)((y + 1) * 40);
            p[2] = (uint8_t)((x * 4 + y + 1) * 7);
            p[3] = 255;
            for (face = 0; face < 6; face++)
            {
                p = bytes + 512 + face * 256 + (y * 4 + x) * 4;
                p[0] = (uint8_t)((x + 1) * 40);
                p[1] = (uint8_t)((y + 1) * 40);
                p[2] = (uint8_t)((face + 1) * 30);
                p[3] = 255;
            }
            for (z = 0; z < 4; z++)
            {
                p = bytes + 2048 + ((z * 4 + y) * 4 + x) * 4;
                p[0] = (uint8_t)((x + 1) * 40);
                p[1] = (uint8_t)((y + 1) * 40);
                p[2] = (uint8_t)((z * 16 + x * 4 + y + 1) * 3);
                p[3] = 255;
            }
        }
    SDL_memcpy(bytes + 3072, vertices, sizeof(vertices));
    SDL_UnmapGPUTransferBuffer(gpu_device, fixture->upload);
    command = SDL_AcquireGPUCommandBuffer(gpu_device);
    if (command == NULL)
        return 0;
    copy = SDL_BeginGPUCopyPass(command);
    if (copy == NULL)
    {
        SDL_CancelGPUCommandBuffer(command);
        return 0;
    }
    texreg_upload(copy, fixture->upload, 0, fixture->source, 0, 1, 1);
    texreg_upload(copy, fixture->upload, 256, fixture->lookup[0], 0, 4, 1);
    for (face = 0; face < 6; face++)
        texreg_upload(copy, fixture->upload, 512 + face * 256, fixture->lookup[1], face, 4, 1);
    texreg_upload(copy, fixture->upload, 2048, fixture->lookup[2], 0, 4, 4);
    SDL_zero(from);
    SDL_zero(to);
    from.transfer_buffer = fixture->upload;
    from.offset = 3072;
    to.buffer = fixture->vertices;
    to.size = sizeof(vertices);
    SDL_UploadToGPUBuffer(copy, &from, &to, false);
    SDL_EndGPUCopyPass(copy);
    return SDL_SubmitGPUCommandBuffer(command) && SDL_WaitForGPUIdle(gpu_device);
}

static void texreg_pixels_finish(TexRegPixels *f)
{
    int i;
    if (f->source)
        SDL_ReleaseGPUTexture(gpu_device, f->source);
    for (i = 0; i < 3; i++)
        if (f->lookup[i])
            SDL_ReleaseGPUTexture(gpu_device, f->lookup[i]);
    if (f->target)
        SDL_ReleaseGPUTexture(gpu_device, f->target);
    if (f->vertices)
        SDL_ReleaseGPUBuffer(gpu_device, f->vertices);
    if (f->upload)
        SDL_ReleaseGPUTransferBuffer(gpu_device, f->upload);
    if (f->download)
        SDL_ReleaseGPUTransferBuffer(gpu_device, f->download);
    if (f->sampler)
        SDL_ReleaseGPUSampler(gpu_device, f->sampler);
}

static int texreg_pixels_draw(TexRegPixels *f, MOJOSHADER_sdlContext *ctx,
                              MOJOSHADER_sdlShaderData *vs, MOJOSHADER_sdlShaderData *ps,
                              int destination, int dimension, const uint8_t expected[4])
{
    SDL_GPUVertexBufferDescription buffer;
    SDL_GPUVertexAttribute attributes[2];
    SDL_GPUColorTargetDescription target;
    SDL_GPUGraphicsPipelineCreateInfo info;
    SDL_GPUGraphicsPipeline *pipeline;
    SDL_GPUCommandBuffer *command;
    SDL_GPURenderPass *render;
    SDL_GPUCopyPass *copy;
    SDL_GPUColorTargetInfo color;
    SDL_GPUBufferBinding binding;
    SDL_GPUTextureSamplerBinding samplers[4];
    SDL_GPUTextureRegion from;
    SDL_GPUTextureTransferInfo to;
    const uint8_t *pixels;
    int i, same = 1;
    int position = MOJOSHADER_sdlGetVertexAttribLocation(vs, MOJOSHADER_USAGE_POSITION, 0);
    int coordinates = MOJOSHADER_sdlGetVertexAttribLocation(vs, MOJOSHADER_USAGE_COLOR, 0);
    if (position < 0 || coordinates < 0)
        return 0;
    if (MOJOSHADER_sdlGetSamplerSlots(vs) > 4 || MOJOSHADER_sdlGetSamplerSlots(ps) > 4)
        return 0;
    SDL_zero(buffer);
    SDL_zero(attributes);
    SDL_zero(target);
    SDL_zero(info);
    buffer.pitch = 8 * sizeof(float);
    buffer.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
    attributes[0].location = position;
    attributes[0].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4;
    attributes[1].location = coordinates;
    attributes[1].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4;
    attributes[1].offset = 4 * sizeof(float);
    target.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    MOJOSHADER_sdlGetShaders(ctx, &info.vertex_shader, &info.fragment_shader);
    info.vertex_input_state.vertex_buffer_descriptions = &buffer;
    info.vertex_input_state.num_vertex_buffers = 1;
    info.vertex_input_state.vertex_attributes = attributes;
    info.vertex_input_state.num_vertex_attributes = 2;
    info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    info.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
    info.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
    info.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
    info.target_info.color_target_descriptions = &target;
    info.target_info.num_color_targets = 1;
    pipeline = SDL_CreateGPUGraphicsPipeline(gpu_device, &info);
    if (pipeline == NULL)
        return 0;
    command = SDL_AcquireGPUCommandBuffer(gpu_device);
    if (command == NULL)
    {
        SDL_ReleaseGPUGraphicsPipeline(gpu_device, pipeline);
        return 0;
    }
    SDL_zero(color);
    SDL_zero(binding);
    color.texture = f->target;
    color.load_op = SDL_GPU_LOADOP_CLEAR;
    color.store_op = SDL_GPU_STOREOP_STORE;
    MOJOSHADER_sdlUpdateUniformBuffers(ctx, command);
    render = SDL_BeginGPURenderPass(command, &color, 1, NULL);
    if (render == NULL)
    {
        SDL_CancelGPUCommandBuffer(command);
        SDL_ReleaseGPUGraphicsPipeline(gpu_device, pipeline);
        return 0;
    }
    SDL_BindGPUGraphicsPipeline(render, pipeline);
    binding.buffer = f->vertices;
    SDL_BindGPUVertexBuffers(render, 0, &binding, 1);
    for (i = 0; i < 4; i++)
    {
        samplers[i].texture = f->source;
        samplers[i].sampler = f->sampler;
    }
    SDL_BindGPUVertexSamplers(render, 0, samplers, MOJOSHADER_sdlGetSamplerSlots(vs));
    samplers[destination].texture = f->lookup[dimension];
    SDL_BindGPUFragmentSamplers(render, 0, samplers, MOJOSHADER_sdlGetSamplerSlots(ps));
    SDL_DrawGPUPrimitives(render, 3, 1, 0, 0);
    SDL_EndGPURenderPass(render);
    copy = SDL_BeginGPUCopyPass(command);
    if (copy == NULL)
    {
        SDL_CancelGPUCommandBuffer(command);
        SDL_ReleaseGPUGraphicsPipeline(gpu_device, pipeline);
        return 0;
    }
    SDL_zero(from);
    SDL_zero(to);
    from.texture = f->target;
    from.w = TEXREG_WIDTH;
    from.h = TEXREG_HEIGHT;
    from.d = 1;
    to.transfer_buffer = f->download;
    SDL_DownloadFromGPUTexture(copy, &from, &to);
    SDL_EndGPUCopyPass(copy);
    if (!SDL_SubmitGPUCommandBuffer(command) || !SDL_WaitForGPUIdle(gpu_device))
    {
        SDL_ReleaseGPUGraphicsPipeline(gpu_device, pipeline);
        return 0;
    }
    pixels = SDL_MapGPUTransferBuffer(gpu_device, f->download, false);
    if (pixels == NULL)
    {
        SDL_ReleaseGPUGraphicsPipeline(gpu_device, pipeline);
        return 0;
    }
    for (i = 0; i < TEXREG_WIDTH * TEXREG_HEIGHT; i++)
        if (SDL_memcmp(pixels + i * 4, expected, 4))
        {
            same = 0;
            break;
        }
    if (!same)
        fprintf(stderr, "mapped texreg pixel%d=(%u,%u,%u,%u), expected=(%u,%u,%u,%u)\n", i,
                pixels[i * 4], pixels[i * 4 + 1], pixels[i * 4 + 2], pixels[i * 4 + 3], expected[0],
                expected[1], expected[2], expected[3]);
    CNA_CHECK(same, "native pixels match the literal expected result");
    SDL_UnmapGPUTransferBuffer(gpu_device, f->download);
    SDL_ReleaseGPUGraphicsPipeline(gpu_device, pipeline);
    return 1;
}

static void test_texreg2_pixels(void)
{
    static const uint32_t vertex[] = {CNA_VS(2, 0),
                                      CNA_OP(CNA_DCL_OP, 2),
                                      CNA_DCL(0, 0),
                                      CNA_DST(CNA_INPUT, 0, 15),
                                      CNA_OP(CNA_DCL_OP, 2),
                                      CNA_DCL(10, 0),
                                      CNA_DST(CNA_INPUT, 1, 15),
                                      CNA_OP(CNA_MOV, 2),
                                      CNA_DST(CNA_RASTOUT, 0, 15),
                                      CNA_SRC(CNA_INPUT, 0),
                                      CNA_OP(CNA_MOV, 2),
                                      CNA_DST(CNA_TEXCRDOUT, 0, 15),
                                      CNA_SRC(CNA_INPUT, 1),
                                      CNA_OP(CNA_MOV, 2),
                                      CNA_DST(CNA_TEXCRDOUT, 1, 15),
                                      CNA_SRC(CNA_INPUT, 1),
                                      CNA_OP(CNA_MOV, 2),
                                      CNA_DST(CNA_TEXCRDOUT, 2, 15),
                                      CNA_SRC(CNA_INPUT, 1),
                                      CNA_OP(CNA_MOV, 2),
                                      CNA_DST(CNA_TEXCRDOUT, 3, 15),
                                      CNA_SRC(CNA_INPUT, 1),
                                      CNA_END};
    static const uint8_t rgb[3][2][4] = {{{40, 80, 14, 255}, {40, 40, 7, 255}},
                                         {{120, 40, 150, 255}, {120, 120, 60, 255}},
                                         {{40, 80, 102, 255}, {40, 40, 51, 255}}};
    static const uint8_t ar[4] = {160, 40, 91, 255}, gb[4] = {80, 120, 49, 255};
    static const int opcodes[] = {69, 70, 82};
    CountingAllocator a;
    TexRegPixels fixture;
    MOJOSHADER_sdlContext *ctx = make_context(gpu_device, &a);
    MOJOSHADER_sdlShaderData *vs = NULL;
    int opcode, destination, source, dimension, signedScale, draws = 0;
    if (ctx == NULL)
    {
        CNA_CHECK(0, "native pixel context is made");
        return;
    }
    if (!texreg_pixels_start(&fixture, NULL))
    {
        CNA_CHECK(0, "native pixel textures/buffers upload");
        fprintf(stderr, "%s\n", SDL_GetError());
        goto done;
    }
    vs = compile(ctx, vertex, sizeof(vertex));
    CNA_CHECK(vs != NULL, "native pixel vertex program compiles");
    if (vs != NULL)
        for (opcode = 0; opcode < 3; opcode++)
            for (destination = 1; destination <= 3; destination++)
                for (source = 0; source < destination; source++)
                    for (dimension = 0; dimension < (opcode == 2 ? 3 : 1); dimension++)
                        for (signedScale = 0; signedScale < (opcode == 2 ? 2 : 1); signedScale++)
                        {
                            const uint32_t words[] = {CNA_PS(1, 2),
                                                      CNA_TEX,
                                                      CNA_DST(CNA_TEXTURE, source, 15),
                                                      opcodes[opcode],
                                                      CNA_DST(CNA_TEXTURE, destination, 15),
                                                      CNA_SRC(CNA_TEXTURE, source) |
                                                          (signedScale ? 0x04000000u : 0),
                                                      CNA_MOV,
                                                      CNA_DST(CNA_TEMP, 0, 15),
                                                      CNA_SRC(CNA_TEXTURE, destination),
                                                      CNA_END};
                            MOJOSHADER_samplerMap map = {destination,
                                                         (MOJOSHADER_samplerType)dimension};
                            MOJOSHADER_sdlShaderData *ps = MOJOSHADER_sdlCompileShader(
                                ctx, "main", (const unsigned char *)words, sizeof(words), NULL, 0,
                                &map, 1);
                            const uint8_t *expected = opcode == 0   ? ar
                                                      : opcode == 1 ? gb
                                                                    : rgb[dimension][signedScale];
                            printf("mapped texreg opcode=%d source=%d destination=%d type=%d "
                                   "signed=%d\n",
                                   opcodes[opcode], source, destination, dimension, signedScale);
                            CNA_CHECK(ps != NULL, "native pixel fragment program compiles");
                            if (ps == NULL)
                                fprintf(stderr, "mapped texreg compile: %s\n",
                                        MOJOSHADER_sdlGetError(ctx));
                            if (ps != NULL && link_pair(ctx, vs, ps) != NULL)
                            {
                                int rendered = texreg_pixels_draw(&fixture, ctx, vs, ps,
                                                                  destination, dimension, expected);
                                CNA_CHECK(rendered, "native dependent-texture draw completes");
                                if (!rendered)
                                    fprintf(stderr, "%s\n", SDL_GetError());
                                draws += rendered;
                            }
                            else
                                CNA_CHECK(0, "native dependent-texture programs link");
                            MOJOSHADER_sdlDeleteShader(ctx, ps);
                        }
    CNA_CHECK(draws == 48, "all mapped dependent-texture variants rendered");
done:
    MOJOSHADER_sdlDeleteShader(ctx, vs);
    texreg_pixels_finish(&fixture);
    MOJOSHADER_sdlDestroyContext(ctx);
    CNA_CHECK(a.live == 0, "mapped pixel context returns all MojoShader allocations");
}

/* Native source bytes yield dot=(80+8*destination)/255 from the distinct
 * coordinate sets (2+destination/4,1,-1/2,9), so TEXDP3 outputs 88/96/104.
 * _bx2 makes the dot negative. TEXDP3TEX chooses (x=1,y=0), or (0,0) signed. */
static void test_texdp3_pixels(void)
{
    static const uint32_t vertex[] = {CNA_VS(2, 0),
                                      CNA_OP(CNA_DCL_OP, 2),
                                      CNA_DCL(0, 0),
                                      CNA_DST(CNA_INPUT, 0, 15),
                                      CNA_OP(CNA_DCL_OP, 2),
                                      CNA_DCL(10, 0),
                                      CNA_DST(CNA_INPUT, 1, 15),
                                      CNA_OP(CNA_DEF, 5),
                                      CNA_DST(CNA_CONST, 0, 15),
                                      0x3e800000u,
                                      0,
                                      0,
                                      0,
                                      CNA_OP(CNA_MOV, 2),
                                      CNA_DST(CNA_RASTOUT, 0, 15),
                                      CNA_SRC(CNA_INPUT, 0),
                                      CNA_OP(CNA_MOV, 2),
                                      CNA_DST(CNA_TEXCRDOUT, 0, 15),
                                      CNA_SRC(CNA_INPUT, 1),
                                      CNA_OP(CNA_ADD, 3),
                                      CNA_DST(CNA_TEMP, 0, 15),
                                      CNA_SRC(CNA_INPUT, 1),
                                      CNA_SRC(CNA_CONST, 0),
                                      CNA_OP(CNA_MOV, 2),
                                      CNA_DST(CNA_TEXCRDOUT, 1, 15),
                                      CNA_SRC(CNA_TEMP, 0),
                                      CNA_OP(CNA_ADD, 3),
                                      CNA_DST(CNA_TEMP, 0, 15),
                                      CNA_SRC(CNA_TEMP, 0),
                                      CNA_SRC(CNA_CONST, 0),
                                      CNA_OP(CNA_MOV, 2),
                                      CNA_DST(CNA_TEXCRDOUT, 2, 15),
                                      CNA_SRC(CNA_TEMP, 0),
                                      CNA_OP(CNA_ADD, 3),
                                      CNA_DST(CNA_TEMP, 0, 15),
                                      CNA_SRC(CNA_TEMP, 0),
                                      CNA_SRC(CNA_CONST, 0),
                                      CNA_OP(CNA_MOV, 2),
                                      CNA_DST(CNA_TEXCRDOUT, 3, 15),
                                      CNA_SRC(CNA_TEMP, 0),
                                      CNA_END};
    static const float coordinates[] = {2, 1, -.5f, 9};
    static const int opcodes[] = {83, 85};
    CountingAllocator a;
    TexRegPixels fixture;
    MOJOSHADER_sdlContext *ctx = make_context(gpu_device, &a);
    MOJOSHADER_sdlShaderData *vs = NULL;
    int opcode, destination, source, signedScale, draws = 0;
    if (ctx == NULL)
    {
        CNA_CHECK(0, "dependent dot native context exists");
        return;
    }
    if (!texreg_pixels_start(&fixture, coordinates))
    {
        CNA_CHECK(0, "dependent dot native resources upload");
        goto done;
    }
    vs = compile(ctx, vertex, sizeof(vertex));
    CNA_CHECK(vs != NULL, "distinct per-stage coordinates compile");
    for (opcode = 0; vs != NULL && opcode < 2; opcode++)
        for (destination = 1; destination <= 3; destination++)
            for (source = 0; source < destination; source++)
                for (signedScale = 0; signedScale < 2; signedScale++)
                {
                    const uint32_t words[] = {CNA_PS(1, 2),
                                              CNA_TEX,
                                              CNA_DST(CNA_TEXTURE, source, 15),
                                              opcodes[opcode],
                                              CNA_DST(CNA_TEXTURE, destination, 15),
                                              CNA_SRC(CNA_TEXTURE, source) |
                                                  (signedScale ? 0x04000000u : 0),
                                              CNA_MOV,
                                              CNA_DST(CNA_TEMP, 0, 15),
                                              CNA_SRC(CNA_TEXTURE, destination),
                                              CNA_END};
                    MOJOSHADER_sdlShaderData *ps = compile(ctx, words, sizeof(words));
                    uint8_t expected[4];
                    printf("dependent dot opcode=%d source=%d destination=%d signed=%d\n",
                           opcodes[opcode], source, destination, signedScale);
                    if (opcodes[opcode] == 85)
                        SDL_memset(expected, signedScale ? 0 : 80 + 8 * destination, 4);
                    else
                    {
                        expected[0] = signedScale ? 40 : 80;
                        expected[1] = 40;
                        expected[2] = signedScale ? 7 : 35;
                        expected[3] = 255;
                    }
                    CNA_CHECK(ps != NULL, "dependent dot fragment program compiles");
                    if (ps == NULL)
                        fprintf(stderr, "dependent dot compile: %s\n", MOJOSHADER_sdlGetError(ctx));
                    if (ps != NULL && link_pair(ctx, vs, ps) != NULL)
                    {
                        int rendered =
                            texreg_pixels_draw(&fixture, ctx, vs, ps, destination, 0, expected);
                        CNA_CHECK(rendered, "dependent dot native draw completes");
                        if (!rendered)
                            fprintf(stderr, "%s\n", SDL_GetError());
                        draws += rendered;
                    }
                    MOJOSHADER_sdlDeleteShader(ctx, ps);
                }
    CNA_CHECK(draws == 24, "all dependent dot opcode/register/modifier combinations rendered");
done:
    MOJOSHADER_sdlDeleteShader(ctx, vs);
    texreg_pixels_finish(&fixture);
    MOJOSHADER_sdlDestroyContext(ctx);
    CNA_CHECK(a.live == 0, "dependent dot context returns all allocations");
}
