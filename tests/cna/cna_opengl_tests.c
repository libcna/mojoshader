/**
 * OpenGL glue regressions, CPU only: no GL context is created and no GL function is called.
 *
 * This file is part of the libcna fork of MojoShader; see tests/cna/README.md.
 */

/* The rewrite helpers are static; take the translation unit whole. */
#include "../../mojoshader_opengl.c"

#include <stdio.h>
#include "cna_test.h"

/* vs_2_0 writing only oPos. */
static const uint32_t position_only_vs2[] = {
    CNA_VS(2, 0),
    CNA_OP(CNA_DCL_OP, 2), CNA_DCL(0, 0), CNA_DST(CNA_INPUT, 0, 15),
    CNA_OP(CNA_MOV, 2), CNA_DST(CNA_RASTOUT, 0, 15), CNA_SRC(CNA_INPUT, 0),
    CNA_END
};

/* vs_2_0 writing oPos and oD0. */
static const uint32_t color_vs2[] = {
    CNA_VS(2, 0),
    CNA_OP(CNA_DCL_OP, 2), CNA_DCL(0, 0), CNA_DST(CNA_INPUT, 0, 15),
    CNA_OP(CNA_MOV, 2), CNA_DST(CNA_RASTOUT, 0, 15), CNA_SRC(CNA_INPUT, 0),
    CNA_OP(CNA_MOV, 2), CNA_DST(CNA_ATTROUT, 0, 15), CNA_SRC(CNA_INPUT, 0),
    CNA_END
};

/* ps_3_0 reading COLOR0 (interpolated at the centroid, as every SM2+ colour input is). */
static const uint32_t color_ps3[] = {
    CNA_PS(3, 0),
    CNA_OP(CNA_DCL_OP, 2), CNA_DCL(10, 0), CNA_DST(CNA_INPUT, 0, 15),
    CNA_OP(CNA_MOV, 2), CNA_DST(CNA_COLOROUT, 0, 15), CNA_SRC(CNA_INPUT, 0),
    CNA_END
};

/* ps_3_0 reading TEXCOORD0 declared _centroid. */
static const uint32_t centroid_texcoord_ps3[] = {
    CNA_PS(3, 0),
    CNA_OP(CNA_DCL_OP, 2), CNA_DCL(5, 0), CNA_DST(CNA_INPUT, 0, 15) | 0x00400000u,
    CNA_OP(CNA_MOV, 2), CNA_DST(CNA_COLOROUT, 0, 15), CNA_SRC(CNA_INPUT, 0),
    CNA_END
};

/* ps_3_0 reading plain TEXCOORD4 (desktop GLSL has no gl_TexCoord built-in for it). */
static const uint32_t texcoord_ps3[] = {
    CNA_PS(3, 0),
    CNA_OP(CNA_DCL_OP, 2), CNA_DCL(5, 4), CNA_DST(CNA_INPUT, 0, 15),
    CNA_OP(CNA_MOV, 2), CNA_DST(CNA_COLOROUT, 0, 15), CNA_SRC(CNA_INPUT, 0),
    CNA_END
};

static MOJOSHADER_glContext local_context;

static void use_local_context(void)
{
    memset(&local_context, '\0', sizeof (local_context));
    local_context.malloc_fn = MOJOSHADER_internal_malloc;
    local_context.free_fn = MOJOSHADER_internal_free;
    ctx = &local_context;
} // use_local_context

/*
 * Rewrites the pixel shader of a pair whose vertex shader does not produce `name`, and checks
 * that the input became an initialized private global (so the pair links) and that no input
 * declaration of that name is left.
 */
static void check_unmatched(const char *profile, const uint32_t *vs, size_t vs_bytes,
                            const uint32_t *ps, size_t ps_bytes, const char *name,
                            int expect_rewrite)
{
    const MOJOSHADER_parseData *v = cna_parse(profile, vs, vs_bytes);
    const MOJOSHADER_parseData *p = cna_parse(profile, ps, ps_bytes);
    char what[160];
    char *patched;
    char decl[96];

    use_local_context();
    snprintf(what, sizeof (what), "%s/%s: both shaders translate", profile, name);
    CNA_CHECK((cna_report_errors(what, v) == 0) && (cna_report_errors(what, p) == 0), what);
    if ((v->error_count != 0) || (p->error_count != 0))
        goto done;
    snprintf(what, sizeof (what), "%s/%s: the pixel shader declares the input", profile, name);
    CNA_CHECK(strstr(p->output, name) != NULL, what);

    patched = glsl_source_without_unmatched_inputs(v, p);
    if (!expect_rewrite)
    {
        snprintf(what, sizeof (what), "%s/%s: a matched input is left alone", profile, name);
        CNA_CHECK(patched == NULL, what);
        Free(patched);
        goto done;
    } // if

    snprintf(what, sizeof (what), "%s/%s: the pixel shader is rewritten", profile, name);
    CNA_CHECK(patched != NULL, what);
    if (patched != NULL)
    {
        snprintf(decl, sizeof (decl), "vec4 %s = vec4(0.0);", name);
        snprintf(what, sizeof (what), "%s/%s: the input becomes an initialized global", profile, name);
        CNA_CHECK(strstr(patched, decl) != NULL, what);
        snprintf(decl, sizeof (decl), "in highp vec4 %s;", name);
        snprintf(what, sizeof (what), "%s/%s: no input declaration remains", profile, name);
        CNA_CHECK(strstr(patched, decl) == NULL, what);
        snprintf(decl, sizeof (decl), "varying vec4 %s;", name);
        CNA_CHECK(strstr(patched, decl) == NULL, what);
        if (strstr(patched, "= vec4(0.0);") == NULL)
            fprintf(stderr, "%s\n", patched);
        Free(patched);
    } // if

done:
    MOJOSHADER_freeParseData(v);
    MOJOSHADER_freeParseData(p);
} // check_unmatched

/* A2v2 A-13: an unmatched centroid input is replaced as an ordinary one is. */
static void test_unmatched_centroid_inputs_are_replaced(void)
{
    static const char *const profiles[] = { "glsles3", "glsl120" };
    size_t i;
    for (i = 0; i < 2; i++)
    {
        check_unmatched(profiles[i], position_only_vs2, sizeof (position_only_vs2),
                        color_ps3, sizeof (color_ps3), "cna_centroid_10_0", 1);
        check_unmatched(profiles[i], position_only_vs2, sizeof (position_only_vs2),
                        centroid_texcoord_ps3, sizeof (centroid_texcoord_ps3),
                        "cna_centroid_5_0", 1);
        check_unmatched(profiles[i], position_only_vs2, sizeof (position_only_vs2),
                        texcoord_ps3, sizeof (texcoord_ps3), "io_5_4", 1);
        check_unmatched(profiles[i], color_vs2, sizeof (color_vs2),
                        color_ps3, sizeof (color_ps3), "cna_centroid_10_0", 0);
    } // for
} // test_unmatched_centroid_inputs_are_replaced

int main(void)
{
    cna_test_run("unmatched_centroid_inputs_are_replaced",
                 test_unmatched_centroid_inputs_are_replaced);
    return cna_test_finish();
} // main
