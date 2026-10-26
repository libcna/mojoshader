/**
 * Direct3D 11 glue regressions (mojoshader_d3d11.c), without Direct3D.
 *
 * The glue is compiled into this file against tests/cna/d3d11-stub/d3d11.h, and talks to a fake
 * device and immediate context that keep each constant buffer's contents in memory. A buffer
 * created without initial data, or mapped with WRITE_DISCARD, starts out as garbage (0xA5), as
 * the real API leaves it undefined. The context is made by hand: MOJOSHADER_d3d11CreateContext()
 * would load a real d3dcompiler, which no case here needs, since none links a program.
 *
 * This file is part of the libcna fork of MojoShader; see tests/cna/README.md.
 */

/* The glue's types and statics; take the translation unit whole, as cna_sdlgpu_tests does. */
#include "../../mojoshader_d3d11.c"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cna_test.h"

#define CNA_TEXBEM 67  /* D3DSIO_TEXBEM */
#define GARBAGE 0xA5

/* ps_1_1: tex t0; texbem t1, t0; mov r0, t1 -- no uniforms, one TEXBEM sampler. */
static const uint32_t texbem_ps11[] = {
    CNA_PS(1, 1),
    CNA_TEX, CNA_DST(CNA_TEXTURE, 0, 0xF),
    CNA_TEXBEM, CNA_DST(CNA_TEXTURE, 1, 0xF), CNA_SRC(CNA_TEXTURE, 0),
    CNA_MOV, CNA_DST(CNA_TEMP, 0, 0xF), CNA_SRC(CNA_TEXTURE, 1),
    CNA_END
};

/* vs_2_0: add oPos, v0, c0 -- one float4 uniform. */
static const uint32_t uniform_vs2[] = {
    CNA_VS(2, 0),
    CNA_OP(CNA_DCL_OP, 2), CNA_DCL(0, 0), CNA_DST(CNA_INPUT, 0, 15),
    CNA_OP(CNA_ADD, 3), CNA_DST(CNA_RASTOUT, 0, 15), CNA_SRC(CNA_INPUT, 0), CNA_SRC(CNA_CONST, 0),
    CNA_END
};

/* -- Fake device and context ---------------------------------------------------------------- */

typedef struct FakeBuffer
{
    ID3D11Buffer iface;  /* first: the glue holds a pointer to this */
    unsigned char contents[1024];
    UINT size;
} FakeBuffer;

static int buffers_live = 0;
static int maps = 0;
static int refuse_buffers = 0;  /* CreateBuffer() fails, as for a lost device */

static ULONG fake_buffer_release(void *This)
{
    buffers_live--;
    free(This);
    return 0;
} // fake_buffer_release

static const ID3D11ReleaseVtbl fake_buffer_vtbl = { fake_buffer_release };

static HRESULT fake_create_buffer(ID3D11Device *This, const D3D11_BUFFER_DESC *desc,
                                  const D3D11_SUBRESOURCE_DATA *initial, ID3D11Buffer **buffer)
{
    FakeBuffer *b;
    (void) This;
    *buffer = NULL;
    if (refuse_buffers || (desc->ByteWidth > sizeof (b->contents)))
        return -1;
    b = (FakeBuffer *) calloc(1, sizeof (FakeBuffer));
    if (b == NULL)
        return -1;
    b->iface.lpVtbl = &fake_buffer_vtbl;
    b->size = desc->ByteWidth;
    /* Without initial data a buffer's contents are undefined. */
    memset(b->contents, GARBAGE, sizeof (b->contents));
    if ((initial != NULL) && (initial->pSysMem != NULL))
        memcpy(b->contents, initial->pSysMem, desc->ByteWidth);
    buffers_live++;
    *buffer = &b->iface;
    return 0;
} // fake_create_buffer

static HRESULT fake_create_vertex_shader(ID3D11Device *This, const void *code, SIZE_T length,
                                         void *linkage, ID3D11VertexShader **shader)
{
    (void) This; (void) code; (void) length; (void) linkage;
    *shader = NULL;
    return -1;
} // fake_create_vertex_shader

static HRESULT fake_create_pixel_shader(ID3D11Device *This, const void *code, SIZE_T length,
                                        void *linkage, ID3D11PixelShader **shader)
{
    (void) This; (void) code; (void) length; (void) linkage;
    *shader = NULL;
    return -1;
} // fake_create_pixel_shader

static const ID3D11DeviceVtbl fake_device_vtbl = {
    fake_create_buffer, fake_create_vertex_shader, fake_create_pixel_shader
};
static ID3D11Device fake_device = { &fake_device_vtbl };

static HRESULT fake_map(ID3D11DeviceContext *This, ID3D11Resource *resource, UINT subresource,
                        UINT type, UINT flags, D3D11_MAPPED_SUBRESOURCE *mapped)
{
    FakeBuffer *b = (FakeBuffer *) resource;
    (void) This; (void) subresource; (void) flags;
    maps++;
    /* WRITE_DISCARD hands out memory whose contents are undefined. */
    if (type == D3D11_MAP_WRITE_DISCARD)
        memset(b->contents, GARBAGE, sizeof (b->contents));
    mapped->pData = b->contents;
    mapped->RowPitch = b->size;
    mapped->DepthPitch = b->size;
    return 0;
} // fake_map

static void fake_unmap(ID3D11DeviceContext *This, ID3D11Resource *resource, UINT subresource)
{
    (void) This; (void) resource; (void) subresource;
} // fake_unmap

static void fake_set_vs(ID3D11DeviceContext *This, ID3D11VertexShader *s, void *c, UINT n)
{ (void) This; (void) s; (void) c; (void) n; }
static void fake_set_ps(ID3D11DeviceContext *This, ID3D11PixelShader *s, void *c, UINT n)
{ (void) This; (void) s; (void) c; (void) n; }
static void fake_set_cb(ID3D11DeviceContext *This, UINT slot, UINT n, ID3D11Buffer *const *b)
{ (void) This; (void) slot; (void) n; (void) b; }

static const ID3D11DeviceContextVtbl fake_context_vtbl = {
    fake_map, fake_unmap, fake_set_vs, fake_set_cb, fake_set_ps, fake_set_cb
};
static ID3D11DeviceContext fake_context = { &fake_context_vtbl };

/* -- Helpers --------------------------------------------------------------------------------- */

/* What MOJOSHADER_d3d11CreateContext() sets up, but without a d3dcompiler. */
static MOJOSHADER_d3d11Context *make_context(void)
{
    MOJOSHADER_d3d11Context *ctx = (MOJOSHADER_d3d11Context *) calloc(1, sizeof (*ctx));
    if (ctx == NULL)
        return NULL;
    ctx->malloc_fn = cna_alloc_malloc;
    ctx->free_fn = cna_alloc_free;
    ctx->device = &fake_device;
    ctx->deviceContext = &fake_context;
    return ctx;
} // make_context

static MOJOSHADER_d3d11Shader *compile(MOJOSHADER_d3d11Context *ctx, const uint32_t *tokens,
                                       size_t bytes)
{
    MOJOSHADER_d3d11Shader *shader = MOJOSHADER_d3d11CompileShader(ctx, "main",
        (const unsigned char *) tokens, (unsigned int) bytes, NULL, 0, NULL, 0);
    if (shader == NULL)
        fprintf(stderr, "MOJOSHADER_d3d11CompileShader: %s\n", MOJOSHADER_d3d11GetError(ctx));
    return shader;
} // compile

/* Whether the fake GPU buffer holds what the glue's staging copy says it does. */
static int gpu_matches_staging(const MOJOSHADER_d3d11Shader *shader)
{
    const FakeBuffer *b = (const FakeBuffer *) shader->ubo;
    return (b != NULL) && (shader->constantData != NULL) && (shader->buflen <= b->size) &&
           (memcmp(b->contents, shader->constantData, shader->buflen) == 0);
} // gpu_matches_staging

/* Binds `shader` alone and runs the update the renderer runs before a draw. */
static void update(MOJOSHADER_d3d11Context *ctx, MOJOSHADER_d3d11Shader *shader)
{
    float *vsf, *psf;
    int *vsi, *psi;
    unsigned char *vsb, *psb;
    ctx->vertexShader = NULL;
    ctx->pixelShader = NULL;
    if (shader->parseData->shader_type == MOJOSHADER_TYPE_VERTEX)
        MOJOSHADER_d3d11BindShaders(ctx, shader, NULL);
    else
        MOJOSHADER_d3d11BindShaders(ctx, NULL, shader);
    MOJOSHADER_d3d11MapUniformBufferMemory(ctx, &vsf, &vsi, &vsb, &psf, &psi, &psb);
    MOJOSHADER_d3d11UnmapUniformBufferMemory(ctx);
} // update

/* -- Cases ----------------------------------------------------------------------------------- */

/*
 * R2-06 (2026-10-26 remediation review): a shader's constant buffer was created without initial
 * data and its staging copy zeroed, and an update maps only what differs from that copy -- so
 * while every uniform and the TEXBEM bump environment were still zero, the first draw read a
 * buffer whose contents Direct3D leaves undefined. The buffer must hold the staging copy from
 * the start, and a later change must still reach it.
 */
static void check_zero_state(const char *what, const uint32_t *tokens, size_t bytes,
                             int texbem)
{
    MOJOSHADER_d3d11Context *ctx;
    MOJOSHADER_d3d11Shader *shader;
    char label[128];

    cna_alloc_reset(0);
    ctx = make_context();
    maps = 0;
    shader = (ctx != NULL) ? compile(ctx, tokens, bytes) : NULL;
    snprintf(label, sizeof (label), "%s: the shader compiles with a constant buffer", what);
    CNA_CHECK((shader != NULL) && (shader->ubo != NULL) && (shader->buflen > 0), label);
    if ((shader == NULL) || (shader->ubo == NULL))
        goto done;

    snprintf(label, sizeof (label), "%s: a new buffer holds the staging copy", what);
    CNA_CHECK(gpu_matches_staging(shader), label);
    update(ctx, shader);
    snprintf(label, sizeof (label), "%s: with every value still zero, so does the first draw's", what);
    CNA_CHECK(gpu_matches_staging(shader), label);

    if (texbem)
        MOJOSHADER_d3d11SetLegacyBumpMapEnv(ctx, 1, 2.0f, 0.0f, 0.0f, 2.0f, 1.0f, 0.5f);
    else
        ctx->vs_reg_file_f[0] = 3.0f;
    update(ctx, shader);
    snprintf(label, sizeof (label), "%s: a changed value is uploaded", what);
    CNA_CHECK((maps == 1) && gpu_matches_staging(shader) &&
              (((const float *) ((const FakeBuffer *) shader->ubo)->contents)[0] ==
               (texbem ? 2.0f : 3.0f)), label);
    update(ctx, shader);
    snprintf(label, sizeof (label), "%s: an unchanged one is not uploaded again", what);
    CNA_CHECK(maps == 1, label);

done:
    if (shader != NULL)
        MOJOSHADER_d3d11DeleteShader(ctx, shader);
    snprintf(label, sizeof (label), "%s: everything is released", what);
    CNA_CHECK((buffers_live == 0) && (cna_alloc_live() == 0), label);
    free(ctx);
} // check_zero_state

static void test_texbem_zero_state(void)
{
    check_zero_state("ps_1_1 TEXBEM", texbem_ps11, sizeof (texbem_ps11), 1);
} // test_texbem_zero_state

static void test_uniform_zero_state(void)
{
    check_zero_state("vs_2_0 c0", uniform_vs2, sizeof (uniform_vs2), 0);
} // test_uniform_zero_state

/* A shader whose CreateBuffer() failed has no buffer but a staging copy, which deleting the
 * shader must free too. */
static void test_refused_buffer_frees_staging(void)
{
    MOJOSHADER_d3d11Context *ctx;
    MOJOSHADER_d3d11Shader *shader;

    cna_alloc_reset(0);
    ctx = make_context();
    maps = 0;
    refuse_buffers = 1;
    shader = (ctx != NULL) ? compile(ctx, uniform_vs2, sizeof (uniform_vs2)) : NULL;
    refuse_buffers = 0;
    CNA_CHECK((shader != NULL) && (shader->ubo == NULL), "the shader compiles without a buffer");
    if (shader != NULL)
    {
        update(ctx, shader);  /* no buffer: nothing to map */
        CNA_CHECK(maps == 0, "nothing is mapped");
        MOJOSHADER_d3d11DeleteShader(ctx, shader);
    } // if
    if (cna_alloc_live() != 0)
        fprintf(stderr, "  %d allocation(s) outlive the shader\n", cna_alloc_live());
    CNA_CHECK((buffers_live == 0) && (cna_alloc_live() == 0), "everything is released");
    free(ctx);
} // test_refused_buffer_frees_staging

int main(void)
{
    cna_test_run("texbem_zero_state", test_texbem_zero_state);
    cna_test_run("uniform_zero_state", test_uniform_zero_state);
    cna_test_run("refused_buffer_frees_staging", test_refused_buffer_frees_staging);
    return cna_test_finish();
} // main
