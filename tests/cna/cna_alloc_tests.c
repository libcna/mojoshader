/**
 * Shader translation under allocation failure, in every profile.
 *
 * Each case translates a valid shader while failing its 1st, 2nd, ... Nth allocation in turn,
 * until a run asks for no more allocations than it was given. Every run must return parse data
 * that frees without leaving an allocation behind, and a run whose allocation was refused must
 * report an error rather than a translation with something missing.
 *
 * This file is part of the libcna fork of MojoShader; see tests/cna/README.md.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cna_test.h"

#define CNA_TEXBEM 67  /* D3DSIO_TEXBEM */
#define CNA_CMP 88  /* D3DSIO_CMP */
#define CNA_DCL_2D (0x80000000u | (2u << 27))  /* dcl_2d s# */
#define CNA_F1 0x3f800000u  /* 1.0f */

/* A register of type 17 (MISCTYPE): vFace is 1. */
#define CNA_MISC_DST(num, mask) (0x80000000u | ((17u & 7u) << 28) | ((17u >> 3) << 11) | \
                                 ((uint32_t) (mask) << 16) | (uint32_t) (num))
#define CNA_MISC_SRC(num, swizzle) (0x80000000u | ((17u & 7u) << 28) | ((17u >> 3) << 11) | \
                                    ((uint32_t) (swizzle) << 16) | (uint32_t) (num))

/* vs_1_1: oPos, oD0, oT0, oT1 and oFog from two inputs and a uniform. */
static const uint32_t vs11[] = {
    CNA_VS(1, 1),
    CNA_DCL_OP, CNA_DCL(0, 0), CNA_DST(CNA_INPUT, 0, 0xF),
    CNA_DCL_OP, CNA_DCL(10, 0), CNA_DST(CNA_INPUT, 1, 0xF),
    CNA_ADD, CNA_DST(CNA_TEMP, 0, 0xF), CNA_SRC(CNA_INPUT, 0), CNA_SRC(CNA_CONST, 0),
    CNA_MOV, CNA_DST(CNA_RASTOUT, 0, 0xF), CNA_SRC(CNA_TEMP, 0),
    CNA_MOV, CNA_DST(CNA_ATTROUT, 0, 0xF), CNA_SRC(CNA_INPUT, 1),
    CNA_MOV, CNA_DST(CNA_TEXCRDOUT, 0, 0xF), CNA_SRC(CNA_INPUT, 0),
    CNA_MOV, CNA_DST(CNA_TEXCRDOUT, 1, 0xF), CNA_SRC(CNA_TEMP, 0),
    CNA_MOV, CNA_DST(CNA_RASTOUT, 1, 0x1), CNA_SRC_SWZ(CNA_INPUT, 0, 0xFF),
    CNA_END
};

/* ps_1_1: tex t0; texbem t1, t0; mov r0, t1. */
static const uint32_t ps11_texbem[] = {
    CNA_PS(1, 1),
    CNA_TEX, CNA_DST(CNA_TEXTURE, 0, 0xF),
    CNA_TEXBEM, CNA_DST(CNA_TEXTURE, 1, 0xF), CNA_SRC(CNA_TEXTURE, 0),
    CNA_MOV, CNA_DST(CNA_TEMP, 0, 0xF), CNA_SRC(CNA_TEXTURE, 1),
    CNA_END
};

/* ps_1_4: texld r0, t0; texld r1, t3; mul r0, r0, r1. */
static const uint32_t ps14_texld[] = {
    CNA_PS(1, 4),
    CNA_OP(CNA_TEXLD, 0), CNA_DST(CNA_TEMP, 0, 15), CNA_SRC(CNA_TEXTURE, 0),
    CNA_OP(CNA_TEXLD, 0), CNA_DST(CNA_TEMP, 1, 15), CNA_SRC(CNA_TEXTURE, 3),
    CNA_OP(CNA_MUL, 0), CNA_DST(CNA_TEMP, 0, 15), CNA_SRC(CNA_TEMP, 0), CNA_SRC(CNA_TEMP, 1),
    CNA_END
};

/* vs_2_0: main calls l0, which writes oPos and oD0 from v0 and a DEF constant. */
static const uint32_t vs2_subroutine[] = {
    CNA_VS(2, 0),
    CNA_OP(CNA_DEF, 5), CNA_DST(CNA_CONST, 1, 0xF), CNA_F1, CNA_F1, CNA_F1, CNA_F1,
    CNA_OP(CNA_DCL_OP, 2), CNA_DCL(0, 0), CNA_DST(CNA_INPUT, 0, 0xF),
    CNA_OP(CNA_CALL, 1), CNA_SRC(CNA_LABEL, 0),
    CNA_OP(CNA_RET, 0),
    CNA_OP(CNA_LABEL_OP, 1), CNA_SRC(CNA_LABEL, 0),
    CNA_OP(CNA_MOV, 2), CNA_DST(CNA_RASTOUT, 0, 0xF), CNA_SRC(CNA_INPUT, 0),
    CNA_OP(CNA_MUL, 3), CNA_DST(CNA_ATTROUT, 0, 0xF), CNA_SRC(CNA_INPUT, 0), CNA_SRC(CNA_CONST, 1),
    CNA_OP(CNA_RET, 0),
    CNA_END
};

/* ps_2_0: dcl t0; dcl_2d s0; texld r0, t0, s0; mul oC0, r0, c0. */
static const uint32_t ps2_texld[] = {
    CNA_PS(2, 0),
    CNA_OP(CNA_DCL_OP, 2), 0x80000000u, CNA_DST(CNA_TEXTURE, 0, 0xF),
    CNA_OP(CNA_DCL_OP, 2), CNA_DCL_2D, CNA_DST(CNA_SAMPLER, 0, 0xF),
    CNA_OP(CNA_TEXLD, 3), CNA_DST(CNA_TEMP, 0, 0xF), CNA_SRC(CNA_TEXTURE, 0), CNA_SRC(CNA_SAMPLER, 0),
    CNA_OP(CNA_MUL, 3), CNA_DST(CNA_COLOROUT, 0, 0xF), CNA_SRC(CNA_TEMP, 0), CNA_SRC(CNA_CONST, 0),
    CNA_END
};

/* vs_3_0: FOG and TEXCOORD0 packed in o1, a REP loop over i0, two TEXCOORDs packed in o2. */
static const uint32_t vs3_packed_loop[] = {
    CNA_VS(3, 0),
    CNA_OP(CNA_DEFI, 5), CNA_DST(CNA_CONSTINT, 0, 0xF), 2, 0, 0, 0,
    CNA_OP(CNA_DCL_OP, 2), CNA_DCL(0, 0), CNA_DST(CNA_INPUT, 0, 0xF),
    CNA_OP(CNA_DCL_OP, 2), CNA_DCL(0, 0), CNA_DST(CNA_OUTPUT, 0, 0xF),
    CNA_OP(CNA_DCL_OP, 2), CNA_DCL(11, 0), CNA_DST(CNA_OUTPUT, 1, 0x1),
    CNA_OP(CNA_DCL_OP, 2), CNA_DCL(5, 0), CNA_DST(CNA_OUTPUT, 1, 0xE),
    CNA_OP(CNA_DCL_OP, 2), CNA_DCL(5, 1), CNA_DST(CNA_OUTPUT, 2, 0x3),
    CNA_OP(CNA_DCL_OP, 2), CNA_DCL(5, 2), CNA_DST(CNA_OUTPUT, 2, 0xC),
    CNA_OP(CNA_MOV, 2), CNA_DST(CNA_TEMP, 0, 0xF), CNA_SRC(CNA_INPUT, 0),
    CNA_OP(CNA_REP, 1), CNA_SRC(CNA_CONSTINT, 0),
    CNA_OP(CNA_ADD, 3), CNA_DST(CNA_TEMP, 0, 0xF), CNA_SRC(CNA_TEMP, 0), CNA_SRC(CNA_CONST, 0),
    CNA_OP(CNA_ENDREP, 0),
    CNA_OP(CNA_MOV, 2), CNA_DST(CNA_OUTPUT, 0, 0xF), CNA_SRC(CNA_TEMP, 0),
    CNA_OP(CNA_MOV, 2), CNA_DST(CNA_OUTPUT, 1, 0xE), CNA_SRC(CNA_INPUT, 0),
    CNA_OP(CNA_MOV, 2), CNA_DST(CNA_OUTPUT, 1, 0x1), CNA_SRC_SWZ(CNA_TEMP, 0, 0xFF),
    CNA_OP(CNA_MOV, 2), CNA_DST(CNA_OUTPUT, 2, 0xF), CNA_SRC(CNA_TEMP, 0),
    CNA_END
};

/* ps_3_0: TEXCOORD0/TEXCOORD1 packed in v0, a texture, vFace choosing between two DEFs. */
static const uint32_t ps3_packed_face[] = {
    CNA_PS(3, 0),
    CNA_OP(CNA_DEF, 5), CNA_DST(CNA_CONST, 1, 0xF), CNA_F1, CNA_F1, CNA_F1, CNA_F1,
    CNA_OP(CNA_DEF, 5), CNA_DST(CNA_CONST, 2, 0xF), 0, 0, 0, CNA_F1,
    CNA_OP(CNA_DCL_OP, 2), CNA_DCL(5, 0), CNA_DST(CNA_INPUT, 0, 0x3),
    CNA_OP(CNA_DCL_OP, 2), CNA_DCL(5, 1), CNA_DST(CNA_INPUT, 0, 0xC),
    CNA_OP(CNA_DCL_OP, 2), 0x80000000u, CNA_MISC_DST(1, 0xF),
    CNA_OP(CNA_DCL_OP, 2), CNA_DCL_2D, CNA_DST(CNA_SAMPLER, 0, 0xF),
    CNA_OP(CNA_TEXLD, 3), CNA_DST(CNA_TEMP, 0, 0xF), CNA_SRC(CNA_INPUT, 0), CNA_SRC(CNA_SAMPLER, 0),
    CNA_OP(CNA_CMP, 4), CNA_DST(CNA_TEMP, 1, 0xF), CNA_MISC_SRC(1, 0x00), CNA_SRC(CNA_CONST, 1),
        CNA_SRC(CNA_CONST, 2),
    CNA_OP(CNA_MUL, 3), CNA_DST(CNA_COLOROUT, 0, 0xF), CNA_SRC(CNA_TEMP, 0), CNA_SRC(CNA_TEMP, 1),
    CNA_END
};

static const char *const profiles[] = {
    MOJOSHADER_PROFILE_D3D, MOJOSHADER_PROFILE_BYTECODE, MOJOSHADER_PROFILE_HLSL,
    MOJOSHADER_PROFILE_GLSL, MOJOSHADER_PROFILE_GLSL120, MOJOSHADER_PROFILE_GLSLES,
    MOJOSHADER_PROFILE_GLSLES3, MOJOSHADER_PROFILE_ARB1, MOJOSHADER_PROFILE_NV2,
    MOJOSHADER_PROFILE_NV3, MOJOSHADER_PROFILE_NV4, MOJOSHADER_PROFILE_METAL,
    MOJOSHADER_PROFILE_SPIRV, MOJOSHADER_PROFILE_GLSPIRV
};

/*
 * Fails each allocation of one translation in turn. Returns the number of runs whose refused
 * allocation was reached.
 */
static int sweep(const char *name, const char *profile, const unsigned char *bytes,
                 unsigned int length)
{
    char label[160];
    int fail_at;
    int injected = 0;
    for (fail_at = 1; fail_at <= 100000; fail_at++)
    {
        const MOJOSHADER_parseData *pd;
        cna_alloc_reset(fail_at);
        pd = MOJOSHADER_parse(profile, "main", bytes, length, NULL, 0, NULL, 0,
                              cna_alloc_malloc, cna_alloc_free, NULL);
        snprintf(label, sizeof (label), "%s %s, allocation %d refused: parse data comes back",
                 name, profile, fail_at);
        CNA_CHECK(pd != NULL, label);
        if (pd == NULL)
            return injected;
        if (cna_alloc_hit())
        {
            injected++;
            snprintf(label, sizeof (label), "%s %s, allocation %d refused: an error is reported",
                     name, profile, fail_at);
            CNA_CHECK(pd->error_count > 0, label);
        } // if
        if (!cna_alloc_hit())  // the complete run: what the sweep covered
            printf("  (%s %s: %d allocations, %s)\n", name, profile, fail_at - 1,
                   pd->error_count ? pd->errors[0].error : "translated");
        MOJOSHADER_freeParseData(pd);
        snprintf(label, sizeof (label), "%s %s, allocation %d refused: nothing outlives it",
                 name, profile, fail_at);
        if (cna_alloc_live() != 0)
            fprintf(stderr, "  %s (%d still live)\n", label, cna_alloc_live());
        CNA_CHECK(cna_alloc_live() == 0, label);
        if (!cna_alloc_hit())
            return injected;
    } // for
    snprintf(label, sizeof (label), "%s %s: the sweep ends", name, profile);
    CNA_CHECK(0, label);
    return injected;
} // sweep

static void sweep_all_profiles(const char *name, const uint32_t *tokens, size_t bytes)
{
    unsigned char *copy = (unsigned char *) cna_exact_copy(tokens, bytes);
    size_t i;
    int injected = 0;
    for (i = 0; i < sizeof (profiles) / sizeof (profiles[0]); i++)
        injected += sweep(name, profiles[i], copy, (unsigned int) bytes);
    printf("  (%s: %d refused allocations over %d profiles)\n", name, injected,
           (int) (sizeof (profiles) / sizeof (profiles[0])));
    CNA_CHECK(injected > 0, "the sweep refused allocations");
    free(copy);
} // sweep_all_profiles

/*
 * R2-01 (2026-10-26 remediation review): translation crashed when an allocation failed --
 * SPIR-V at the 20th of a plain vs_3_0, spv_output_location() asking buffer_size() of a NULL
 * buffer.
 */
static void test_vs11(void) { sweep_all_profiles("vs11", vs11, sizeof (vs11)); }
static void test_ps11_texbem(void) { sweep_all_profiles("ps11_texbem", ps11_texbem, sizeof (ps11_texbem)); }
static void test_ps14_texld(void) { sweep_all_profiles("ps14_texld", ps14_texld, sizeof (ps14_texld)); }
static void test_vs2_subroutine(void) { sweep_all_profiles("vs2_subroutine", vs2_subroutine, sizeof (vs2_subroutine)); }
static void test_ps2_texld(void) { sweep_all_profiles("ps2_texld", ps2_texld, sizeof (ps2_texld)); }
static void test_vs3_packed_loop(void) { sweep_all_profiles("vs3_packed_loop", vs3_packed_loop, sizeof (vs3_packed_loop)); }
static void test_ps3_packed_face(void) { sweep_all_profiles("ps3_packed_face", ps3_packed_face, sizeof (ps3_packed_face)); }

/* A vs_2_0 with a constant table and a preshader (cna_push_vs_with_preshader()). */
static void test_vs2_ctab_preshader(void)
{
    CnaWords w;
    memset(&w, '\0', sizeof (w));
    cna_push_vs_with_preshader(&w);
    sweep_all_profiles("vs2_ctab_preshader", w.at, w.count * 4);
    cna_words_free(&w);
} // test_vs2_ctab_preshader

/* vs_3_0: def c0; then a write from the undeclared input v1, which the SPIR-V profile refuses. */
static const uint32_t vs3_def_refused[] = {
    CNA_VS(3, 0),
    CNA_OP(CNA_DEF, 5), CNA_DST(CNA_CONST, 0, 0xF), CNA_F1, 0x3f000000u, 0x40000000u, 0x3e800000u,
    CNA_OP(CNA_DCL_OP, 2), CNA_DCL(0, 0), CNA_DST(CNA_INPUT, 0, 0xF),
    CNA_OP(CNA_DCL_OP, 2), CNA_DCL(0, 0), CNA_DST(CNA_OUTPUT, 0, 0xF),
    CNA_OP(CNA_MOV, 2), CNA_DST(CNA_OUTPUT, 0, 0xF), CNA_SRC(CNA_INPUT, 1),
    CNA_END
};

/*
 * A SPIR-V parse refused after a DEF, with every allocation granted, frees the constant cache
 * the DEF filled: emit_SPIRV_finalize(), which alone freed it, never runs for a refused parse.
 */
static void test_refused_spirv_frees_constants(void)
{
    static const char *const spirv[] = { MOJOSHADER_PROFILE_SPIRV, MOJOSHADER_PROFILE_GLSPIRV };
    size_t i;
    for (i = 0; i < 2; i++)
    {
        const MOJOSHADER_parseData *pd;
        cna_alloc_reset(0);
        pd = MOJOSHADER_parse(spirv[i], "main", (const unsigned char *) vs3_def_refused,
                              sizeof (vs3_def_refused), NULL, 0, NULL, 0,
                              cna_alloc_malloc, cna_alloc_free, NULL);
        CNA_CHECK(pd->error_count > 0, "the shader is refused");
        MOJOSHADER_freeParseData(pd);
        if (cna_alloc_live() != 0)
            fprintf(stderr, "  %s: %d allocation(s) outlive the parse\n", spirv[i], cna_alloc_live());
        CNA_CHECK(cna_alloc_live() == 0, "a refused parse frees its constants");
    } // for
} // test_refused_spirv_frees_constants

int main(void)
{
    cna_test_run("refused_spirv_frees_constants", test_refused_spirv_frees_constants);
    cna_test_run("vs11", test_vs11);
    cna_test_run("ps11_texbem", test_ps11_texbem);
    cna_test_run("ps14_texld", test_ps14_texld);
    cna_test_run("vs2_subroutine", test_vs2_subroutine);
    cna_test_run("vs2_ctab_preshader", test_vs2_ctab_preshader);
    cna_test_run("ps2_texld", test_ps2_texld);
    cna_test_run("vs3_packed_loop", test_vs3_packed_loop);
    cna_test_run("ps3_packed_face", test_ps3_packed_face);
    return cna_test_finish();
} // main
