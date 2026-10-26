/**
 * GLSL profile regressions.
 *
 * This file is part of the libcna fork of MojoShader; see tests/cna/README.md.
 */

#include <stdio.h>
#include <string.h>

#include "cna_test.h"

/* vs_2_0: main() calls l0 and returns; l0, after it, writes oPos and oD0. */
static const uint32_t color_subroutine_vs2[] = {
    CNA_VS(2, 0),
    CNA_OP(CNA_DCL_OP, 2), CNA_DCL(0, 0), CNA_DST(CNA_INPUT, 0, 15),
    CNA_OP(CNA_CALL, 1), CNA_SRC(CNA_LABEL, 0),
    CNA_OP(CNA_RET, 0),
    CNA_OP(CNA_LABEL_OP, 1), CNA_SRC(CNA_LABEL, 0),
    CNA_OP(CNA_MOV, 2), CNA_DST(CNA_RASTOUT, 0, 15), CNA_SRC(CNA_INPUT, 0),
    CNA_OP(CNA_MOV, 2), CNA_DST(CNA_ATTROUT, 0, 15), CNA_SRC(CNA_INPUT, 0),
    CNA_OP(CNA_RET, 0),
    CNA_END
};

/* The same writes in main(), no subroutine. */
static const uint32_t color_main_vs2[] = {
    CNA_VS(2, 0),
    CNA_OP(CNA_DCL_OP, 2), CNA_DCL(0, 0), CNA_DST(CNA_INPUT, 0, 15),
    CNA_OP(CNA_MOV, 2), CNA_DST(CNA_RASTOUT, 0, 15), CNA_SRC(CNA_INPUT, 0),
    CNA_OP(CNA_MOV, 2), CNA_DST(CNA_ATTROUT, 0, 15), CNA_SRC(CNA_INPUT, 0),
    CNA_END
};

static const char *const glsl_profiles[] = { "glsl", "glsl120", "glsles", "glsles3" };

/* Offset of `text` in the output, or -1. */
static long find(const MOJOSHADER_parseData *pd, const char *text)
{
    const char *at = (pd && pd->output) ? strstr(pd->output, text) : NULL;
    return at ? (long) (at - pd->output) : -1;
} // find

/* A2v2 A-10: oD0 written only in a forward subroutine is still clamped, once, at main's end. */
static void test_color_written_in_subroutine_is_clamped(void)
{
    size_t i;
    for (i = 0; i < sizeof (glsl_profiles) / sizeof (glsl_profiles[0]); i++)
    {
        const MOJOSHADER_parseData *pd = cna_parse(glsl_profiles[i], color_subroutine_vs2,
                                                   sizeof (color_subroutine_vs2));
        char what[96];
        long def, main_at, call;
        snprintf(what, sizeof (what), "%s: translates", glsl_profiles[i]);
        CNA_CHECK(cna_report_errors(what, pd) == 0, what);
        if (pd->error_count != 0)
        {
            MOJOSHADER_freeParseData(pd);
            continue;
        } // if
        def = find(pd, "void cna_vs_epilogue()");
        main_at = find(pd, "void main()");
        call = find(pd, "cna_vs_epilogue();");
        snprintf(what, sizeof (what), "%s: the epilogue clamps oD0", glsl_profiles[i]);
        CNA_CHECK(cna_output_contains(pd, "vs_oD0 = clamp(vs_oD0, 0.0, 1.0);"), what);
        snprintf(what, sizeof (what), "%s: the epilogue is defined before main", glsl_profiles[i]);
        CNA_CHECK((def >= 0) && (main_at > def), what);
        snprintf(what, sizeof (what), "%s: main calls the epilogue", glsl_profiles[i]);
        CNA_CHECK(call > main_at, what);
        snprintf(what, sizeof (what), "%s: the epilogue runs once", glsl_profiles[i]);
        CNA_CHECK(cna_output_count(pd, "cna_vs_epilogue();") == 1, what);
        CNA_CHECK(cna_output_count(pd, "gl_Position.xy += cnaPixelCenter") == 1, what);
        if (!cna_output_contains(pd, "vs_oD0 = clamp(vs_oD0, 0.0, 1.0);"))
            fprintf(stderr, "%s\n", pd->output);
        MOJOSHADER_freeParseData(pd);
    } // for
} // test_color_written_in_subroutine_is_clamped

/* Without a subroutine the epilogue stays inline, as before. */
static void test_epilogue_stays_inline_without_subroutines(void)
{
    size_t i;
    for (i = 0; i < sizeof (glsl_profiles) / sizeof (glsl_profiles[0]); i++)
    {
        const MOJOSHADER_parseData *pd = cna_parse(glsl_profiles[i], color_main_vs2,
                                                   sizeof (color_main_vs2));
        char what[96];
        snprintf(what, sizeof (what), "%s: translates", glsl_profiles[i]);
        CNA_CHECK(cna_report_errors(what, pd) == 0, what);
        snprintf(what, sizeof (what), "%s: clamps oD0 inline", glsl_profiles[i]);
        CNA_CHECK(cna_output_contains(pd, "vs_oD0 = clamp(vs_oD0, 0.0, 1.0);"), what);
        CNA_CHECK(!cna_output_contains(pd, "cna_vs_epilogue"), what);
        MOJOSHADER_freeParseData(pd);
    } // for
} // test_epilogue_stays_inline_without_subroutines

int main(void)
{
    cna_test_run("color_written_in_subroutine_is_clamped",
                 test_color_written_in_subroutine_is_clamped);
    cna_test_run("epilogue_stays_inline_without_subroutines",
                 test_epilogue_stays_inline_without_subroutines);
    return cna_test_finish();
} // main
