/**
 * Pixel Shader Model 1.x texture-coordinate inputs and samplers, across profiles.
 *
 * ps_1_4 has no DCL instruction: its t0..t5 are implicit, read-only TEXCOORD0..5 inputs and its
 * texture stages implicit samplers. Each case translates the same bytecode with the GLSL profile,
 * the reference, and with both SPIR-V profiles; links each SPIR-V pixel shader to a vertex shader
 * that writes exactly the TEXCOORDs it reads; and validates the pair.
 *
 * This file is part of the libcna fork of MojoShader; see tests/cna/README.md.
 */

#include <stdio.h>
#include <string.h>

#include "cna_test.h"
#include "spirv/spirv.h"

/* MOJOSHADER_spirv_link_attributes(), the GL glue's link step (mojoshader_opengl.c). */
#define __MOJOSHADER_INTERNAL__ 1
#include "mojoshader_internal.h"

#define PS14_PHASE 0x0000fffdu
#define PS14_TEXKILL 65

/* ps_1_1: `texcoord t0`, then r0 = t0. The mutable ps_1_1..1_3 texture register. */
static const uint32_t ps11_texcoord[] = {
    CNA_PS(1, 1),
    CNA_OP(CNA_TEXCRD, 0), CNA_DST(CNA_TEXTURE, 0, 15),
    CNA_OP(CNA_MOV, 0), CNA_DST(CNA_TEMP, 0, 15), CNA_SRC(CNA_TEXTURE, 0),
    CNA_END
};

/* ps_1_4: `texcrd r0.xyz, t0` and `mov r0.w, c0` (A2v2 A-11's own probe). */
static const uint32_t ps14_texcrd_t0[] = {
    CNA_PS(1, 4),
    CNA_OP(CNA_TEXCRD, 0), CNA_DST(CNA_TEMP, 0, 7), CNA_SRC(CNA_TEXTURE, 0),
    CNA_OP(CNA_MOV, 0), CNA_DST(CNA_TEMP, 0, 8), CNA_SRC(CNA_CONST, 0),
    CNA_END
};

/* The same from t5, beyond the four texture registers of ps_1_1..1_3. */
static const uint32_t ps14_texcrd_t5[] = {
    CNA_PS(1, 4),
    CNA_OP(CNA_TEXCRD, 0), CNA_DST(CNA_TEMP, 0, 7), CNA_SRC(CNA_TEXTURE, 5),
    CNA_OP(CNA_MOV, 0), CNA_DST(CNA_TEMP, 0, 8), CNA_SRC(CNA_CONST, 0),
    CNA_END
};

/* `texld r0, t0` and `texld r1, t3`: stages 0 and 1 at two texture coordinates. */
static const uint32_t ps14_texld_t[] = {
    CNA_PS(1, 4),
    CNA_OP(CNA_TEXLD, 0), CNA_DST(CNA_TEMP, 0, 15), CNA_SRC(CNA_TEXTURE, 0),
    CNA_OP(CNA_TEXLD, 0), CNA_DST(CNA_TEMP, 1, 15), CNA_SRC(CNA_TEXTURE, 3),
    CNA_OP(CNA_MUL, 0), CNA_DST(CNA_TEMP, 0, 15), CNA_SRC(CNA_TEMP, 0), CNA_SRC(CNA_TEMP, 1),
    CNA_END
};

/*
 * Dependent reads. Phase 1: `texcrd r1.xyz, t1`, `texld r2, t4`. Phase 2: `texld r0, r1` (a
 * coordinate the first phase computed) and `texld r2, r2` (a sampled value as a coordinate, at
 * the stage the first phase already sampled).
 */
static const uint32_t ps14_texld_phase2[] = {
    CNA_PS(1, 4),
    CNA_OP(CNA_TEXCRD, 0), CNA_DST(CNA_TEMP, 1, 7), CNA_SRC(CNA_TEXTURE, 1),
    CNA_OP(CNA_TEXLD, 0), CNA_DST(CNA_TEMP, 2, 15), CNA_SRC(CNA_TEXTURE, 4),
    PS14_PHASE,
    CNA_OP(CNA_TEXLD, 0), CNA_DST(CNA_TEMP, 0, 15), CNA_SRC(CNA_TEMP, 1),
    CNA_OP(CNA_TEXLD, 0), CNA_DST(CNA_TEMP, 2, 15), CNA_SRC(CNA_TEMP, 2),
    CNA_OP(CNA_MUL, 0), CNA_DST(CNA_TEMP, 0, 15), CNA_SRC(CNA_TEMP, 0), CNA_SRC(CNA_TEMP, 2),
    CNA_END
};

/* `texld r0, t2` at a stage the caller's sampler map makes a cube map: XYZ coordinates. */
static const uint32_t ps14_texld_cube[] = {
    CNA_PS(1, 4),
    CNA_OP(CNA_TEXLD, 0), CNA_DST(CNA_TEMP, 0, 15), CNA_SRC(CNA_TEXTURE, 2),
    CNA_END
};

/* `texkill t1`: the other instruction that reads a ps_1_4 texture coordinate. */
static const uint32_t ps14_texkill[] = {
    CNA_PS(1, 4),
    CNA_OP(PS14_TEXKILL, 0), CNA_DST(CNA_TEXTURE, 1, 15),
    CNA_OP(CNA_MOV, 0), CNA_DST(CNA_TEMP, 0, 15), CNA_SRC(CNA_CONST, 0),
    CNA_END
};

typedef struct Ps14Case
{
    const char *name;
    const uint32_t *tokens;
    size_t bytes;
    unsigned texcoords;          /* bit n: the shader reads TEXCOORDn */
    int cube_stage;              /* a stage the sampler map makes a cube map, or -1 */
    int samples;                 /* OpImageSampleImplicitLod instructions expected */
    int divisions;               /* OpFDiv instructions expected: one per _dz/_dw */
    const char *glsl[3];         /* lines the GLSL profile produces for the same bytecode */
} Ps14Case;

static const Ps14Case cases[] = {
    { "ps11_texcoord", ps11_texcoord, sizeof (ps11_texcoord), 0x01, -1, 0, 0,
      { "ps_t0 = gl_TexCoord[0];", "ps_r0 = ps_t0;", NULL } },
    { "ps14_texcrd_t0", ps14_texcrd_t0, sizeof (ps14_texcrd_t0), 0x01, -1, 0, 0,
      { "#define ps_t0 gl_TexCoord[0]", "ps_r0.xyz = ps_t0.xyz;", "ps_r0.w = ps_c0.w;" } },
    { "ps14_texcrd_t5", ps14_texcrd_t5, sizeof (ps14_texcrd_t5), 0x20, -1, 0, 0,
      { "#define ps_t5 io_5_5", "ps_r0.xyz = ps_t5.xyz;", NULL } },
    { "ps14_texld_t", ps14_texld_t, sizeof (ps14_texld_t), 0x09, -1, 2, 0,
      { "ps_r0 = texture2D(ps_s0, ps_t0.xy);", "ps_r1 = texture2D(ps_s1, ps_t3.xy);", NULL } },
    { "ps14_texld_phase2", ps14_texld_phase2, sizeof (ps14_texld_phase2), 0x12, -1, 3, 0,
      { "ps_r2 = texture2D(ps_s2, ps_t4.xy);", "ps_r0 = texture2D(ps_s0, ps_r1.xy);",
        "ps_r2 = texture2D(ps_s2, ps_r2.xy);" } },
    { "ps14_texld_cube", ps14_texld_cube, sizeof (ps14_texld_cube), 0x04, 0, 1, 0,
      { "ps_r0 = textureCube(ps_s0, ps_t2.xyz);", NULL, NULL } },
    { "ps14_texkill", ps14_texkill, sizeof (ps14_texkill), 0x02, -1, 0, 0,
      { "if (any(lessThan(ps_t1.xyz, vec3(0.0)))) discard;", NULL, NULL } },
};

static const MOJOSHADER_parseData *parse_case(const char *profile, const Ps14Case *c)
{
    MOJOSHADER_samplerMap map;
    map.index = c->cube_stage;
    map.type = MOJOSHADER_SAMPLER_CUBE;
    return MOJOSHADER_parse(profile, "main", (const unsigned char *) c->tokens,
                            (unsigned int) c->bytes, NULL, 0,
                            (c->cube_stage >= 0) ? &map : NULL, (c->cube_stage >= 0) ? 1 : 0,
                            NULL, NULL, NULL);
} // parse_case

/* vs_2_0 writing oPos, and oT<n> for every bit n of `texcoords`, from v0. */
static size_t build_vertex_shader(uint32_t *out, unsigned texcoords)
{
    size_t n = 0;
    int i;
    out[n++] = CNA_VS(2, 0);
    out[n++] = CNA_OP(CNA_DCL_OP, 2);
    out[n++] = CNA_DCL(0, 0);
    out[n++] = CNA_DST(CNA_INPUT, 0, 15);
    out[n++] = CNA_OP(CNA_MOV, 2);
    out[n++] = CNA_DST(CNA_RASTOUT, 0, 15);
    out[n++] = CNA_SRC(CNA_INPUT, 0);
    for (i = 0; i < 8; i++)
    {
        if ((texcoords & (1u << i)) == 0)
            continue;
        out[n++] = CNA_OP(CNA_MOV, 2);
        out[n++] = CNA_DST(CNA_TEXCRDOUT, i, 15);
        out[n++] = CNA_SRC(CNA_INPUT, 0);
    } // for
    out[n++] = CNA_END;
    return n * sizeof (uint32_t);
} // build_vertex_shader

#define MAX_IDS 4096

/* What the checks need to know about one SPIR-V module, read from its words. */
typedef struct Module
{
    int storage[MAX_IDS];       /* -1: not a variable */
    int location[MAX_IDS];      /* -1: no Location decoration */
    int builtin[MAX_IDS];       /* -1: no BuiltIn decoration */
    int loads[MAX_IDS];         /* OpLoad instructions through this pointer */
    char name[MAX_IDS][32];     /* OpName, if any */
    int interface_ids[64];
    int interface_count;
    int duplicate_variables;
    int samples;                /* OpImageSampleImplicitLod */
    int divisions;              /* OpFDiv */
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
        m->builtin[i] = -1;
    } // for

    while (at < count)
    {
        const uint32_t length = words[at] >> 16;
        const uint32_t opcode = words[at] & 0xFFFF;
        if ((length == 0) || (at + length > count))
            break;
        if ((opcode == SpvOpVariable) && (length >= 4) && (words[at + 2] < MAX_IDS))
        {
            if (m->storage[words[at + 2]] != -1)
                m->duplicate_variables++;
            m->storage[words[at + 2]] = (int) words[at + 3];
        } // if
        else if ((opcode == SpvOpDecorate) && (length >= 4) && (words[at + 1] < MAX_IDS))
        {
            if (words[at + 2] == SpvDecorationLocation)
                m->location[words[at + 1]] = (int) words[at + 3];
            else if (words[at + 2] == SpvDecorationBuiltIn)
                m->builtin[words[at + 1]] = (int) words[at + 3];
        } // else if
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
        else if ((opcode == SpvOpLoad) && (length >= 4) && (words[at + 3] < MAX_IDS))
            m->loads[words[at + 3]]++;
        else if (opcode == SpvOpImageSampleImplicitLod)
            m->samples++;
        else if (opcode == SpvOpFDiv)
            m->divisions++;
        else if ((opcode == SpvOpEntryPoint) && (length >= 4))
        {
            /* model, function id, literal name (NUL-terminated, padded), interface ids */
            size_t name = at + 3;
            while ((name < at + length) && ((words[name] >> 24) != 0))
                name++;
            for (name++; (name < at + length) && (m->interface_count < 64); name++)
                m->interface_ids[m->interface_count++] = (int) words[name];
        } // else if
        at += length;
    } // while
} // read_module

static int in_interface(const Module *m, int id)
{
    int i;
    for (i = 0; i < m->interface_count; i++)
        if (m->interface_ids[i] == id)
            return 1;
    return 0;
} // in_interface

/* The variable of one storage class with one of the names, or -1. */
static int find_variable(const Module *m, int storage, const char *name, const char *other)
{
    int id;
    for (id = 0; id < MAX_IDS; id++)
    {
        if (m->storage[id] != storage)
            continue;
        if ((strcmp(m->name[id], name) == 0) || ((other != NULL) && (strcmp(m->name[id], other) == 0)))
            return id;
    } // for
    return -1;
} // find_variable

/* Number of interface variables of one storage class that are not built-ins. */
static int user_variables(const Module *m, int storage)
{
    int i, n = 0;
    for (i = 0; i < m->interface_count; i++)
    {
        const int id = m->interface_ids[i];
        if ((id >= 0) && (id < MAX_IDS) && (m->storage[id] == storage) && (m->builtin[id] == -1))
            n++;
    } // for
    return n;
} // user_variables

static Module vertex_module, pixel_module;

/* Translates, links and checks one case with one SPIR-V profile. */
static void check_spirv(const Ps14Case *c, const char *profile)
{
    uint32_t vs[64];
    const size_t vs_bytes = build_vertex_shader(vs, c->texcoords);
    const MOJOSHADER_parseData *v = cna_parse(profile, vs, vs_bytes);
    const MOJOSHADER_parseData *p = parse_case(profile, c);
    const int is_gl = (strcmp(profile, "glspirv") == 0);
    Module *vm = &vertex_module, *pm = &pixel_module;
    char label[160];
    int locations[8];
    int n, i, j, expected = 0;

    snprintf(label, sizeof (label), "%s/%s: vertex shader translates", c->name, profile);
    CNA_CHECK(cna_report_errors(label, v) == 0, label);
    snprintf(label, sizeof (label), "%s/%s: pixel shader translates", c->name, profile);
    CNA_CHECK(cna_report_errors(label, p) == 0, label);
    if ((v == NULL) || (p == NULL) || v->error_count || p->error_count)
        goto done;

    if (is_gl)
        MOJOSHADER_spirv_link_attributes(v, p, 1);
    else
    {
        MOJOSHADER_vertexAttribute position;
        memset(&position, '\0', sizeof (position));
        position.usage = MOJOSHADER_USAGE_POSITION;
        position.vertexElementFormat = MOJOSHADER_VERTEXELEMENTFORMAT_VECTOR4;
        MOJOSHADER_linkSPIRVShaders(v, p, &position, 1);
    } // else

    read_module(v, vm);
    read_module(p, pm);

    snprintf(label, sizeof (label), "%s/%s: every variable is defined once", c->name, profile);
    CNA_CHECK((vm->duplicate_variables == 0) && (pm->duplicate_variables == 0), label);
    for (i = 0; i < pm->interface_count; i++)
    {
        const int id = pm->interface_ids[i];
        const int ok = (id >= 0) && (id < MAX_IDS) &&
                       ((pm->storage[id] == SpvStorageClassInput) ||
                        (pm->storage[id] == SpvStorageClassOutput));
        snprintf(label, sizeof (label), "%s/%s: pixel interface id %d is Input/Output",
                 c->name, profile, id);
        CNA_CHECK(ok, label);
    } // for

    /* Each TEXCOORDn the pixel shader reads is an Input variable in its interface that the
     * shader loads, at the Location of the vertex shader's oTn. */
    n = 0;
    for (i = 0; i < 8; i++)
    {
        char ps_name[16], vs_name[16];
        int in, out;
        if ((c->texcoords & (1u << i)) == 0)
            continue;
        expected++;
        snprintf(ps_name, sizeof (ps_name), "ps_t%d", i);
        snprintf(vs_name, sizeof (vs_name), "vs_oT%d", i);
        in = find_variable(pm, SpvStorageClassInput, ps_name,
                           (i == 0) ? "ps_PointCoordOrTexCoord0" : NULL);
        out = find_variable(vm, SpvStorageClassOutput, vs_name, NULL);
        snprintf(label, sizeof (label), "%s/%s: TEXCOORD%d is a pixel Input in the interface",
                 c->name, profile, i);
        CNA_CHECK((in >= 0) && in_interface(pm, in), label);
        snprintf(label, sizeof (label), "%s/%s: oT%d is a vertex Output in the interface",
                 c->name, profile, i);
        CNA_CHECK((out >= 0) && in_interface(vm, out), label);
        if ((in < 0) || (out < 0))
            continue;
        snprintf(label, sizeof (label), "%s/%s: TEXCOORD%d's input is read", c->name, profile, i);
        CNA_CHECK(pm->loads[in] > 0, label);
        snprintf(label, sizeof (label), "%s/%s: t%d and oT%d share a Location",
                 c->name, profile, i, i);
        CNA_CHECK((pm->location[in] >= 0) && (pm->location[in] == vm->location[out]), label);
        locations[n++] = pm->location[in];
    } // for
    for (i = 0; i < n; i++)
        for (j = i + 1; j < n; j++)
        {
            snprintf(label, sizeof (label), "%s/%s: TEXCOORD Locations are distinct",
                     c->name, profile);
            CNA_CHECK(locations[i] != locations[j], label);
        } // for
    snprintf(label, sizeof (label), "%s/%s: no other pixel inputs", c->name, profile);
    CNA_CHECK(user_variables(pm, SpvStorageClassInput) == expected, label);

    snprintf(label, sizeof (label), "%s/%s: %d texture sample(s)", c->name, profile, c->samples);
    CNA_CHECK(pm->samples == c->samples, label);
    snprintf(label, sizeof (label), "%s/%s: %d projective division(s)", c->name, profile,
             c->divisions);
    CNA_CHECK(pm->divisions == c->divisions, label);

    snprintf(label, sizeof (label), "%s_%s_vs", c->name, profile);
    CNA_CHECK(cna_spirv_validate(v, label) != 0, "spirv-val accepts the vertex shader");
    snprintf(label, sizeof (label), "%s_%s_ps", c->name, profile);
    CNA_CHECK(cna_spirv_validate(p, label) != 0, "spirv-val accepts the pixel shader");

done:
    MOJOSHADER_freeParseData(v);
    MOJOSHADER_freeParseData(p);
} // check_spirv

/* The GLSL profile's translation of the same bytecode, which the SPIR-V one follows. */
static void check_glsl(const Ps14Case *c)
{
    const MOJOSHADER_parseData *pd = parse_case("glsl", c);
    char label[200];
    int i, missing = 0;

    snprintf(label, sizeof (label), "%s/glsl: translates", c->name);
    CNA_CHECK(cna_report_errors(label, pd) == 0, label);
    for (i = 0; i < 3; i++)
    {
        if (c->glsl[i] == NULL)
            continue;
        snprintf(label, sizeof (label), "%s/glsl: produces `%s`", c->name, c->glsl[i]);
        if (!cna_output_contains(pd, c->glsl[i]))
            missing = 1;
        CNA_CHECK(cna_output_contains(pd, c->glsl[i]), label);
    } // for
    if (missing && (pd != NULL) && (pd->output != NULL))
        fprintf(stderr, "--- %s/glsl output ---\n%s\n---\n", c->name, pd->output);
    MOJOSHADER_freeParseData(pd);
} // check_glsl

/* vs_3_0 writing a position and a point size (dcl_psize o1) -- a point sprite -- and no
 * TEXCOORD0. (A vs_1_x/vs_2_x oPts does not reach the PointSize built-in on this profile.) */
static const uint32_t point_size_vs3[] = {
    CNA_VS(3, 0),
    CNA_OP(CNA_DCL_OP, 2), CNA_DCL(0, 0), CNA_DST(CNA_INPUT, 0, 15),
    CNA_OP(CNA_DCL_OP, 2), CNA_DCL(0, 0), CNA_DST(CNA_OUTPUT, 0, 15),
    CNA_OP(CNA_DCL_OP, 2), CNA_DCL(MOJOSHADER_USAGE_POINTSIZE, 0), CNA_DST(CNA_OUTPUT, 1, 15),
    CNA_OP(CNA_MOV, 2), CNA_DST(CNA_OUTPUT, 0, 15), CNA_SRC(CNA_INPUT, 0),
    CNA_OP(CNA_MOV, 2), CNA_DST(CNA_OUTPUT, 1, 1), CNA_SRC(CNA_INPUT, 0),
    CNA_END
};

/*
 * ps_1_4's t0 is the same dual-purpose TEXCOORD0-or-gl_PointCoord input as a ps_2_0 DCL'd t0:
 * linked to a vertex shader that writes a point size, it becomes the PointCoord built-in.
 */
static void test_ps14_t0_point_coord(void)
{
    const MOJOSHADER_parseData *v = cna_parse("spirv", point_size_vs3, sizeof (point_size_vs3));
    const MOJOSHADER_parseData *p = cna_parse("spirv", ps14_texcrd_t0, sizeof (ps14_texcrd_t0));
    MOJOSHADER_vertexAttribute position;
    Module *pm = &pixel_module;
    int in;

    CNA_CHECK(cna_report_errors("point-size vertex shader", v) == 0, "the vertex shader translates");
    CNA_CHECK(cna_report_errors("ps_1_4 t0", p) == 0, "the pixel shader translates");
    if ((v == NULL) || (p == NULL) || v->error_count || p->error_count)
        goto done;

    memset(&position, '\0', sizeof (position));
    position.usage = MOJOSHADER_USAGE_POSITION;
    position.vertexElementFormat = MOJOSHADER_VERTEXELEMENTFORMAT_VECTOR4;
    MOJOSHADER_linkSPIRVShaders(v, p, &position, 1);

    read_module(p, pm);
    in = find_variable(pm, SpvStorageClassInput, "ps_PointCoordOrTexCoord0", NULL);
    CNA_CHECK((in >= 0) && in_interface(pm, in), "t0 is a pixel Input in the interface");
    CNA_CHECK((in >= 0) && (pm->builtin[in] == SpvBuiltInPointCoord), "t0 became gl_PointCoord");
    CNA_CHECK((in >= 0) && (pm->loads[in] > 0), "t0's input is read");
    CNA_CHECK(cna_spirv_validate(v, "ps14_t0_point_coord_vs") != 0,
              "spirv-val accepts the vertex shader");
    CNA_CHECK(cna_spirv_validate(p, "ps14_t0_point_coord_ps") != 0,
              "spirv-val accepts the pixel shader");

done:
    MOJOSHADER_freeParseData(v);
    MOJOSHADER_freeParseData(p);
} // test_ps14_t0_point_coord

static const Ps14Case *current;

static void run_current(void)
{
    check_glsl(current);
    check_spirv(current, "spirv");
    check_spirv(current, "glspirv");
} // run_current

int main(void)
{
    size_t i;
    for (i = 0; i < sizeof (cases) / sizeof (cases[0]); i++)
    {
        current = &cases[i];
        cna_test_run(cases[i].name, run_current);
    } // for
    cna_test_run("ps14_t0_point_coord", test_ps14_t0_point_coord);
    return cna_test_finish();
} // main
