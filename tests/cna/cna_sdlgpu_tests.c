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

#define CNA_TEXBEM 67  /* D3DSIO_TEXBEM */

/* vs_2_0 writing only oPos. */
static const uint32_t position_vs2[] = {
    CNA_VS(2, 0),
    CNA_OP(CNA_DCL_OP, 2), CNA_DCL(0, 0), CNA_DST(CNA_INPUT, 0, 15),
    CNA_OP(CNA_MOV, 2), CNA_DST(CNA_RASTOUT, 0, 15), CNA_SRC(CNA_INPUT, 0),
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

int main(void)
{
    cna_test_run("context_without_device", test_context_without_device);
    if (open_device())
    {
        cna_test_run("link_and_cache", test_link_and_cache);
        cna_test_run("texbem_uniforms", test_texbem_uniforms);
        SDL_DestroyGPUDevice(gpu_device);
        SDL_Quit();
    } // if
    return cna_test_finish();
} // main
