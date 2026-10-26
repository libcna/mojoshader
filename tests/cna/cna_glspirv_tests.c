/**
 * The GLSPIRV profile carries the two raster corrections the GLSL profile has for an OpenGL
 * renderer: the XNA pixel centre (cnaPixelCenter, MOJOSHADER_glProgramPixelCenterInfo) and the
 * VFACE sign under GL's winding (cnaVFaceFlip, MOJOSHADER_glProgramVFaceFlipInfo). Both were
 * GLSL-only, so a program linked through ARB_gl_spirv silently ignored both setters. The Vulkan
 * SPIR-V profile, whose renderer moves the viewport instead, must not grow either uniform.
 *
 * This file is part of the libcna fork of MojoShader; see tests/cna/README.md.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cna_test.h"
#include "spirv/spirv.h"

#define __MOJOSHADER_INTERNAL__ 1
#include "mojoshader_internal.h"

#define MAX_IDS 1024

/* A destination or source register of type 17 (MISCTYPE), whose type spans bits 28-30 and 11-12. */
#define MISC_DST(num, mask) (0x80000000u | ((17u & 7u) << 28) | ((17u >> 3) << 11) | \
                             ((uint32_t) (mask) << 16) | (uint32_t) (num))
#define MISC_SRC(num, swizzle) (0x80000000u | ((17u & 7u) << 28) | ((17u >> 3) << 11) | \
                                ((uint32_t) (swizzle) << 16) | (uint32_t) (num))

/* vs_2_0: dcl_position v0; mov oPos, v0 */
static const uint32_t vs_position[] = {
    CNA_VS(2, 0),
    CNA_OP(31, 2), CNA_DCL(0, 0), CNA_DST(1, 0, 0xF),
    CNA_OP(1, 2), CNA_DST(4, 0, 0xF), CNA_SRC(1, 0),
    CNA_END
};

/* ps_3_0: def c0, 1, 1, 1, 1; def c1, 0, 0, 0, 1; dcl vFace; cmp oC0, vFace.x, c0, c1 */
static const uint32_t ps_vface[] = {
    CNA_PS(3, 0),
    CNA_OP(81, 5), CNA_DST(2, 0, 0xF), 0x3f800000u, 0x3f800000u, 0x3f800000u, 0x3f800000u,
    CNA_OP(81, 5), CNA_DST(2, 1, 0xF), 0x00000000u, 0x00000000u, 0x00000000u, 0x3f800000u,
    CNA_OP(31, 2), 0x80000000u, MISC_DST(1, 0xF),
    CNA_OP(88, 4), CNA_DST(8, 0, 0xF), MISC_SRC(1, 0x00), CNA_SRC(2, 0), CNA_SRC(2, 1),
    CNA_END
};

typedef struct Module
{
    int storage[MAX_IDS];
    int location[MAX_IDS];
    char name[MAX_IDS][32];
} Module;

static void read_module(const MOJOSHADER_parseData *pd, Module *m)
{
    const uint32_t *words = (const uint32_t *) pd->output;
    const size_t count = cna_spirv_module_words(pd);
    size_t at = 5;
    int i;

    memset(m, '\0', sizeof (*m));
    for (i = 0; i < MAX_IDS; i++)
    {
        m->storage[i] = -1;
        m->location[i] = -1;
    } // for

    while (at < count)
    {
        const uint32_t length = words[at] >> 16;
        const uint32_t opcode = words[at] & 0xFFFF;
        if ((length == 0) || (at + length > count))
            break;
        if ((opcode == SpvOpVariable) && (length >= 4) && (words[at + 2] < MAX_IDS))
            m->storage[words[at + 2]] = (int) words[at + 3];
        else if ((opcode == SpvOpDecorate) && (length >= 4) && (words[at + 1] < MAX_IDS) &&
                 (words[at + 2] == SpvDecorationLocation))
            m->location[words[at + 1]] = (int) words[at + 3];
        else if ((opcode == SpvOpName) && (length >= 3) && (words[at + 1] < MAX_IDS))
        {
            const char *text = (const char *) &words[at + 2];
            const size_t room = (length - 2) * 4;
            char *name = m->name[words[at + 1]];
            size_t j;
            for (j = 0; (j < room) && (j < sizeof (m->name[0]) - 1) && text[j]; j++)
                name[j] = text[j];
            name[j] = '\0';
        } // else if
        at += length;
    } // while
} // read_module

/* The id of the named UniformConstant variable, or -1. */
static int uniform_constant(const Module *m, const char *name)
{
    int id;
    for (id = 0; id < MAX_IDS; id++)
        if ((m->storage[id] == SpvStorageClassUniformConstant) && (strcmp(m->name[id], name) == 0))
            return id;
    return -1;
} // uniform_constant

/* spirv-val for an OpenGL 4.5 consumer, as ARB_gl_spirv takes it. 1 valid, 0 not, -1 no tool. */
static int validate_for_opengl(const char *name)
{
    const char *validator = getenv("MOJOSHADER_SPIRV_VAL");
    const char *dir = getenv("MOJOSHADER_CNA_TEST_DIR");
    char command[2048];
    if ((validator == NULL) || (*validator == '\0') || (dir == NULL))
        return -1;
    snprintf(command, sizeof (command), "'%s' --target-env opengl4.5 '%s/%s.spv'",
             validator, dir, name);
    return (system(command) == 0) ? 1 : 0;
} // validate_for_opengl

static Module module;

static void check_profile(const char *profile, const uint32_t *tokens, size_t bytes,
                          const char *uniform, const char *name)
{
    const MOJOSHADER_parseData *pd = cna_parse(profile, tokens, bytes);
    const int glspirv = (strcmp(profile, MOJOSHADER_PROFILE_GLSPIRV) == 0);
    SpirvPatchTable table;
    int id;
    int validated;

    CNA_CHECK(cna_report_errors(name, pd) == 0, "the shader translates");
    if (pd->error_count != 0)
    {
        MOJOSHADER_freeParseData(pd);
        return;
    } // if

    validated = cna_spirv_validate(pd, name);
    CNA_CHECK(validated != 0, "spirv-val accepts the module");
    read_module(pd, &module);
    MOJOSHADER_spirv_get_patch_table(pd, &table);
    id = uniform_constant(&module, uniform);
    if (glspirv)
    {
        const SpirvPatchEntry *entry =
            (strcmp(uniform, "cnaPixelCenter") == 0) ? &table.pixelcenter : &table.vfaceflip;
        CNA_CHECK(id != -1, "GLSPIRV declares the correction uniform");
        CNA_CHECK((id != -1) && (module.location[id] >= 0), "the uniform has a location");
        CNA_CHECK((id != -1) && (entry->location == module.location[id]),
                  "the patch table names the uniform's location, which the GL glue looks up");
        CNA_CHECK(validate_for_opengl(name) != 0, "spirv-val accepts the module for OpenGL 4.5");
    } // if
    else
    {
        CNA_CHECK(id == -1, "the Vulkan SPIR-V profile declares no correction uniform");
        CNA_CHECK((table.pixelcenter.location == -1) && (table.vfaceflip.location == -1),
                  "the Vulkan SPIR-V patch table has no correction location");
    } // else
    MOJOSHADER_freeParseData(pd);
} // check_profile

static void test_pixel_center_glspirv(void)
{
    check_profile(MOJOSHADER_PROFILE_GLSPIRV, vs_position, sizeof (vs_position),
                  "cnaPixelCenter", "pixel_center_glspirv");
} // test_pixel_center_glspirv

static void test_pixel_center_spirv(void)
{
    check_profile(MOJOSHADER_PROFILE_SPIRV, vs_position, sizeof (vs_position),
                  "cnaPixelCenter", "pixel_center_spirv");
} // test_pixel_center_spirv

static void test_vface_flip_glspirv(void)
{
    check_profile(MOJOSHADER_PROFILE_GLSPIRV, ps_vface, sizeof (ps_vface),
                  "cnaVFaceFlip", "vface_flip_glspirv");
} // test_vface_flip_glspirv

static void test_vface_flip_spirv(void)
{
    check_profile(MOJOSHADER_PROFILE_SPIRV, ps_vface, sizeof (ps_vface),
                  "cnaVFaceFlip", "vface_flip_spirv");
} // test_vface_flip_spirv

/* The GLSL profile, whose output these mirror, keeps both uniforms as they were. */
static void test_glsl_reference(void)
{
    const MOJOSHADER_parseData *vs = cna_parse(MOJOSHADER_PROFILE_GLSL120, vs_position,
                                               sizeof (vs_position));
    const MOJOSHADER_parseData *ps = cna_parse(MOJOSHADER_PROFILE_GLSL120, ps_vface,
                                               sizeof (ps_vface));
    CNA_CHECK(cna_report_errors("glsl vs", vs) == 0, "the vertex shader translates to GLSL");
    CNA_CHECK(cna_report_errors("glsl ps", ps) == 0, "the pixel shader translates to GLSL");
    CNA_CHECK(cna_output_contains(vs, "gl_Position.xy += cnaPixelCenter * gl_Position.w;"),
              "GLSL applies the pixel centre");
    CNA_CHECK(cna_output_contains(ps, "uniform float cnaVFaceFlip;"),
              "GLSL declares the VFACE flip");
    MOJOSHADER_freeParseData(vs);
    MOJOSHADER_freeParseData(ps);
} // test_glsl_reference

int main(void)
{
    cna_test_run("pixel_center_glspirv", test_pixel_center_glspirv);
    cna_test_run("pixel_center_spirv", test_pixel_center_spirv);
    cna_test_run("vface_flip_glspirv", test_vface_flip_glspirv);
    cna_test_run("vface_flip_spirv", test_vface_flip_spirv);
    cna_test_run("glsl_reference", test_glsl_reference);
    return cna_test_finish();
} // main
