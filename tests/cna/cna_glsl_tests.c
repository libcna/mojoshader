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

/* vs_3_0: FOG in o1.x and TEXCOORD0 in o1.yzw, FOG declared first; each lane written alone. */
static const uint32_t fog_first_vs3[] = {
    CNA_VS(3, 0),
    CNA_OP(CNA_DCL_OP, 2), CNA_DCL(0, 0), CNA_DST(CNA_INPUT, 0, 15),
    CNA_OP(CNA_DCL_OP, 2), CNA_DCL(0, 0), CNA_DST(CNA_OUTPUT, 0, 15),
    CNA_OP(CNA_DCL_OP, 2), CNA_DCL(11, 0), CNA_DST(CNA_OUTPUT, 1, 0x1),
    CNA_OP(CNA_DCL_OP, 2), CNA_DCL(5, 0), CNA_DST(CNA_OUTPUT, 1, 0xE),
    CNA_OP(CNA_MOV, 2), CNA_DST(CNA_OUTPUT, 0, 15), CNA_SRC(CNA_INPUT, 0),
    CNA_OP(CNA_MOV, 2), CNA_DST(CNA_OUTPUT, 1, 0xE), CNA_SRC(CNA_INPUT, 0),
    CNA_OP(CNA_MOV, 2), CNA_DST(CNA_OUTPUT, 1, 0x1), CNA_SRC_SWZ(CNA_INPUT, 0, 0xFF),
    CNA_END
};

/* The same, TEXCOORD0 declared first. */
static const uint32_t fog_last_vs3[] = {
    CNA_VS(3, 0),
    CNA_OP(CNA_DCL_OP, 2), CNA_DCL(0, 0), CNA_DST(CNA_INPUT, 0, 15),
    CNA_OP(CNA_DCL_OP, 2), CNA_DCL(0, 0), CNA_DST(CNA_OUTPUT, 0, 15),
    CNA_OP(CNA_DCL_OP, 2), CNA_DCL(5, 0), CNA_DST(CNA_OUTPUT, 1, 0xE),
    CNA_OP(CNA_DCL_OP, 2), CNA_DCL(11, 0), CNA_DST(CNA_OUTPUT, 1, 0x1),
    CNA_OP(CNA_MOV, 2), CNA_DST(CNA_OUTPUT, 0, 15), CNA_SRC(CNA_INPUT, 0),
    CNA_OP(CNA_MOV, 2), CNA_DST(CNA_OUTPUT, 1, 0xE), CNA_SRC(CNA_INPUT, 0),
    CNA_OP(CNA_MOV, 2), CNA_DST(CNA_OUTPUT, 1, 0x1), CNA_SRC_SWZ(CNA_INPUT, 0, 0xFF),
    CNA_END
};

/* vs_3_0: TEXCOORD0 in o1.xyz and FOG in o1.w, FOG declared first; one write to all four. */
static const uint32_t fog_w_vs3[] = {
    CNA_VS(3, 0),
    CNA_OP(CNA_DCL_OP, 2), CNA_DCL(0, 0), CNA_DST(CNA_INPUT, 0, 15),
    CNA_OP(CNA_DCL_OP, 2), CNA_DCL(0, 0), CNA_DST(CNA_OUTPUT, 0, 15),
    CNA_OP(CNA_DCL_OP, 2), CNA_DCL(11, 0), CNA_DST(CNA_OUTPUT, 1, 0x8),
    CNA_OP(CNA_DCL_OP, 2), CNA_DCL(5, 0), CNA_DST(CNA_OUTPUT, 1, 0x7),
    CNA_OP(CNA_MOV, 2), CNA_DST(CNA_OUTPUT, 0, 15), CNA_SRC(CNA_INPUT, 0),
    CNA_OP(CNA_MOV, 2), CNA_DST(CNA_OUTPUT, 1, 15), CNA_SRC(CNA_INPUT, 0),
    CNA_END
};

/* vs_3_0: FOG alone in o1.x. */
static const uint32_t fog_alone_vs3[] = {
    CNA_VS(3, 0),
    CNA_OP(CNA_DCL_OP, 2), CNA_DCL(0, 0), CNA_DST(CNA_INPUT, 0, 15),
    CNA_OP(CNA_DCL_OP, 2), CNA_DCL(0, 0), CNA_DST(CNA_OUTPUT, 0, 15),
    CNA_OP(CNA_DCL_OP, 2), CNA_DCL(11, 0), CNA_DST(CNA_OUTPUT, 1, 0x1),
    CNA_OP(CNA_MOV, 2), CNA_DST(CNA_OUTPUT, 0, 15), CNA_SRC(CNA_INPUT, 0),
    CNA_OP(CNA_MOV, 2), CNA_DST(CNA_OUTPUT, 1, 0x1), CNA_SRC_SWZ(CNA_INPUT, 0, 0xFF),
    CNA_END
};

/* ps_3_0: FOG in v0.x and TEXCOORD0 in v0.yzw, FOG declared first, read whole. */
static const uint32_t fog_first_ps3[] = {
    CNA_PS(3, 0),
    CNA_OP(CNA_DCL_OP, 2), CNA_DCL(11, 0), CNA_DST(CNA_INPUT, 0, 0x1),
    CNA_OP(CNA_DCL_OP, 2), CNA_DCL(5, 0), CNA_DST(CNA_INPUT, 0, 0xE),
    CNA_OP(CNA_MOV, 2), CNA_DST(CNA_COLOROUT, 0, 15), CNA_SRC(CNA_INPUT, 0),
    CNA_END
};

/* Translates `tokens` with `profile` and checks that every `expected` line is in the source. */
static void check_glsl(const char *what, const char *profile, const uint32_t *tokens,
                       size_t bytes, const char *const *expected, size_t count)
{
    const MOJOSHADER_parseData *pd = cna_parse(profile, tokens, bytes);
    char label[160];
    size_t i;
    snprintf(label, sizeof (label), "%s (%s): translates", what, profile);
    CNA_CHECK(cna_report_errors(label, pd) == 0, label);
    for (i = 0; (pd->error_count == 0) && (i < count); i++)
    {
        snprintf(label, sizeof (label), "%s (%s): %s", what, profile, expected[i]);
        CNA_CHECK(cna_output_contains(pd, expected[i]), label);
    } // for
    /* gl_FogFragCoord is a float, and GLSL before 4.20 has no swizzle on a scalar. */
    snprintf(label, sizeof (label), "%s (%s): gl_FogFragCoord takes no swizzle", what, profile);
    CNA_CHECK(!cna_output_contains(pd, "gl_FogFragCoord."), label);
    MOJOSHADER_freeParseData(pd);
} // check_glsl

/*
 * R2-03 (2026-10-26 remediation review): FOG packed with TEXCOORD0 in one vs_3_0 output, in
 * either declaration order. The writes keep their masks and each semantic is copied from its
 * own lanes; gl_FogFragCoord, a float, takes FOG's one lane and fills a pixel shader's FOG
 * lanes. Before, FOG declared first was refused ("writemasks overlap") or, in .w, silently
 * narrowed TEXCOORD0 and every write to .x, and gl_FogFragCoord was swizzled or assigned a
 * vec4, which no GLSL before 4.20 compiles.
 */
static void test_packed_fog(void)
{
    static const char *const desktop[] = { "glsl", "glsl120" };
    static const char *const es[] = { "glsles", "glsles3" };
    static const char *const packed_desktop[] = {
        "vs_o1.yzw = vs_v0.yzw;", "vs_o1.x = vs_v0.w;",
        "gl_TexCoord[0].yzw = vs_o1.yzw;", "gl_FogFragCoord = vs_o1.x;" };
    static const char *const packed_es[] = {
        "vs_o1.yzw = vs_v0.yzw;", "vs_o1.x = vs_v0.w;",
        "io_5_0.yzw = vs_o1.yzw;", "io_11_0.x = vs_o1.x;" };
    static const char *const fog_w_desktop[] = {
        "vs_o1 = vs_v0;", "gl_TexCoord[0].xyz = vs_o1.xyz;", "gl_FogFragCoord = vs_o1.w;" };
    static const char *const fog_w_es[] = {
        "vs_o1 = vs_v0;", "io_5_0.xyz = vs_o1.xyz;", "io_11_0.w = vs_o1.w;" };
    static const char *const alone_desktop[] = { "gl_FogFragCoord = vs_o1.x;" };
    static const char *const ps_desktop[] = {
        "ps_v0.x = gl_FogFragCoord;", "ps_v0.yzw = gl_TexCoord[0].yzw;" };
    static const char *const ps_es[] = { "ps_v0.x = io_11_0.x;", "ps_v0.yzw = io_5_0.yzw;" };
    size_t i;
    for (i = 0; i < 2; i++)
    {
        check_glsl("FOG first", desktop[i], fog_first_vs3, sizeof (fog_first_vs3), packed_desktop, 4);
        check_glsl("FOG last", desktop[i], fog_last_vs3, sizeof (fog_last_vs3), packed_desktop, 4);
        check_glsl("FOG in .w", desktop[i], fog_w_vs3, sizeof (fog_w_vs3), fog_w_desktop, 3);
        check_glsl("FOG alone", desktop[i], fog_alone_vs3, sizeof (fog_alone_vs3), alone_desktop, 1);
        check_glsl("pixel FOG", desktop[i], fog_first_ps3, sizeof (fog_first_ps3), ps_desktop, 2);
        check_glsl("FOG first", es[i], fog_first_vs3, sizeof (fog_first_vs3), packed_es, 4);
        check_glsl("FOG last", es[i], fog_last_vs3, sizeof (fog_last_vs3), packed_es, 4);
        check_glsl("FOG in .w", es[i], fog_w_vs3, sizeof (fog_w_vs3), fog_w_es, 3);
        check_glsl("pixel FOG", es[i], fog_first_ps3, sizeof (fog_first_ps3), ps_es, 2);
    } // for
} // test_packed_fog

static const uint32_t fog_alone_y_vs3[] = CNA_FOG_ALONE_VS3(0x2);
static const uint32_t fog_alone_z_vs3[] = CNA_FOG_ALONE_VS3(0x4);
static const uint32_t fog_alone_w_vs3[] = CNA_FOG_ALONE_VS3(0x8);

/* vs_3_0: FOG alone in o1.z, the register written whole. */
static const uint32_t fog_alone_z_full_vs3[] = {
    CNA_VS(3, 0),
    CNA_OP(CNA_DCL_OP, 2), CNA_DCL(0, 0), CNA_DST(CNA_INPUT, 0, 15),
    CNA_OP(CNA_DCL_OP, 2), CNA_DCL(0, 0), CNA_DST(CNA_OUTPUT, 0, 15),
    CNA_OP(CNA_DCL_OP, 2), CNA_DCL(11, 0), CNA_DST(CNA_OUTPUT, 1, 0x4),
    CNA_OP(CNA_MOV, 2), CNA_DST(CNA_OUTPUT, 0, 15), CNA_SRC(CNA_INPUT, 0),
    CNA_OP(CNA_MOV, 2), CNA_DST(CNA_OUTPUT, 1, 15), CNA_SRC(CNA_INPUT, 0),
    CNA_END
};

/*
 * R5-02 (2026-10-26 remediation review, round 2): FOG alone in a vs_3_0 output lane other than
 * x. The parser narrowed every write to such a register to .x, so `mov o1.z, v0` became
 * `vs_o1.x = vs_v0.x`; desktop GLSL then copied that x to gl_FogFragCoord (v0.x where FOG is
 * v0.z) and GLSL ES copied the never-written z to its varying. The write keeps its lane and
 * FOG is copied from it.
 */
static void test_standalone_fog_lanes(void)
{
    static const struct { const char *lane; const uint32_t *tokens; size_t bytes; } vs[] = {
        { "y", fog_alone_y_vs3, sizeof (fog_alone_y_vs3) },
        { "z", fog_alone_z_vs3, sizeof (fog_alone_z_vs3) },
        { "w", fog_alone_w_vs3, sizeof (fog_alone_w_vs3) },
    };
    size_t i, j;
    for (i = 0; i < sizeof (vs) / sizeof (vs[0]); i++)
    {
        char write[64], desktop_copy[64], es_copy[64];
        const char *desktop[2], *es[2];
        snprintf(write, sizeof (write), "vs_o1.%s = vs_v0.%s;", vs[i].lane, vs[i].lane);
        snprintf(desktop_copy, sizeof (desktop_copy), "gl_FogFragCoord = vs_o1.%s;", vs[i].lane);
        snprintf(es_copy, sizeof (es_copy), "io_11_0.%s = vs_o1.%s;", vs[i].lane, vs[i].lane);
        desktop[0] = es[0] = write;
        desktop[1] = desktop_copy;
        es[1] = es_copy;
        for (j = 0; j < sizeof (glsl_profiles) / sizeof (glsl_profiles[0]); j++)
        {
            const int is_es = (strncmp(glsl_profiles[j], "glsles", 6) == 0);
            check_glsl("FOG alone", glsl_profiles[j], vs[i].tokens, vs[i].bytes,
                       is_es ? es : desktop, 2);
        } // for
    } // for
    {
        static const char *const desktop[] = { "vs_o1 = vs_v0;", "gl_FogFragCoord = vs_o1.z;" };
        static const char *const es[] = { "vs_o1 = vs_v0;", "io_11_0.z = vs_o1.z;" };
        for (j = 0; j < sizeof (glsl_profiles) / sizeof (glsl_profiles[0]); j++)
        {
            const int is_es = (strncmp(glsl_profiles[j], "glsles", 6) == 0);
            check_glsl("FOG alone in .z, written whole", glsl_profiles[j], fog_alone_z_full_vs3,
                       sizeof (fog_alone_z_full_vs3), is_es ? es : desktop, 2);
        } // for
    } // block
} // test_standalone_fog_lanes

/* vs_2_0: oPos = v0, oFog = v0.z. */
static const uint32_t ofog_vs2[] = {
    CNA_VS(2, 0),
    CNA_OP(CNA_DCL_OP, 2), CNA_DCL(0, 0), CNA_DST(CNA_INPUT, 0, 15),
    CNA_OP(CNA_MOV, 2), CNA_DST(CNA_RASTOUT, 0, 15), CNA_SRC(CNA_INPUT, 0),
    CNA_OP(CNA_MOV, 2), CNA_DST(CNA_RASTOUT, 1, 15), CNA_SRC_SWZ(CNA_INPUT, 0, 0xAA),
    CNA_END
};

/*
 * R5-01 (2026-10-26 remediation review, round 2), the GLSL varyings: a vs_2_0 oFog is a scalar
 * register, written `vs_oFog = vs_v0.z;`, but GLSL ES (and every profile's centroid variant)
 * stood it for a vec4 varying, `#define vs_oFog io_11_0` -- a float assigned to a vec4, which no
 * GLSL compiles. The varying stays a vec4, as a ps_3_0 FOG input declares it, and takes the
 * float in its x.
 */
static void test_vs2_fog_varying(void)
{
    size_t i;
    for (i = 0; i < sizeof (glsl_profiles) / sizeof (glsl_profiles[0]); i++)
    {
        const int is_es = (strncmp(glsl_profiles[i], "glsles", 6) == 0);
        const char *const expected[] = {
            "vs_oFog = vs_v0.z;", "#define vs_oFog cna_centroid_11_0.x\n",
            is_es ? "#define vs_oFog io_11_0.x\n" : "#define vs_oFog gl_FogFragCoord\n" };
        check_glsl("vs_2_0 oFog", glsl_profiles[i], ofog_vs2, sizeof (ofog_vs2), expected, 3);
    } // for
} // test_vs2_fog_varying

int main(void)
{
    cna_test_run("color_written_in_subroutine_is_clamped",
                 test_color_written_in_subroutine_is_clamped);
    cna_test_run("epilogue_stays_inline_without_subroutines",
                 test_epilogue_stays_inline_without_subroutines);
    cna_test_run("packed_fog", test_packed_fog);
    cna_test_run("standalone_fog_lanes", test_standalone_fog_lanes);
    cna_test_run("vs2_fog_varying", test_vs2_fog_varying);
    return cna_test_finish();
} // main
