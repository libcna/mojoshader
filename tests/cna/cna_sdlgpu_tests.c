/**
 * SDL_GPU glue regressions (mojoshader_sdlgpu.c).
 *
 * Two kinds of case:
 *
 * - Without a GPU device, run everywhere. A context made for no device (NULL) finds no native
 *   shader format, so it takes the dynamic SDL_shadercross path; tests/cna builds a stand-in
 *   libSDL3_shadercross.so.0 for it (cna_sdlgpu_shadercross_stub.c) and puts it on the test's
 *   library path. Shaders compile (they are only parsed); linking ends in the stand-in, which
 *   compiles nothing.
 *
 * - With a GPU device, opt-in: set MOJOSHADER_CNA_SDLGPU_DEVICE=1. These create a real
 *   SDL_GPUDevice that takes SPIR-V (Vulkan, on Linux) and link real programs. Run them on a
 *   private display, never on a desktop session someone is using; the device name and driver
 *   are printed first. Without the variable they are reported as skipped. Under
 *   AddressSanitizer, run them with ASAN_OPTIONS=detect_leaks=0: Vulkan drivers keep
 *   allocations for the process that LeakSanitizer reports once SDL_gpu has unloaded them.
 *   MojoShader's own are still checked -- every context here allocates through a counting
 *   allocator, and each case requires it to return everything.
 *
 * This file is part of the libcna fork of MojoShader; see tests/cna/README.md.
 */

/* The glue's types and statics; take the translation unit whole, as cna_opengl_tests does. */
#include "../../mojoshader_sdlgpu.c"

#include <stdio.h>
#include "cna_test.h"

#if defined(__linux__)
#undef dlopen  /* mojoshader_internal.h maps these to SDL's, which has no RTLD_NOLOAD */
#undef dlclose
#undef dlsym
#include <dlfcn.h>
#define CNA_HAVE_RTLD_NOLOAD 1
#endif

#define CNA_TEXBEM 67  /* D3DSIO_TEXBEM */

/* vs_2_0 writing only oPos. */
static const uint32_t position_vs2[] = {
    CNA_VS(2, 0),
    CNA_OP(CNA_DCL_OP, 2), CNA_DCL(0, 0), CNA_DST(CNA_INPUT, 0, 15),
    CNA_OP(CNA_MOV, 2), CNA_DST(CNA_RASTOUT, 0, 15), CNA_SRC(CNA_INPUT, 0),
    CNA_END
};

/* vs_2_0 reading one uniform: add oPos, v0, c0. */
static const uint32_t uniform_vs2[] = {
    CNA_VS(2, 0),
    CNA_OP(CNA_DCL_OP, 2), CNA_DCL(0, 0), CNA_DST(CNA_INPUT, 0, 15),
    CNA_OP(CNA_ADD, 3), CNA_DST(CNA_RASTOUT, 0, 15), CNA_SRC(CNA_INPUT, 0), CNA_SRC(CNA_CONST, 0),
    CNA_END
};

/* ps_2_0 writing a constant colour: def c0, r, g, b, 1; mov oC0, c0. */
#define CNA_COLOR_PS2(r, g, b) { \
    CNA_PS(2, 0), \
    CNA_OP(CNA_DEF, 5), CNA_DST(CNA_CONST, 0, 15), (r), (g), (b), 0x3f800000u, \
    CNA_OP(CNA_MOV, 2), CNA_DST(CNA_COLOROUT, 0, 15), CNA_SRC(CNA_CONST, 0), \
    CNA_END }
static const uint32_t red_ps2[] = CNA_COLOR_PS2(0x3f800000u, 0, 0);
static const uint32_t green_ps2[] = CNA_COLOR_PS2(0, 0x3f800000u, 0);

/* ps_1_1: tex t0; texbem t1, t0; mov r0, t1. */
static const uint32_t texbem_ps11[] = {
    CNA_PS(1, 1),
    CNA_TEX, CNA_DST(CNA_TEXTURE, 0, 0xF),
    CNA_TEXBEM, CNA_DST(CNA_TEXTURE, 1, 0xF), CNA_SRC(CNA_TEXTURE, 0),
    CNA_MOV, CNA_DST(CNA_TEMP, 0, 0xF), CNA_SRC(CNA_TEXTURE, 1),
    CNA_END
};

/* -- Allocator ----------------------------------------------------------------------------- */

/*
 * Every context here allocates through this, so a case can tell that everything it made was
 * returned, and can make one chosen allocation fail.
 */
typedef struct CountingAllocator
{
    long live;        /* allocations not freed yet */
    long fail_after;  /* the allocation after this many more fails, once; -1: none */
    long failed;      /* allocations refused so far */
} CountingAllocator;

static void *MOJOSHADERCALL counting_malloc(int bytes, void *data)
{
    CountingAllocator *a = (CountingAllocator *) data;
    void *ptr;
    if (a->fail_after == 0)
    {
        a->fail_after = -1;
        a->failed++;
        return NULL;
    } // if
    if (a->fail_after > 0)
        a->fail_after--;
    ptr = SDL_malloc((size_t) bytes);
    if (ptr != NULL)
        a->live++;
    return ptr;
} // counting_malloc

static void MOJOSHADERCALL counting_free(void *ptr, void *data)
{
    CountingAllocator *a = (CountingAllocator *) data;
    if (ptr == NULL)
        return;
    a->live--;
    SDL_free(ptr);
} // counting_free

static void reset_allocator(CountingAllocator *a)
{
    a->live = 0;
    a->fail_after = -1;
    a->failed = 0;
} // reset_allocator

/* -- Helpers ------------------------------------------------------------------------------- */

static SDL_GPUDevice *gpu_device = NULL;

static MOJOSHADER_sdlContext *make_context(SDL_GPUDevice *device, CountingAllocator *a)
{
    MOJOSHADER_sdlContext *ctx;
    reset_allocator(a);
    ctx = MOJOSHADER_sdlCreateContext(device, counting_malloc, counting_free, a);
    if (ctx == NULL)
        fprintf(stderr, "MOJOSHADER_sdlCreateContext: %s\n", MOJOSHADER_sdlGetError(NULL));
    return ctx;
} // make_context

static MOJOSHADER_sdlShaderData *compile(MOJOSHADER_sdlContext *ctx, const uint32_t *tokens,
                                         size_t bytes)
{
    MOJOSHADER_sdlShaderData *shader = MOJOSHADER_sdlCompileShader(ctx, "main",
        (const unsigned char *) tokens, (unsigned int) bytes, NULL, 0, NULL, 0);
    if (shader == NULL)
        fprintf(stderr, "MOJOSHADER_sdlCompileShader: %s\n", MOJOSHADER_sdlGetError(ctx));
    return shader;
} // compile

static MOJOSHADER_sdlProgram *link_pair(MOJOSHADER_sdlContext *ctx,
                                        MOJOSHADER_sdlShaderData *vs,
                                        MOJOSHADER_sdlShaderData *ps)
{
    MOJOSHADER_vertexAttribute position;
    memset(&position, '\0', sizeof (position));
    position.usage = MOJOSHADER_USAGE_POSITION;
    position.vertexElementFormat = MOJOSHADER_VERTEXELEMENTFORMAT_VECTOR4;
    position.usageIndex = 0;
    MOJOSHADER_sdlBindShaders(ctx, vs, ps);
    return MOJOSHADER_sdlLinkProgram(ctx, &position, 1);
} // link_pair

static int error_contains(const char *text)
{
    const char *error = MOJOSHADER_sdlGetError(NULL);
    return (error != NULL) && (strstr(error, text) != NULL);
} // error_contains

/* True when the stand-in ShaderCross is the one on the library path (see CMakeLists.txt). */
static int shadercross_is_stub(void)
{
    const char *stub = SDL_getenv("MOJOSHADER_CNA_SHADERCROSS_STUB");
    return (stub != NULL) && (strcmp(stub, "1") == 0);
} // shadercross_is_stub

/* -- Cases without a device ---------------------------------------------------------------- */

/*
 * A context for no device: shaders compile and are returned, MOJOSHADER_sdlGetShaderFormats()
 * reports what the dynamic ShaderCross offers, and a link ends in ShaderCross without crashing.
 */
static void test_context_without_device(void)
{
    CountingAllocator a;
    MOJOSHADER_sdlContext *ctx = make_context(NULL, &a);
    MOJOSHADER_sdlShaderData *vs, *ps;
    unsigned int formats = MOJOSHADER_sdlGetShaderFormats();

    CNA_CHECK(formats & SDL_GPU_SHADERFORMAT_SPIRV, "SPIR-V is always a format");
    if (shadercross_is_stub())
        CNA_CHECK(formats & SDL_GPU_SHADERFORMAT_MSL, "the stand-in ShaderCross adds its formats");

    CNA_CHECK(ctx != NULL, "a context for no device is made");
    if (ctx == NULL)
        return;
    vs = compile(ctx, position_vs2, sizeof (position_vs2));
    ps = compile(ctx, red_ps2, sizeof (red_ps2));
    CNA_CHECK((vs != NULL) && (ps != NULL), "both shaders compile");

    if ((vs != NULL) && (ps != NULL) && MOJOSHADER_sdlSetShaderCrossTestMode(ctx, 1))
    {
        CNA_CHECK(link_pair(ctx, vs, ps) == NULL, "no program without a device");
        if (shadercross_is_stub())
            CNA_CHECK(error_contains("cna shadercross stub: compiled no vertex shader"),
                      "the link reached ShaderCross's compiler");
    } // if
    else if (!shadercross_is_stub())
        printf("  (no SDL3_shadercross library: link through ShaderCross not exercised)\n");
    else
        CNA_CHECK(0, "the stand-in ShaderCross is loaded");

    MOJOSHADER_sdlDeleteShader(ctx, vs);
    MOJOSHADER_sdlDeleteShader(ctx, ps);
    MOJOSHADER_sdlDestroyContext(ctx);
    CNA_CHECK(a.live == 0, "the context returned everything it allocated");
} // test_context_without_device

/* A2v2 A-14: a context's last shader tag is refused at the end of the space, not wrapped. */
static void test_shader_identity_exhaustion(void)
{
    CountingAllocator a;
    MOJOSHADER_sdlContext *ctx = make_context(NULL, &a);
    MOJOSHADER_sdlShaderData *last, *refused;
    long live;

    if (ctx == NULL)
    {
        CNA_CHECK(0, "a context is made");
        return;
    } // if
    ctx->next_shader_tag = ~0ULL - 1;  // the white-box way to the end of the space
    last = compile(ctx, red_ps2, sizeof (red_ps2));
    CNA_CHECK((last != NULL) && (last->tag == ~0ULL), "the last tag is given out");
    live = a.live;
    refused = MOJOSHADER_sdlCompileShader(ctx, "main", (const unsigned char *) red_ps2,
                                          sizeof (red_ps2), NULL, 0, NULL, 0);
    CNA_CHECK(refused == NULL, "no shader once the tags are exhausted");
    CNA_CHECK(error_contains("shader identity space exhausted"), "... with the reason");
    CNA_CHECK(a.live == live, "... allocating nothing");
    MOJOSHADER_sdlDeleteShader(ctx, last);
    MOJOSHADER_sdlDestroyContext(ctx);
    CNA_CHECK(a.live == 0, "the context returned everything it allocated");
} // test_shader_identity_exhaustion

/*
 * Links a pair in a context that uses ShaderCross and no device: the link fails, but only in
 * ShaderCross's compiler, called through the context's pointers.
 */
static void check_link_reaches_shadercross(MOJOSHADER_sdlContext *ctx, const char *label)
{
    MOJOSHADER_sdlShaderData *vs = compile(ctx, position_vs2, sizeof (position_vs2));
    MOJOSHADER_sdlShaderData *ps = compile(ctx, red_ps2, sizeof (red_ps2));
    CNA_CHECK(vs && ps, label);
    if (vs && ps)
    {
        CNA_CHECK(link_pair(ctx, vs, ps) == NULL, label);
        if (shadercross_is_stub())
            CNA_CHECK(error_contains("cna shadercross stub: compiled no vertex shader"), label);
        else
            printf("  (%s: %s)\n", label, MOJOSHADER_sdlGetError(NULL));
    } // if
    MOJOSHADER_sdlDeleteShader(ctx, vs);
    MOJOSHADER_sdlDeleteShader(ctx, ps);
} // check_link_reaches_shadercross

/* True when the ShaderCross library is still loaded into the process (-1: cannot tell). */
static int shadercross_loaded(void)
{
#ifdef CNA_HAVE_RTLD_NOLOAD
    void *handle = dlopen(SDL_SHADERCROSS_LIB_NAME, RTLD_NOW | RTLD_NOLOAD);
    if (handle == NULL)
        return 0;
    dlclose(handle);
    return 1;
#else
    return -1;
#endif
} // shadercross_loaded

/*
 * A2v2 A-15: the dynamic ShaderCross library and its function pointers are the process's, the
 * decision to use them each context's. Destroying one context that uses it must leave them to
 * another that still does, and the last one to go unloads the library.
 */
static void test_shadercross_outlives_another_context(void)
{
    CountingAllocator a, b;
    MOJOSHADER_sdlContext *ctx_a = make_context(NULL, &a);
    MOJOSHADER_sdlContext *ctx_b = make_context(NULL, &b);
    MOJOSHADER_sdlContext *ctx_c;

    CNA_CHECK((ctx_a != NULL) && (ctx_b != NULL), "two contexts without a device");
    if ((ctx_a == NULL) || (ctx_b == NULL))
        goto done;
    if (!MOJOSHADER_sdlSetShaderCrossTestMode(ctx_b, 1))
    {
        if (shadercross_is_stub())
            CNA_CHECK(0, "the stand-in ShaderCross is loaded");
        else
            printf("  (no SDL3_shadercross library: not exercised)\n");
        goto done;
    } // if

    MOJOSHADER_sdlDestroyContext(ctx_a);
    ctx_a = NULL;
    CNA_CHECK(a.live == 0, "the first context returned everything it allocated");
    CNA_CHECK(shadercross_loaded() != 0, "the library stays loaded for the second context");
    check_link_reaches_shadercross(ctx_b, "the second context still links through ShaderCross");

    MOJOSHADER_sdlDestroyContext(ctx_b);
    ctx_b = NULL;
    CNA_CHECK(b.live == 0, "the second context returned everything it allocated");
    CNA_CHECK(shadercross_loaded() != 1, "the last context unloads the library");

    ctx_c = make_context(NULL, &a);
    CNA_CHECK((ctx_c != NULL) && MOJOSHADER_sdlSetShaderCrossTestMode(ctx_c, 1),
              "a new context loads it again");
    if (ctx_c != NULL)
    {
        check_link_reaches_shadercross(ctx_c, "the new context links through ShaderCross");
        MOJOSHADER_sdlDestroyContext(ctx_c);
        CNA_CHECK(a.live == 0, "the new context returned everything it allocated");
    } // if
    CNA_CHECK(shadercross_loaded() != 1, "... and unloads it");

done:
    if (ctx_a != NULL)
        MOJOSHADER_sdlDestroyContext(ctx_a);
    if (ctx_b != NULL)
        MOJOSHADER_sdlDestroyContext(ctx_b);
} // test_shadercross_outlives_another_context

/* -- Cases with a device ------------------------------------------------------------------- */

/* Linking caches by pair; the identity and the GPU shaders come from the bound program. */
static void test_link_and_cache(void)
{
    CountingAllocator a;
    MOJOSHADER_sdlContext *ctx = make_context(gpu_device, &a);
    MOJOSHADER_sdlShaderData *vs, *red, *green;
    MOJOSHADER_sdlProgram *p1, *p2, *p3;
    SDL_GPUShader *gvs = NULL, *gps = NULL;
    unsigned long long id1;

    if (ctx == NULL)
    {
        CNA_CHECK(0, "a context is made");
        return;
    } // if
    vs = compile(ctx, position_vs2, sizeof (position_vs2));
    red = compile(ctx, red_ps2, sizeof (red_ps2));
    green = compile(ctx, green_ps2, sizeof (green_ps2));
    CNA_CHECK(vs && red && green, "the shaders compile");
    if (vs && red && green)
    {
        p1 = link_pair(ctx, vs, red);
        CNA_CHECK(p1 != NULL, "the pair links");
        id1 = MOJOSHADER_sdlGetProgramIdentity(ctx);
        CNA_CHECK(id1 != 0, "a linked program has an identity");
        MOJOSHADER_sdlGetShaders(ctx, &gvs, &gps);
        CNA_CHECK((gvs != NULL) && (gps != NULL), "a linked program has both GPU shaders");

        p2 = link_pair(ctx, vs, green);
        CNA_CHECK((p2 != NULL) && (p2 != p1), "another pixel shader links another program");
        CNA_CHECK(MOJOSHADER_sdlGetProgramIdentity(ctx) != id1, "... with another identity");

        p3 = link_pair(ctx, vs, red);
        CNA_CHECK(p3 == p1, "relinking a pair returns its cached program");
        CNA_CHECK(MOJOSHADER_sdlGetProgramIdentity(ctx) == id1, "... with its identity");
    } // if

    MOJOSHADER_sdlDeleteShader(ctx, vs);
    MOJOSHADER_sdlDeleteShader(ctx, red);
    MOJOSHADER_sdlDeleteShader(ctx, green);
    MOJOSHADER_sdlDestroyContext(ctx);
    CNA_CHECK(a.live == 0, "the context returned everything it allocated");
} // test_link_and_cache

/* A ps_1_x TEXBEM's bump environment follows its uniforms in the staged buffer (0c671bc). */
static void test_texbem_uniforms(void)
{
    CountingAllocator a;
    MOJOSHADER_sdlContext *ctx = make_context(gpu_device, &a);
    MOJOSHADER_sdlShaderData *vs, *ps;
    SDL_GPUCommandBuffer *cb;
    static const float expected[8] = { 0.5f, -0.25f, 0.125f, 2.0f, 1.5f, -0.75f, 0.0f, 0.0f };

    if (ctx == NULL)
    {
        CNA_CHECK(0, "a context is made");
        return;
    } // if
    vs = compile(ctx, position_vs2, sizeof (position_vs2));
    ps = compile(ctx, texbem_ps11, sizeof (texbem_ps11));
    CNA_CHECK(vs && ps, "the shaders compile");
    if (vs && ps)
    {
        CNA_CHECK(MOJOSHADER_sdlGetUniformBufferSize(ps) == 32,
                  "one TEXBEM sampler adds two float4 registers");
        CNA_CHECK(link_pair(ctx, vs, ps) != NULL, "the pair links");
        MOJOSHADER_sdlSetLegacyBumpMapEnv(ctx, 1, expected[0], expected[1], expected[2],
                                          expected[3], expected[4], expected[5]);
        cb = SDL_AcquireGPUCommandBuffer(gpu_device);
        CNA_CHECK(cb != NULL, "a command buffer");
        if ((cb != NULL) && (ctx->bound_program != NULL))
        {
            MOJOSHADER_sdlUpdateUniformBuffers(ctx, cb);
            CNA_CHECK((ctx->uniform_staging != NULL) &&
                      (memcmp(ctx->uniform_staging, expected, sizeof (expected)) == 0),
                      "the staged pixel uniforms are stage 1's bump matrix, scale and offset");
            SDL_SubmitGPUCommandBuffer(cb);
        } // if
    } // if

    MOJOSHADER_sdlDeleteShader(ctx, vs);
    MOJOSHADER_sdlDeleteShader(ctx, ps);
    MOJOSHADER_sdlDestroyContext(ctx);
    CNA_CHECK(a.live == 0, "the context returned everything it allocated");
} // test_texbem_uniforms

/*
 * A2v2 A-08: a program deleted with MOJOSHADER_sdlDeleteProgram() must leave the context's
 * program cache too. Relinking the pair links a new program rather than returning the freed one.
 */
static void test_delete_program_then_relink(void)
{
    CountingAllocator a;
    MOJOSHADER_sdlContext *ctx = make_context(gpu_device, &a);
    MOJOSHADER_sdlShaderData *vs, *ps;
    MOJOSHADER_sdlProgram *p1, *p2;
    SDL_GPUShader *gvs = NULL, *gps = NULL;
    unsigned long long id1;

    if (ctx == NULL)
    {
        CNA_CHECK(0, "a context is made");
        return;
    } // if
    vs = compile(ctx, position_vs2, sizeof (position_vs2));
    ps = compile(ctx, red_ps2, sizeof (red_ps2));
    CNA_CHECK(vs && ps, "the shaders compile");
    if (vs && ps)
    {
        p1 = link_pair(ctx, vs, ps);
        CNA_CHECK(p1 != NULL, "the pair links");
        id1 = MOJOSHADER_sdlGetProgramIdentity(ctx);
        if (p1 != NULL)
            MOJOSHADER_sdlDeleteProgram(ctx, p1);
        CNA_CHECK(MOJOSHADER_sdlGetProgramIdentity(ctx) == 0, "the deleted program is unbound");

        p2 = link_pair(ctx, vs, ps);
        CNA_CHECK(p2 != NULL, "the pair links again");
        CNA_CHECK(MOJOSHADER_sdlGetProgramIdentity(ctx) != id1,
                  "relinking after the deletion links a new program");
        MOJOSHADER_sdlGetShaders(ctx, &gvs, &gps);
        CNA_CHECK((gvs != NULL) && (gps != NULL), "the new program has both GPU shaders");
        CNA_CHECK(link_pair(ctx, vs, ps) == p2, "the new program is the cached one");
    } // if

    MOJOSHADER_sdlDeleteShader(ctx, vs);
    MOJOSHADER_sdlDeleteShader(ctx, ps);
    MOJOSHADER_sdlDestroyContext(ctx);
    CNA_CHECK(a.live == 0, "the context returned everything it allocated");
} // test_delete_program_then_relink

/* A2v2 A-08: deleting a shader of an explicitly deleted program evicts nothing twice. */
static void test_delete_program_then_delete_shaders(void)
{
    CountingAllocator a;
    MOJOSHADER_sdlContext *ctx = make_context(gpu_device, &a);
    MOJOSHADER_sdlShaderData *vs, *red, *green;
    MOJOSHADER_sdlProgram *p_red, *p_green;

    if (ctx == NULL)
    {
        CNA_CHECK(0, "a context is made");
        return;
    } // if
    vs = compile(ctx, position_vs2, sizeof (position_vs2));
    red = compile(ctx, red_ps2, sizeof (red_ps2));
    green = compile(ctx, green_ps2, sizeof (green_ps2));
    CNA_CHECK(vs && red && green, "the shaders compile");
    if (vs && red && green)
    {
        p_red = link_pair(ctx, vs, red);
        p_green = link_pair(ctx, vs, green);
        CNA_CHECK((p_red != NULL) && (p_green != NULL), "both pairs link");
        if (p_red != NULL)
            MOJOSHADER_sdlDeleteProgram(ctx, p_red);
        CNA_CHECK(MOJOSHADER_sdlGetProgramIdentity(ctx) != 0,
                  "deleting another program leaves the bound one bound");
    } // if

    /* mojoshader.h's order: programs, then shaders, then the context. */
    MOJOSHADER_sdlDeleteShader(ctx, red);
    MOJOSHADER_sdlDeleteShader(ctx, green);
    MOJOSHADER_sdlDeleteShader(ctx, vs);
    MOJOSHADER_sdlDestroyContext(ctx);
    CNA_CHECK(a.live == 0, "the context returned everything it allocated");
} // test_delete_program_then_delete_shaders

/* Returns what a shader the context did not free holds (mojoshader.h: it frees none). */
static void free_orphan_shader(MOJOSHADER_sdlShaderData *shader, CountingAllocator *a)
{
    if (shader == NULL)
        return;
    MOJOSHADER_freeParseData(shader->parseData);
    counting_free(shader, a);
} // free_orphan_shader

/* A2v2 A-08: tearing down the cache does not delete an explicitly deleted program again. */
static void test_delete_program_then_destroy_context(void)
{
    CountingAllocator a;
    MOJOSHADER_sdlContext *ctx = make_context(gpu_device, &a);
    MOJOSHADER_sdlShaderData *vs, *ps;
    MOJOSHADER_sdlProgram *p;

    if (ctx == NULL)
    {
        CNA_CHECK(0, "a context is made");
        return;
    } // if
    vs = compile(ctx, position_vs2, sizeof (position_vs2));
    ps = compile(ctx, red_ps2, sizeof (red_ps2));
    CNA_CHECK(vs && ps, "the shaders compile");
    if (vs && ps)
    {
        p = link_pair(ctx, vs, ps);
        CNA_CHECK(p != NULL, "the pair links");
        if (p != NULL)
            MOJOSHADER_sdlDeleteProgram(ctx, p);
    } // if

    MOJOSHADER_sdlDestroyContext(ctx);  // with both shaders alive: the cache is torn down
    free_orphan_shader(vs, &a);
    free_orphan_shader(ps, &a);
    CNA_CHECK(a.live == 0, "everything allocated was returned");
} // test_delete_program_then_destroy_context

/*
 * A2v2 A-14: a shader compiled 65536 shaders after another is a different shader, whatever a
 * 16-bit tag says. Linking it must not return the other's cached program.
 */
static void test_shader_identity_after_65536_compiles(void)
{
    CountingAllocator a;
    MOJOSHADER_sdlContext *ctx = make_context(gpu_device, &a);
    MOJOSHADER_sdlShaderData *vs, *red, *green, *other;
    MOJOSHADER_sdlProgram *p_red, *p_green;
    SDL_GPUShader *g_red = NULL, *g_green = NULL;
    unsigned long long id_red, id_green;
    int i, compiled = 0;

    if (ctx == NULL)
    {
        CNA_CHECK(0, "a context is made");
        return;
    } // if
    vs = compile(ctx, position_vs2, sizeof (position_vs2));
    red = compile(ctx, red_ps2, sizeof (red_ps2));
    CNA_CHECK(vs && red, "the shaders compile");
    if (vs && red)
    {
        p_red = link_pair(ctx, vs, red);
        CNA_CHECK(p_red != NULL, "vs + red links");
        id_red = MOJOSHADER_sdlGetProgramIdentity(ctx);
        MOJOSHADER_sdlGetShaders(ctx, NULL, &g_red);

        /* 65535 shaders more, so that green is the 65536th compiled after red. */
        for (i = 0; i < 65535; i++)
        {
            other = MOJOSHADER_sdlCompileShader(ctx, "main", (const unsigned char *) green_ps2,
                                                sizeof (green_ps2), NULL, 0, NULL, 0);
            if (other == NULL)
                break;
            compiled++;
            MOJOSHADER_sdlDeleteShader(ctx, other);
        } // for
        CNA_CHECK(compiled == 65535, "65535 shaders compile");
        green = compile(ctx, green_ps2, sizeof (green_ps2));
        CNA_CHECK(green != NULL, "green compiles");
        if (green != NULL)
        {
            p_green = link_pair(ctx, vs, green);
            id_green = MOJOSHADER_sdlGetProgramIdentity(ctx);
            CNA_CHECK((p_green != NULL) && (p_green != p_red),
                      "vs + green links its own program, not vs + red's");
            CNA_CHECK(id_green != id_red, "... with its own identity");
            MOJOSHADER_sdlGetShaders(ctx, NULL, &g_green);
            CNA_CHECK(g_green != g_red, "... and its own GPU pixel shader");

            MOJOSHADER_sdlDeleteShader(ctx, red);
            red = NULL;
            CNA_CHECK((link_pair(ctx, vs, green) == p_green) &&
                      (MOJOSHADER_sdlGetProgramIdentity(ctx) == id_green),
                      "deleting red leaves vs + green's program cached");
            MOJOSHADER_sdlDeleteShader(ctx, green);
        } // if
    } // if

    MOJOSHADER_sdlDeleteShader(ctx, vs);
    MOJOSHADER_sdlDeleteShader(ctx, red);
    MOJOSHADER_sdlDestroyContext(ctx);
    CNA_CHECK(a.live == 0, "the context returned everything it allocated");
} // test_shader_identity_after_65536_compiles

/* A2v2 A-14: tags that differ only above bit 32 are different shaders too. */
static void test_shader_identity_high_bits(void)
{
    CountingAllocator a;
    MOJOSHADER_sdlContext *ctx = make_context(gpu_device, &a);
    MOJOSHADER_sdlShaderData *vs, *red, *green;
    MOJOSHADER_sdlProgram *p_red, *p_green;

    if (ctx == NULL)
    {
        CNA_CHECK(0, "a context is made");
        return;
    } // if
    vs = compile(ctx, position_vs2, sizeof (position_vs2));
    red = compile(ctx, red_ps2, sizeof (red_ps2));
    CNA_CHECK(vs && red, "the shaders compile");
    if (vs && red)
    {
        p_red = link_pair(ctx, vs, red);
        CNA_CHECK(p_red != NULL, "vs + red links");
        ctx->next_shader_tag = red->tag + 0x100000000ULL - 1;  // white box, as above
        green = compile(ctx, green_ps2, sizeof (green_ps2));
        CNA_CHECK((green != NULL) && (green->tag == red->tag + 0x100000000ULL),
                  "green's tag differs from red's above bit 32 only");
        if (green != NULL)
        {
            p_green = link_pair(ctx, vs, green);
            CNA_CHECK((p_green != NULL) && (p_green != p_red),
                      "vs + green links its own program");
            CNA_CHECK(link_pair(ctx, vs, red) == p_red, "vs + red's stays cached");
            MOJOSHADER_sdlDeleteShader(ctx, green);
        } // if
    } // if

    MOJOSHADER_sdlDeleteShader(ctx, vs);
    MOJOSHADER_sdlDeleteShader(ctx, red);
    MOJOSHADER_sdlDestroyContext(ctx);
    CNA_CHECK(a.live == 0, "the context returned everything it allocated");
} // test_shader_identity_high_bits

/*
 * A2v2 A-15: a context on a device that takes SPIR-V never loads ShaderCross, so destroying it
 * must not unload the library from under a context that uses it.
 */
static void test_shadercross_outlives_native_context(void)
{
    CountingAllocator a, b;
    MOJOSHADER_sdlContext *native = make_context(gpu_device, &a);
    MOJOSHADER_sdlContext *cross = make_context(NULL, &b);

    CNA_CHECK((native != NULL) && (cross != NULL), "a native and a ShaderCross context");
    if ((native != NULL) && (cross != NULL))
    {
        CNA_CHECK(!native->use_shadercross, "the device's context compiles SPIR-V natively");
        if (MOJOSHADER_sdlSetShaderCrossTestMode(cross, 1))
        {
            MOJOSHADER_sdlDestroyContext(native);
            native = NULL;
            check_link_reaches_shadercross(cross, "the ShaderCross context still links");
        } // if
        else if (shadercross_is_stub())
            CNA_CHECK(0, "the stand-in ShaderCross is loaded");
        else
            printf("  (no SDL3_shadercross library: not exercised)\n");
    } // if
    if (native != NULL)
        MOJOSHADER_sdlDestroyContext(native);
    if (cross != NULL)
        MOJOSHADER_sdlDestroyContext(cross);
    CNA_CHECK((a.live == 0) && (b.live == 0), "both contexts returned everything they allocated");
} // test_shadercross_outlives_native_context

/*
 * A2v2 A-06: whichever allocation of a link is refused -- the cache, the program, its cache key,
 * the cache entry -- the link fails cleanly or succeeds, the next link of the pair succeeds, and
 * everything is returned. The sweep refuses the first allocation of the link, then the second,
 * and so on, each in a fresh context, until a link refuses none.
 */
static void test_link_allocation_failures(void)
{
    CountingAllocator a;
    MOJOSHADER_sdlContext *ctx;
    MOJOSHADER_sdlShaderData *vs, *ps;
    MOJOSHADER_sdlProgram *p;
    char label[128];
    int k, refused = 1;

    for (k = 0; refused && (k < 64); k++)
    {
        ctx = make_context(gpu_device, &a);
        if (ctx == NULL)
        {
            CNA_CHECK(0, "a context is made");
            return;
        } // if
        vs = compile(ctx, position_vs2, sizeof (position_vs2));
        ps = compile(ctx, red_ps2, sizeof (red_ps2));
        CNA_CHECK(vs && ps, "the shaders compile");
        if (vs && ps)
        {
            a.fail_after = k;
            p = link_pair(ctx, vs, ps);
            a.fail_after = -1;
            refused = (a.failed > 0);
            snprintf(label, sizeof (label), "allocation %d refused: the pair links on retry", k);
            if (!refused)
                CNA_CHECK(p != NULL, "with no allocation refused, the pair links");
            else
                CNA_CHECK(link_pair(ctx, vs, ps) != NULL, label);
            if (refused && (p == NULL))
            {
                snprintf(label, sizeof (label), "allocation %d refused: out of memory", k);
                CNA_CHECK(error_contains("out of memory"), label);
            } // if
        } // if
        MOJOSHADER_sdlDeleteShader(ctx, vs);
        MOJOSHADER_sdlDeleteShader(ctx, ps);
        MOJOSHADER_sdlDestroyContext(ctx);
        snprintf(label, sizeof (label), "allocation %d refused: everything returned", k);
        CNA_CHECK(a.live == 0, label);
    } // for
    CNA_CHECK(!refused, "the sweep reached a link that refused nothing");
    printf("  (a link allocated %d time(s))\n", k - 1);
} // test_link_allocation_failures

/* A2v2 A-06 (fixed in 0c671bc): a refused uniform staging buffer stages and pushes nothing. */
static void test_uniform_staging_allocation_failure(void)
{
    CountingAllocator a;
    MOJOSHADER_sdlContext *ctx = make_context(gpu_device, &a);
    MOJOSHADER_sdlShaderData *vs, *ps;
    SDL_GPUCommandBuffer *cb;
    float *vsf, *psf;
    int *vsi, *psi;
    unsigned char *vsb, *psb;
    static const float c0[4] = { 1.0f, 2.0f, 3.0f, 4.0f };

    if (ctx == NULL)
    {
        CNA_CHECK(0, "a context is made");
        return;
    } // if
    vs = compile(ctx, uniform_vs2, sizeof (uniform_vs2));
    ps = compile(ctx, red_ps2, sizeof (red_ps2));
    CNA_CHECK(vs && ps, "the shaders compile");
    if (vs && ps)
    {
        CNA_CHECK((MOJOSHADER_sdlGetUniformBufferSize(vs) == 16) &&
                  (MOJOSHADER_sdlGetUniformBufferSize(ps) == 0),
                  "only the vertex shader has uniforms");
        CNA_CHECK(link_pair(ctx, vs, ps) != NULL, "the pair links");
        cb = SDL_AcquireGPUCommandBuffer(gpu_device);
        CNA_CHECK(cb != NULL, "a command buffer");
        if ((cb != NULL) && (ctx->bound_program != NULL))
        {
            MOJOSHADER_sdlMapUniformBufferMemory(ctx, &vsf, &vsi, &vsb, &psf, &psi, &psb);
            memcpy(vsf, c0, sizeof (c0));
            MOJOSHADER_sdlUnmapUniformBufferMemory(ctx);

            a.fail_after = 0;
            MOJOSHADER_sdlUpdateUniformBuffers(ctx, cb);
            CNA_CHECK(a.failed == 1, "the staging buffer's allocation is refused");
            CNA_CHECK((ctx->uniform_staging == NULL) && (ctx->uniform_staging_length == 0),
                      "... and nothing is staged");

            MOJOSHADER_sdlUpdateUniformBuffers(ctx, cb);
            CNA_CHECK((ctx->uniform_staging != NULL) &&
                      (memcmp(ctx->uniform_staging, c0, sizeof (c0)) == 0),
                      "the next update stages c0");
            SDL_SubmitGPUCommandBuffer(cb);
        } // if
    } // if

    MOJOSHADER_sdlDeleteShader(ctx, vs);
    MOJOSHADER_sdlDeleteShader(ctx, ps);
    MOJOSHADER_sdlDestroyContext(ctx);
    CNA_CHECK(a.live == 0, "the context returned everything it allocated");
} // test_uniform_staging_allocation_failure

/*
 * With a real SDL3_shadercross on the library path, the test mode links a device's programs
 * through it: reflection and compilation by ShaderCross, the shaders created on the device. With
 * the stand-in, which compiles nothing, this is skipped.
 */
static void test_shadercross_links_on_device(void)
{
    CountingAllocator a;
    MOJOSHADER_sdlContext *ctx;
    MOJOSHADER_sdlShaderData *vs, *ps;
    SDL_GPUShader *gvs = NULL, *gps = NULL;

    if (shadercross_is_stub())
    {
        printf("  (the stand-in ShaderCross compiles nothing: skipped)\n");
        return;
    } // if
    ctx = make_context(gpu_device, &a);
    if (ctx == NULL)
    {
        CNA_CHECK(0, "a context is made");
        return;
    } // if
    if (!MOJOSHADER_sdlSetShaderCrossTestMode(ctx, 1))
        printf("  (no SDL3_shadercross library: skipped)\n");
    else
    {
        vs = compile(ctx, uniform_vs2, sizeof (uniform_vs2));
        ps = compile(ctx, red_ps2, sizeof (red_ps2));
        CNA_CHECK(vs && ps, "the shaders compile");
        if (vs && ps)
        {
            CNA_CHECK(link_pair(ctx, vs, ps) != NULL, "the pair links through ShaderCross");
            MOJOSHADER_sdlGetShaders(ctx, &gvs, &gps);
            CNA_CHECK((gvs != NULL) && (gps != NULL), "... into both GPU shaders");
        } // if
        MOJOSHADER_sdlDeleteShader(ctx, vs);
        MOJOSHADER_sdlDeleteShader(ctx, ps);
    } // else
    MOJOSHADER_sdlDestroyContext(ctx);
    CNA_CHECK(a.live == 0, "the context returned everything it allocated");
    CNA_CHECK(shadercross_loaded() != 1, "the context unloads the library it loaded");
} // test_shadercross_links_on_device

/* -- Driver -------------------------------------------------------------------------------- */

static int open_device(void)
{
    const char *want = SDL_getenv("MOJOSHADER_CNA_SDLGPU_DEVICE");
    const char *display = SDL_getenv("DISPLAY");
    const char *wayland = SDL_getenv("WAYLAND_DISPLAY");
    if ((want == NULL) || (strcmp(want, "1") != 0))
    {
        printf("  (MOJOSHADER_CNA_SDLGPU_DEVICE is not 1: the cases with a GPU device are skipped)\n");
        return 0;
    } // if
    // A desktop session's own display: refuse rather than open a device there.
    if (((display != NULL) && (strcmp(display, ":0") == 0)) ||
        ((wayland != NULL) && (strcmp(wayland, "wayland-0") == 0)))
    {
        CNA_CHECK(0, "refusing a GPU device on the desktop display; use a private one");
        return 0;
    } // if
    if (!SDL_Init(SDL_INIT_VIDEO))
    {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        CNA_CHECK(0, "SDL video initializes");
        return 0;
    } // if
    gpu_device = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV, true, NULL);
    if (gpu_device == NULL)
    {
        fprintf(stderr, "SDL_CreateGPUDevice: %s\n", SDL_GetError());
        CNA_CHECK(0, "a SPIR-V GPU device is created");
        SDL_Quit();
        return 0;
    } // if
#ifdef SDL_PROP_GPU_DEVICE_NAME_STRING
    {
        const SDL_PropertiesID props = SDL_GetGPUDeviceProperties(gpu_device);
        printf("  GPU device: %s, SDL_gpu driver %s (%s %s)\n",
               SDL_GetStringProperty(props, SDL_PROP_GPU_DEVICE_NAME_STRING, "?"),
               SDL_GetGPUDeviceDriver(gpu_device),
               SDL_GetStringProperty(props, SDL_PROP_GPU_DEVICE_DRIVER_NAME_STRING, "?"),
               SDL_GetStringProperty(props, SDL_PROP_GPU_DEVICE_DRIVER_INFO_STRING, ""));
    }
#else
    printf("  GPU device: SDL_gpu driver %s\n", SDL_GetGPUDeviceDriver(gpu_device));
#endif
    return 1;
} // open_device

typedef struct TestCase
{
    const char *name;
    void (*run)(void);
    int needs_device;
} TestCase;

static const TestCase test_cases[] = {
    { "context_without_device", test_context_without_device, 0 },
    { "shader_identity_exhaustion", test_shader_identity_exhaustion, 0 },
    { "shadercross_outlives_another_context", test_shadercross_outlives_another_context, 0 },
    { "link_and_cache", test_link_and_cache, 1 },
    { "texbem_uniforms", test_texbem_uniforms, 1 },
    { "delete_program_then_relink", test_delete_program_then_relink, 1 },
    { "delete_program_then_delete_shaders", test_delete_program_then_delete_shaders, 1 },
    { "delete_program_then_destroy_context", test_delete_program_then_destroy_context, 1 },
    { "shader_identity_after_65536_compiles", test_shader_identity_after_65536_compiles, 1 },
    { "shader_identity_high_bits", test_shader_identity_high_bits, 1 },
    { "shadercross_outlives_native_context", test_shadercross_outlives_native_context, 1 },
    { "link_allocation_failures", test_link_allocation_failures, 1 },
    { "uniform_staging_allocation_failure", test_uniform_staging_allocation_failure, 1 },
    { "shadercross_links_on_device", test_shadercross_links_on_device, 1 },
};

/* Every case, or with arguments only the cases they name. */
static int selected(const TestCase *test, int argc, char **argv)
{
    int i;
    if (argc < 2)
        return 1;
    for (i = 1; i < argc; i++)
        if (strcmp(argv[i], test->name) == 0)
            return 1;
    return 0;
} // selected

int main(int argc, char **argv)
{
    const size_t count = sizeof (test_cases) / sizeof (test_cases[0]);
    int device_cases = 0;
    size_t i;

    for (i = 0; i < count; i++)
    {
        if (!selected(&test_cases[i], argc, argv))
            continue;
        if (test_cases[i].needs_device)
            device_cases++;
        else
            cna_test_run(test_cases[i].name, test_cases[i].run);
    } // for
    if ((device_cases > 0) && open_device())
    {
        for (i = 0; i < count; i++)
            if (test_cases[i].needs_device && selected(&test_cases[i], argc, argv))
                cna_test_run(test_cases[i].name, test_cases[i].run);
        SDL_DestroyGPUDevice(gpu_device);
        SDL_Quit();
    } // if
    return cna_test_finish();
} // main
