/**
 * SPIR-V profile regressions.
 *
 * This file is part of the libcna fork of MojoShader; see tests/cna/README.md.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cna_test.h"
#include "spirv/spirv.h"

#define __MOJOSHADER_INTERNAL__ 1
#include "mojoshader_internal.h"  /* MOJOSHADER_spirv_link_attributes(), as the GL glue links */

#define MAX_IDS 4096

/* A scalar or vector type: its component type (SHAPE_*) times 16 plus its component count. */
enum { SHAPE_FLOAT = 1, SHAPE_INT = 2, SHAPE_UINT = 3, SHAPE_BOOL = 4 };

/* What the tests need to know about a module, read from its words. */
typedef struct SpirvFacts
{
    int variables;
    int duplicate_variables;
    int storage[MAX_IDS];       /* -1: not a variable */
    int location[MAX_IDS];      /* -1: no Location decoration */
    int builtin[MAX_IDS];       /* -1: no BuiltIn decoration */
    int var_type[MAX_IDS];      /* a variable's pointer type, -1 when not a variable */
    int pointee[MAX_IDS];       /* a pointer type's pointee type, -1 when not a pointer */
    int shape[MAX_IDS];         /* a scalar or vector type's shape, 0 when neither */
    int interface_ids[64];
    int interface_count;
} SpirvFacts;

/* The shape of the type a variable points to; 0 when it is not a scalar or vector. */
static int variable_shape(const SpirvFacts *facts, int id)
{
    int ptr, pointee;
    if ((id < 0) || (id >= MAX_IDS) || ((ptr = facts->var_type[id]) < 0) || (ptr >= MAX_IDS) ||
        ((pointee = facts->pointee[ptr]) < 0) || (pointee >= MAX_IDS))
        return 0;
    return facts->shape[pointee];
} // variable_shape

static void read_facts(const MOJOSHADER_parseData *pd, SpirvFacts *facts)
{
    const uint32_t *words = (const uint32_t *) pd->output;
    const size_t count = cna_spirv_module_words(pd);
    size_t at = 5;
    int i;

    memset(facts, '\0', sizeof (*facts));
    for (i = 0; i < MAX_IDS; i++)
    {
        facts->storage[i] = -1;
        facts->location[i] = -1;
        facts->builtin[i] = -1;
        facts->var_type[i] = -1;
        facts->pointee[i] = -1;
    } // for

    while (at < count)
    {
        const uint32_t length = words[at] >> 16;
        const uint32_t opcode = words[at] & 0xFFFF;
        if ((length == 0) || (at + length > count))
            break;
        if ((opcode == SpvOpVariable) && (length >= 4) && (words[at + 2] < MAX_IDS))
        {
            if (facts->storage[words[at + 2]] != -1)
                facts->duplicate_variables++;
            facts->storage[words[at + 2]] = (int) words[at + 3];
            facts->var_type[words[at + 2]] = (int) words[at + 1];
            facts->variables++;
        } // if
        else if ((opcode == SpvOpTypeFloat) && (length == 3) && (words[at + 1] < MAX_IDS))
            facts->shape[words[at + 1]] = SHAPE_FLOAT * 16 + 1;
        else if ((opcode == SpvOpTypeInt) && (length == 4) && (words[at + 1] < MAX_IDS))
            facts->shape[words[at + 1]] = (words[at + 3] ? SHAPE_INT : SHAPE_UINT) * 16 + 1;
        else if ((opcode == SpvOpTypeBool) && (length == 2) && (words[at + 1] < MAX_IDS))
            facts->shape[words[at + 1]] = SHAPE_BOOL * 16 + 1;
        else if ((opcode == SpvOpTypeVector) && (length == 4) && (words[at + 1] < MAX_IDS) &&
                 (words[at + 2] < MAX_IDS))
            facts->shape[words[at + 1]] = (facts->shape[words[at + 2]] & ~15) + (int) words[at + 3];
        else if ((opcode == SpvOpTypePointer) && (length == 4) && (words[at + 1] < MAX_IDS))
            facts->pointee[words[at + 1]] = (int) words[at + 3];
        else if ((opcode == SpvOpDecorate) && (length >= 4) && (words[at + 1] < MAX_IDS))
        {
            if (words[at + 2] == SpvDecorationLocation)
                facts->location[words[at + 1]] = (int) words[at + 3];
            else if (words[at + 2] == SpvDecorationBuiltIn)
                facts->builtin[words[at + 1]] = (int) words[at + 3];
        } // else if
        else if ((opcode == SpvOpEntryPoint) && (length >= 4))
        {
            /* model, function id, literal name (NUL-terminated, padded), interface ids */
            size_t name = at + 3;
            while ((name < at + length) && ((words[name] >> 24) != 0))
                name++;
            for (name++; (name < at + length) && (facts->interface_count < 64); name++)
                facts->interface_ids[facts->interface_count++] = (int) words[name];
        } // else if
        at += length;
    } // while
} // read_facts

/*
 * Locations of the user (non-builtin) interface variables of one storage class, sorted, and the
 * shape of each variable's type in `shapes`, in the same order.
 */
static int user_locations(const SpirvFacts *facts, int storage, int *out, int *shapes, int max)
{
    int i, j, n = 0;
    for (i = 0; i < facts->interface_count; i++)
    {
        const int id = facts->interface_ids[i];
        if ((id < 0) || (id >= MAX_IDS) || (facts->storage[id] != storage))
            continue;
        if (facts->builtin[id] != -1)
            continue;
        if (n < max)
        {
            shapes[n] = variable_shape(facts, id);
            out[n++] = facts->location[id];
        } // if
    } // for
    for (i = 1; i < n; i++)
        for (j = i; (j > 0) && (out[j - 1] > out[j]); j--)
        {
            const int t = out[j];
            const int u = shapes[j];
            out[j] = out[j - 1];
            shapes[j] = shapes[j - 1];
            out[j - 1] = t;
            shapes[j - 1] = u;
        } // for
    return n;
} // user_locations

/* Every interface variable is an Input or Output variable defined exactly once. */
static void check_interface(const SpirvFacts *facts, const char *what)
{
    int i;
    CNA_CHECK(facts->duplicate_variables == 0, what);
    for (i = 0; i < facts->interface_count; i++)
    {
        const int id = facts->interface_ids[i];
        const int ok = (id >= 0) && (id < MAX_IDS) &&
                       ((facts->storage[id] == SpvStorageClassInput) ||
                        (facts->storage[id] == SpvStorageClassOutput));
        if (!ok)
            fprintf(stderr, "  %s: interface id %d is not an Input/Output variable\n", what, id);
        CNA_CHECK(ok, what);
    } // for
} // check_interface

/* vs_3_0: POSITION in o0, TEXCOORD0 in o1.xy and TEXCOORD1 in o1.zw (how fxc packs two float2). */
static const uint32_t packed_vs3[] = {
    CNA_VS(3, 0),
    CNA_OP(CNA_DCL_OP, 2), CNA_DCL(0, 0), CNA_DST(CNA_INPUT, 0, 15),
    CNA_OP(CNA_DCL_OP, 2), CNA_DCL(0, 0), CNA_DST(CNA_OUTPUT, 0, 15),
    CNA_OP(CNA_DCL_OP, 2), CNA_DCL(5, 0), CNA_DST(CNA_OUTPUT, 1, 3),
    CNA_OP(CNA_DCL_OP, 2), CNA_DCL(5, 1), CNA_DST(CNA_OUTPUT, 1, 12),
    CNA_OP(CNA_MOV, 2), CNA_DST(CNA_OUTPUT, 0, 15), CNA_SRC(CNA_INPUT, 0),
    CNA_OP(CNA_MOV, 2), CNA_DST(CNA_OUTPUT, 1, 15), CNA_SRC(CNA_INPUT, 0),
    CNA_END
};

/* vs_3_0 with the same semantics in separate registers. */
static const uint32_t plain_vs3[] = {
    CNA_VS(3, 0),
    CNA_OP(CNA_DCL_OP, 2), CNA_DCL(0, 0), CNA_DST(CNA_INPUT, 0, 15),
    CNA_OP(CNA_DCL_OP, 2), CNA_DCL(0, 0), CNA_DST(CNA_OUTPUT, 0, 15),
    CNA_OP(CNA_DCL_OP, 2), CNA_DCL(5, 0), CNA_DST(CNA_OUTPUT, 1, 15),
    CNA_OP(CNA_DCL_OP, 2), CNA_DCL(5, 1), CNA_DST(CNA_OUTPUT, 2, 15),
    CNA_OP(CNA_MOV, 2), CNA_DST(CNA_OUTPUT, 0, 15), CNA_SRC(CNA_INPUT, 0),
    CNA_OP(CNA_MOV, 2), CNA_DST(CNA_OUTPUT, 1, 15), CNA_SRC(CNA_INPUT, 0),
    CNA_OP(CNA_MOV, 2), CNA_DST(CNA_OUTPUT, 2, 15), CNA_SRC(CNA_INPUT, 0),
    CNA_END
};

/* ps_3_0: TEXCOORD0 in v0.xy and TEXCOORD1 in v0.zw, read whole. */
static const uint32_t packed_ps3[] = {
    CNA_PS(3, 0),
    CNA_OP(CNA_DCL_OP, 2), CNA_DCL(5, 0), CNA_DST(CNA_INPUT, 0, 3),
    CNA_OP(CNA_DCL_OP, 2), CNA_DCL(5, 1), CNA_DST(CNA_INPUT, 0, 12),
    CNA_OP(CNA_MOV, 2), CNA_DST(CNA_COLOROUT, 0, 15), CNA_SRC(CNA_INPUT, 0),
    CNA_END
};

/* ps_3_0: TEXCOORD0 and TEXCOORD1 in separate registers. */
static const uint32_t plain_ps3[] = {
    CNA_PS(3, 0),
    CNA_OP(CNA_DCL_OP, 2), CNA_DCL(5, 0), CNA_DST(CNA_INPUT, 0, 15),
    CNA_OP(CNA_DCL_OP, 2), CNA_DCL(5, 1), CNA_DST(CNA_INPUT, 1, 15),
    CNA_OP(CNA_ADD, 3), CNA_DST(CNA_COLOROUT, 0, 15), CNA_SRC(CNA_INPUT, 0), CNA_SRC(CNA_INPUT, 1),
    CNA_END
};

/* vs_2_0: oPos = v0, oFog = v0.x, oPts = v0.x -- the RASTOUT registers 0, 1 and 2. */
static const uint32_t rastout_vs2[] = {
    CNA_VS(2, 0),
    CNA_OP(CNA_DCL_OP, 2), CNA_DCL(0, 0), CNA_DST(CNA_INPUT, 0, 15),
    CNA_OP(CNA_MOV, 2), CNA_DST(4, 0, 15), CNA_SRC(CNA_INPUT, 0),
    CNA_OP(CNA_MOV, 2), CNA_DST(4, 1, 1), CNA_SRC_SWZ(CNA_INPUT, 0, 0x00),
    CNA_OP(CNA_MOV, 2), CNA_DST(4, 2, 1), CNA_SRC_SWZ(CNA_INPUT, 0, 0x00),
    CNA_END
};

/* ps_3_0: dcl_fog v0.x, written to oC0. */
static const uint32_t fog_ps3[] = {
    CNA_PS(3, 0),
    CNA_OP(CNA_DCL_OP, 2), CNA_DCL(11, 0), CNA_DST(CNA_INPUT, 0, 1),
    CNA_OP(CNA_MOV, 2), CNA_DST(CNA_COLOROUT, 0, 15), CNA_SRC_SWZ(CNA_INPUT, 0, 0x00),
    CNA_END
};

/*
 * Translates, links and checks one vertex/pixel pair, in the Vulkan SPIR-V profile (linked by
 * MOJOSHADER_linkSPIRVShaders(), as the SDL_GPU glue links) and the GLSPIRV one (linked as the GL
 * glue links). Each varying must link to one Location, and its vertex Output and pixel Input
 * there must have the same type: the Location sets alone matched for FOG while the vertex side
 * was a float and the pixel side a vec4 (R5-01, 2026-10-26 remediation review, round 2).
 */
static void check_pair_profile(const char *name, const char *profile,
                               const uint32_t *vs, size_t vs_bytes,
                               const uint32_t *ps, size_t ps_bytes, int expected_user_varyings)
{
    MOJOSHADER_vertexAttribute position;
    const int glspirv = (strcmp(profile, "glspirv") == 0);
    const MOJOSHADER_parseData *v = cna_parse(profile, vs, vs_bytes);
    const MOJOSHADER_parseData *p = cna_parse(profile, ps, ps_bytes);
    char label[160];
    SpirvFacts vf, pf;
    int vlocs[16], plocs[16], vshapes[16], pshapes[16];
    int vn, pn, i;

    snprintf(label, sizeof (label), "%s %s: vertex shader translates", name, profile);
    CNA_CHECK(cna_report_errors(label, v) == 0, label);
    snprintf(label, sizeof (label), "%s %s: pixel shader translates", name, profile);
    CNA_CHECK(cna_report_errors(label, p) == 0, label);
    if ((v == NULL) || (p == NULL) || v->error_count || p->error_count)
        goto done;

    if (glspirv)
        MOJOSHADER_spirv_link_attributes(v, p, 1);
    else
    {
        memset(&position, '\0', sizeof (position));
        position.usage = MOJOSHADER_USAGE_POSITION;
        position.vertexElementFormat = MOJOSHADER_VERTEXELEMENTFORMAT_VECTOR4;
        MOJOSHADER_linkSPIRVShaders(v, p, &position, 1);
    } // else

    read_facts(v, &vf);
    read_facts(p, &pf);
    snprintf(label, sizeof (label), "%s %s: vertex interface", name, profile);
    check_interface(&vf, label);
    snprintf(label, sizeof (label), "%s %s: pixel interface", name, profile);
    check_interface(&pf, label);

    vn = user_locations(&vf, SpvStorageClassOutput, vlocs, vshapes, 16);
    pn = user_locations(&pf, SpvStorageClassInput, plocs, pshapes, 16);
    snprintf(label, sizeof (label), "%s %s: one vertex output per varying semantic", name, profile);
    CNA_CHECK(vn == expected_user_varyings, label);
    snprintf(label, sizeof (label), "%s %s: one pixel input per varying semantic", name, profile);
    CNA_CHECK(pn == expected_user_varyings, label);
    if ((vn == pn) && (vn == expected_user_varyings))
        for (i = 0; i < vn; i++)
        {
            snprintf(label, sizeof (label), "%s %s: varying %d links to the same Location",
                     name, profile, i);
            CNA_CHECK((vlocs[i] == plocs[i]) && (vlocs[i] >= 0), label);
            snprintf(label, sizeof (label),
                     "%s %s: the Output and the Input at Location %d have one type", name,
                     profile, vlocs[i]);
            if (vshapes[i] != pshapes[i])
                fprintf(stderr, "  %s: vertex type %d components (base %d), pixel %d (base %d)\n",
                        label, vshapes[i] & 15, vshapes[i] >> 4, pshapes[i] & 15, pshapes[i] >> 4);
            CNA_CHECK((vshapes[i] == pshapes[i]) && (vshapes[i] != 0), label);
            if (i > 0)
            {
                snprintf(label, sizeof (label), "%s %s: varying Locations are distinct",
                         name, profile);
                CNA_CHECK(vlocs[i] != vlocs[i - 1], label);
            } // if
        } // for

    snprintf(label, sizeof (label), "%s_%s_vs", name, profile);
    CNA_CHECK(cna_spirv_validate(v, label) != 0, "spirv-val accepts the vertex shader");
    snprintf(label, sizeof (label), "%s_%s_ps", name, profile);
    CNA_CHECK(cna_spirv_validate(p, label) != 0, "spirv-val accepts the pixel shader");

done:
    MOJOSHADER_freeParseData(v);
    MOJOSHADER_freeParseData(p);
} // check_pair_profile

static void check_pair(const char *name, const uint32_t *vs, size_t vs_bytes,
                       const uint32_t *ps, size_t ps_bytes, int expected_user_varyings)
{
    check_pair_profile(name, "spirv", vs, vs_bytes, ps, ps_bytes, expected_user_varyings);
    check_pair_profile(name, "glspirv", vs, vs_bytes, ps, ps_bytes, expected_user_varyings);
} // check_pair

/* A pre-vs_3_0 oFog is FOG0 and pairs with the pixel shader's FOG0 input, and oPts is the
 * PointSize built-in, not a user varying: one user varying on each side, at one Location. Before,
 * the parser listed them as FOG1 and PSIZE2, and linking the pair wrote a Location over the
 * module's magic number. */
static void test_rastout_semantics(void)
{
    const MOJOSHADER_parseData *v = cna_parse("spirv", rastout_vs2, sizeof (rastout_vs2));
    SpirvFacts facts;
    int id, pointsize = 0;

    check_pair("rastout", rastout_vs2, sizeof (rastout_vs2), fog_ps3, sizeof (fog_ps3), 1);
    CNA_CHECK(cna_report_errors("rastout vs", v) == 0, "the vs_2_0 RASTOUT shader translates");
    if ((v != NULL) && (v->error_count == 0))
    {
        read_facts(v, &facts);
        for (id = 0; id < MAX_IDS; id++)
            if ((facts.storage[id] == SpvStorageClassOutput) &&
                (facts.builtin[id] == SpvBuiltInPointSize))
                pointsize++;
        CNA_CHECK(pointsize == 1, "oPts is the PointSize built-in");
    } // if
    MOJOSHADER_freeParseData(v);

    /* The GLSL profile maps the same registers onto its built-ins. */
    v = cna_parse("glsl120", rastout_vs2, sizeof (rastout_vs2));
    CNA_CHECK(cna_report_errors("rastout glsl", v) == 0, "the vs_2_0 RASTOUT shader translates to GLSL");
    CNA_CHECK(cna_output_contains(v, "#define vs_oPts gl_PointSize"), "GLSL writes oPts to gl_PointSize");
    CNA_CHECK(cna_output_contains(v, "#define vs_oFog gl_FogFragCoord"), "GLSL writes oFog as FOG0");
    MOJOSHADER_freeParseData(v);
} // test_rastout_semantics

/* A2v2 A-02: packed vs_3_0 outputs; unpacked pixel inputs. */
static void test_packed_vertex_outputs(void)
{
    check_pair("packed_vs_plain_ps", packed_vs3, sizeof (packed_vs3), plain_ps3, sizeof (plain_ps3), 2);
} // test_packed_vertex_outputs

/* A2v2 A-02: packed ps_3_0 inputs; unpacked vertex outputs. */
static void test_packed_pixel_inputs(void)
{
    check_pair("plain_vs_packed_ps", plain_vs3, sizeof (plain_vs3), packed_ps3, sizeof (packed_ps3), 2);
} // test_packed_pixel_inputs

/* Both sides packed. */
static void test_packed_both(void)
{
    check_pair("packed_vs_packed_ps", packed_vs3, sizeof (packed_vs3), packed_ps3, sizeof (packed_ps3), 2);
} // test_packed_both

/* Control: the ordinary layout. */
static void test_plain_pair(void)
{
    check_pair("plain_vs_plain_ps", plain_vs3, sizeof (plain_vs3), plain_ps3, sizeof (plain_ps3), 2);
} // test_plain_pair

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

/* ps_3_0: FOG in v0.x and TEXCOORD0 in v0.yzw, FOG declared first, read whole. */
static const uint32_t fog_first_ps3[] = {
    CNA_PS(3, 0),
    CNA_OP(CNA_DCL_OP, 2), CNA_DCL(11, 0), CNA_DST(CNA_INPUT, 0, 0x1),
    CNA_OP(CNA_DCL_OP, 2), CNA_DCL(5, 0), CNA_DST(CNA_INPUT, 0, 0xE),
    CNA_OP(CNA_MOV, 2), CNA_DST(CNA_COLOROUT, 0, 15), CNA_SRC(CNA_INPUT, 0),
    CNA_END
};

/* The same, TEXCOORD0 declared first. */
static const uint32_t fog_last_ps3[] = {
    CNA_PS(3, 0),
    CNA_OP(CNA_DCL_OP, 2), CNA_DCL(5, 0), CNA_DST(CNA_INPUT, 0, 0xE),
    CNA_OP(CNA_DCL_OP, 2), CNA_DCL(11, 0), CNA_DST(CNA_INPUT, 0, 0x1),
    CNA_OP(CNA_MOV, 2), CNA_DST(CNA_COLOROUT, 0, 15), CNA_SRC(CNA_INPUT, 0),
    CNA_END
};

/*
 * The lane a vertex module stores to its one Location-decorated float Output (FOG): the index
 * of the OpCompositeExtract whose result is stored there. -1 when there is no such output, it
 * is stored something else, or there is more than one.
 */
static int fog_output_lane(const MOJOSHADER_parseData *pd)
{
    static int extract_lane[MAX_IDS];
    const uint32_t *words = (const uint32_t *) pd->output;
    const size_t count = cna_spirv_module_words(pd);
    uint32_t float_type = 0, float_output = 0, fog = 0;
    int fogs = 0, lane = -1;
    size_t at;
    SpirvFacts facts;

    read_facts(pd, &facts);
    memset(extract_lane, 0xFF, sizeof (extract_lane));
    for (at = 5; at < count; at += words[at] >> 16)
    {
        const uint32_t length = words[at] >> 16;
        const uint32_t opcode = words[at] & 0xFFFF;
        if ((length == 0) || (at + length > count))
            return -1;
        if ((opcode == SpvOpTypeFloat) && (length == 3) && (words[at + 2] == 32))
            float_type = words[at + 1];
        else if ((opcode == SpvOpTypePointer) && (length == 4) && (words[at + 3] == float_type) &&
                 (words[at + 2] == SpvStorageClassOutput))
            float_output = words[at + 1];
        else if ((opcode == SpvOpVariable) && (length >= 4) && (words[at + 1] == float_output) &&
                 (words[at + 2] < MAX_IDS) && (facts.location[words[at + 2]] != -1))
        {
            fog = words[at + 2];
            fogs++;
        } // else if
        else if ((opcode == SpvOpCompositeExtract) && (length == 5) && (words[at + 2] < MAX_IDS))
            extract_lane[words[at + 2]] = (int) words[at + 4];
        else if ((opcode == SpvOpStore) && (length == 3) && (fog != 0) && (words[at + 1] == fog) &&
                 (words[at + 2] < MAX_IDS))
            lane = extract_lane[words[at + 2]];
    } // for
    return (fogs == 1) ? lane : -1;
} // fog_output_lane

/*
 * R2-03 (2026-10-26 remediation review): FOG packed with TEXCOORD0 in one register, in either
 * declaration order, on either side. FOG is a float Output as in a register of its own, stored
 * from its own lane; each semantic links to its own Location. Before, the parser narrowed the
 * register by its first semantic (FOG first: a false "writemasks overlap", or with FOG in .w
 * TEXCOORD0 and every write silently narrowed to .x), and the profile refused FOG in a packed
 * register outright.
 */
static void test_packed_fog(void)
{
    static const struct { const char *name; const uint32_t *tokens; size_t bytes; int lane; } vs[] = {
        { "fog_first_vs", fog_first_vs3, sizeof (fog_first_vs3), 0 },
        { "fog_last_vs", fog_last_vs3, sizeof (fog_last_vs3), 0 },
        { "fog_w_vs", fog_w_vs3, sizeof (fog_w_vs3), 3 },
    };
    static const struct { const char *name; const uint32_t *tokens; size_t bytes; } ps[] = {
        { "fog_first_ps", fog_first_ps3, sizeof (fog_first_ps3) },
        { "fog_last_ps", fog_last_ps3, sizeof (fog_last_ps3) },
    };
    size_t i, j;
    char label[128];

    for (i = 0; i < sizeof (vs) / sizeof (vs[0]); i++)
    {
        for (j = 0; j < sizeof (ps) / sizeof (ps[0]); j++)
        {
            snprintf(label, sizeof (label), "%s_%s", vs[i].name, ps[j].name);
            check_pair(label, vs[i].tokens, vs[i].bytes, ps[j].tokens, ps[j].bytes, 2);
        } // for

        {
            static const char *profiles[2] = { "spirv", "glspirv" };
            int p;
            for (p = 0; p < 2; p++)
            {
                const MOJOSHADER_parseData *pd = cna_parse(profiles[p], vs[i].tokens, vs[i].bytes);
                snprintf(label, sizeof (label), "%s %s", vs[i].name, profiles[p]);
                CNA_CHECK(cna_report_errors(label, pd) == 0, "the vertex shader translates");
                if (pd->error_count == 0)
                {
                    CNA_CHECK(fog_output_lane(pd) == vs[i].lane,
                              "FOG is one float output, stored from its own lane");
                    snprintf(label, sizeof (label), "%s_%s", vs[i].name, profiles[p]);
                    CNA_CHECK(cna_spirv_validate(pd, label) != 0, "spirv-val accepts it");
                } // if
                MOJOSHADER_freeParseData(pd);
            } // for
        } // block
    } // for
} // test_packed_fog

/*
 * The lanes of the register a pixel module fills from its one Location-decorated float Input
 * (FOG): those an OpVectorShuffle takes from an OpCompositeConstruct of four loads of that
 * Input. -1 when there is no such Input or more than one.
 */
static int fog_input_lanes(const MOJOSHADER_parseData *pd)
{
    static unsigned char loaded[MAX_IDS], splat[MAX_IDS];
    const uint32_t *words = (const uint32_t *) pd->output;
    const size_t count = cna_spirv_module_words(pd);
    int fog = -1, fogs = 0, lanes = 0, id, i;
    size_t at;
    SpirvFacts facts;

    read_facts(pd, &facts);
    for (id = 0; id < MAX_IDS; id++)
        if ((facts.storage[id] == SpvStorageClassInput) && (facts.location[id] != -1) &&
            (variable_shape(&facts, id) == SHAPE_FLOAT * 16 + 1))
        {
            fog = id;
            fogs++;
        } // if
    if (fogs != 1)
        return -1;

    memset(loaded, '\0', sizeof (loaded));
    memset(splat, '\0', sizeof (splat));
    for (at = 5; at < count; at += words[at] >> 16)
    {
        const uint32_t length = words[at] >> 16;
        const uint32_t opcode = words[at] & 0xFFFF;
        if ((length == 0) || (at + length > count))
            return -1;
        if ((opcode == SpvOpLoad) && (length == 4) && (words[at + 3] == (uint32_t) fog) &&
            (words[at + 2] < MAX_IDS))
            loaded[words[at + 2]] = 1;
        else if ((opcode == SpvOpCompositeConstruct) && (length == 7) && (words[at + 2] < MAX_IDS))
        {
            int all = 1;
            for (i = 0; i < 4; i++)
                all = all && (words[at + 3 + i] < MAX_IDS) && loaded[words[at + 3 + i]];
            splat[words[at + 2]] = (unsigned char) all;
        } // else if
        else if ((opcode == SpvOpVectorShuffle) && (length == 9) && (words[at + 3] < MAX_IDS) &&
                 splat[words[at + 3]])
        {
            for (i = 0; i < 4; i++)
                if (words[at + 5 + i] < 4)
                    lanes |= 1 << i;
        } // else if
    } // for
    return lanes;
} // fog_input_lanes

/* vs_3_0: FOG alone in o1.x, written from v0.w. */
static const uint32_t fog_alone_vs3[] = {
    CNA_VS(3, 0),
    CNA_OP(CNA_DCL_OP, 2), CNA_DCL(0, 0), CNA_DST(CNA_INPUT, 0, 15),
    CNA_OP(CNA_DCL_OP, 2), CNA_DCL(0, 0), CNA_DST(CNA_OUTPUT, 0, 15),
    CNA_OP(CNA_DCL_OP, 2), CNA_DCL(11, 0), CNA_DST(CNA_OUTPUT, 1, 0x1),
    CNA_OP(CNA_MOV, 2), CNA_DST(CNA_OUTPUT, 0, 15), CNA_SRC(CNA_INPUT, 0),
    CNA_OP(CNA_MOV, 2), CNA_DST(CNA_OUTPUT, 1, 0x1), CNA_SRC_SWZ(CNA_INPUT, 0, 0xFF),
    CNA_END
};

/* ps_3_0: dcl_fog v0.<lane>, its lane written to oC0. */
static const uint32_t fog_y_ps3[] = CNA_FOG_ALONE_PS3(0x2, 0x55);
static const uint32_t fog_z_ps3[] = CNA_FOG_ALONE_PS3(0x4, 0xAA);
static const uint32_t fog_w_ps3[] = CNA_FOG_ALONE_PS3(0x8, 0xFF);

/*
 * R5-01 (2026-10-26 remediation review, round 2): a pixel FOG input is a float Input, as the
 * vertex shader's FOG output is, at the same Location, and its value fills the register's
 * declared FOG lane -- FOG alone in a register (x, y, z or w), FOG packed with TEXCOORD0 in
 * either declaration order, and the vs_2_0 oFog. Before, the pixel side was a vec4 Input: a
 * type-incompatible stage interface in Vulkan SPIR-V and GLSPIRV alike.
 */
static void test_fog_interface_types(void)
{
    static const struct { const char *name; const uint32_t *tokens; size_t bytes; int mask; } ps[] = {
        { "fog_x_ps", fog_ps3, sizeof (fog_ps3), 0x1 },
        { "fog_y_ps", fog_y_ps3, sizeof (fog_y_ps3), 0x2 },
        { "fog_z_ps", fog_z_ps3, sizeof (fog_z_ps3), 0x4 },
        { "fog_w_ps", fog_w_ps3, sizeof (fog_w_ps3), 0x8 },
        { "fog_first_ps", fog_first_ps3, sizeof (fog_first_ps3), 0x1 },
        { "fog_last_ps", fog_last_ps3, sizeof (fog_last_ps3), 0x1 },
    };
    static const char *profiles[2] = { "spirv", "glspirv" };
    char label[128];
    size_t i;
    int p;

    for (i = 0; i < sizeof (ps) / sizeof (ps[0]); i++)
    {
        /* The packed pixel shaders' pairs, with TEXCOORD0 on both sides, are test_packed_fog's. */
        if ((ps[i].tokens != fog_first_ps3) && (ps[i].tokens != fog_last_ps3))
        {
            snprintf(label, sizeof (label), "fog_alone_vs_%s", ps[i].name);
            check_pair(label, fog_alone_vs3, sizeof (fog_alone_vs3), ps[i].tokens, ps[i].bytes, 1);
            snprintf(label, sizeof (label), "rastout_vs_%s", ps[i].name);
            check_pair(label, rastout_vs2, sizeof (rastout_vs2), ps[i].tokens, ps[i].bytes, 1);
        } // if
        for (p = 0; p < 2; p++)
        {
            const MOJOSHADER_parseData *pd = cna_parse(profiles[p], ps[i].tokens, ps[i].bytes);
            snprintf(label, sizeof (label), "%s %s", ps[i].name, profiles[p]);
            CNA_CHECK(cna_report_errors(label, pd) == 0, "the pixel shader translates");
            if (pd->error_count == 0)
            {
                const int lanes = fog_input_lanes(pd);
                if (lanes != ps[i].mask)
                    fprintf(stderr, "  %s: FOG fills lanes 0x%x, declared 0x%x\n", label,
                            lanes, ps[i].mask);
                CNA_CHECK(lanes == ps[i].mask,
                          "one float FOG input fills exactly the register's declared FOG lane");
            } // if
            MOJOSHADER_freeParseData(pd);
        } // for
    } // for
} // test_fog_interface_types

static const uint32_t fog_alone_x_vs3[] = CNA_FOG_ALONE_VS3(0x1);
static const uint32_t fog_alone_y_vs3[] = CNA_FOG_ALONE_VS3(0x2);
static const uint32_t fog_alone_z_vs3[] = CNA_FOG_ALONE_VS3(0x4);
static const uint32_t fog_alone_w_vs3[] = CNA_FOG_ALONE_VS3(0x8);

/*
 * R5-02 (2026-10-26 remediation review, round 2): FOG alone in a vs_3_0 output lane other than
 * x. The register was the float Output itself and the parser narrowed every write to it to .x,
 * so `mov o1.z, v0` stored v0.x. FOG is now that register's z, stored to the float Output where
 * main returns -- the same as FOG sharing a register -- and each pair links to a pixel FOG in
 * the same lane with one type at one Location.
 */
static void test_standalone_fog_lanes(void)
{
    static const struct { const char *name; const uint32_t *vs; size_t vs_bytes;
                          const uint32_t *ps; size_t ps_bytes; int lane; } cases[] = {
        { "fog_alone_x", fog_alone_x_vs3, sizeof (fog_alone_x_vs3), fog_ps3, sizeof (fog_ps3), 0 },
        { "fog_alone_y", fog_alone_y_vs3, sizeof (fog_alone_y_vs3), fog_y_ps3, sizeof (fog_y_ps3), 1 },
        { "fog_alone_z", fog_alone_z_vs3, sizeof (fog_alone_z_vs3), fog_z_ps3, sizeof (fog_z_ps3), 2 },
        { "fog_alone_w", fog_alone_w_vs3, sizeof (fog_alone_w_vs3), fog_w_ps3, sizeof (fog_w_ps3), 3 },
    };
    static const char *profiles[2] = { "spirv", "glspirv" };
    char label[128];
    size_t i;
    int p;

    for (i = 0; i < sizeof (cases) / sizeof (cases[0]); i++)
    {
        check_pair(cases[i].name, cases[i].vs, cases[i].vs_bytes, cases[i].ps, cases[i].ps_bytes, 1);
        for (p = 0; p < 2; p++)
        {
            const MOJOSHADER_parseData *pd = cna_parse(profiles[p], cases[i].vs, cases[i].vs_bytes);
            snprintf(label, sizeof (label), "%s %s", cases[i].name, profiles[p]);
            CNA_CHECK(cna_report_errors(label, pd) == 0, "the vertex shader translates");
            if (pd->error_count == 0)
            {
                const int lane = fog_output_lane(pd);
                if (lane != cases[i].lane)
                    fprintf(stderr, "  %s: FOG stored from lane %d, declared %d\n", label, lane,
                            cases[i].lane);
                CNA_CHECK(lane == cases[i].lane, "FOG is one float output, stored from its lane");
                snprintf(label, sizeof (label), "%s_vs_%s", cases[i].name, profiles[p]);
                CNA_CHECK(cna_spirv_validate(pd, label) != 0, "spirv-val accepts it");
            } // if
            MOJOSHADER_freeParseData(pd);
        } // for
    } // for
} // test_standalone_fog_lanes

/* Counts what MOJOSHADER_parse() allocates through the caller's allocator, and frees. */
static int live_allocations = 0;

static void *MOJOSHADERCALL counting_malloc(int bytes, void *data)
{
    void *ptr = malloc((size_t) bytes);
    (void) data;
    if (ptr != NULL)
        live_allocations++;
    return ptr;
} // counting_malloc

static void MOJOSHADERCALL counting_free(void *ptr, void *data)
{
    (void) data;
    if (ptr != NULL)
        live_allocations--;
    free(ptr);
} // counting_free

static const MOJOSHADER_parseData *counted_parse(const char *profile, const uint32_t *tokens,
                                                 size_t bytes)
{
    return MOJOSHADER_parse(profile, "main", (const unsigned char *) tokens, (unsigned int) bytes,
                            NULL, 0, NULL, 0, counting_malloc, counting_free, NULL);
} // counted_parse

/* vs_3_0 that reads the undeclared input v1 after loading v0: refused after a recorded load. */
static const uint32_t undeclared_input_vs3[] = {
    CNA_VS(3, 0),
    CNA_OP(CNA_DCL_OP, 2), CNA_DCL(0, 0), CNA_DST(CNA_INPUT, 0, 15),
    CNA_OP(CNA_DCL_OP, 2), CNA_DCL(0, 0), CNA_DST(CNA_OUTPUT, 0, 15),
    CNA_OP(CNA_DCL_OP, 2), CNA_DCL(5, 0), CNA_DST(CNA_OUTPUT, 1, 15),
    CNA_OP(CNA_MOV, 2), CNA_DST(CNA_OUTPUT, 0, 15), CNA_SRC(CNA_INPUT, 0),
    CNA_OP(CNA_MOV, 2), CNA_DST(CNA_OUTPUT, 1, 15), CNA_SRC(CNA_INPUT, 1),
    CNA_END
};

/*
 * handoff "LeakSanitizer reports leaks in FNA3D's MojoShader": every vertex-input load records a
 * patch point in a list the patch table points to, and nothing freed those lists. A parse --
 * successful or not, on both SPIR-V profiles -- must return everything it allocated.
 */
static void test_parse_frees_patch_table_lists(void)
{
    static const char *profiles[2] = { "spirv", "glspirv" };
    int i;
    for (i = 0; i < 2; i++)
    {
        const MOJOSHADER_parseData *pd;

        live_allocations = 0;
        pd = counted_parse(profiles[i], plain_vs3, sizeof (plain_vs3));
        CNA_CHECK((pd != NULL) && (pd->error_count == 0), "the vertex shader translates");
        MOJOSHADER_freeParseData(pd);
        if (live_allocations != 0)
            fprintf(stderr, "  %s: %d allocation(s) outlive the parse data\n", profiles[i],
                    live_allocations);
        CNA_CHECK(live_allocations == 0, "a successful parse frees everything");

        live_allocations = 0;
        pd = counted_parse(profiles[i], undeclared_input_vs3, sizeof (undeclared_input_vs3));
        CNA_CHECK((pd != NULL) && (pd->error_count > 0), "the vertex shader is refused");
        MOJOSHADER_freeParseData(pd);
        if (live_allocations != 0)
            fprintf(stderr, "  %s (refused): %d allocation(s) outlive the parse data\n",
                    profiles[i], live_allocations);
        CNA_CHECK(live_allocations == 0, "a refused parse frees everything");
    } // for
} // test_parse_frees_patch_table_lists

int main(void)
{
    cna_test_run("parse_frees_patch_table_lists", test_parse_frees_patch_table_lists);
    cna_test_run("plain_pair", test_plain_pair);
    cna_test_run("packed_vertex_outputs", test_packed_vertex_outputs);
    cna_test_run("packed_pixel_inputs", test_packed_pixel_inputs);
    cna_test_run("packed_both", test_packed_both);
    cna_test_run("packed_fog", test_packed_fog);
    cna_test_run("rastout_semantics", test_rastout_semantics);
    cna_test_run("fog_interface_types", test_fog_interface_types);
    cna_test_run("standalone_fog_lanes", test_standalone_fog_lanes);
    return cna_test_finish();
} // main
