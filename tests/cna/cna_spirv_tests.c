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

#define MAX_IDS 4096

/* What the tests need to know about a module, read from its words. */
typedef struct SpirvFacts
{
    int variables;
    int duplicate_variables;
    int storage[MAX_IDS];       /* -1: not a variable */
    int location[MAX_IDS];      /* -1: no Location decoration */
    int builtin[MAX_IDS];       /* -1: no BuiltIn decoration */
    int interface_ids[64];
    int interface_count;
} SpirvFacts;

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
            facts->variables++;
        } // if
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

/* Locations of the user (non-builtin) interface variables of one storage class, sorted. */
static int user_locations(const SpirvFacts *facts, int storage, int *out, int max)
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
            out[n++] = facts->location[id];
    } // for
    for (i = 1; i < n; i++)
        for (j = i; (j > 0) && (out[j - 1] > out[j]); j--)
        {
            const int t = out[j];
            out[j] = out[j - 1];
            out[j - 1] = t;
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

/* Translates, links and checks one vertex/pixel pair. */
static void check_pair(const char *name, const uint32_t *vs, size_t vs_bytes,
                       const uint32_t *ps, size_t ps_bytes, int expected_user_varyings)
{
    MOJOSHADER_vertexAttribute position;
    const MOJOSHADER_parseData *v = cna_parse("spirv", vs, vs_bytes);
    const MOJOSHADER_parseData *p = cna_parse("spirv", ps, ps_bytes);
    char label[128];
    SpirvFacts vf, pf;
    int vlocs[16], plocs[16];
    int vn, pn, i;

    snprintf(label, sizeof (label), "%s: vertex shader translates", name);
    CNA_CHECK(cna_report_errors(label, v) == 0, label);
    snprintf(label, sizeof (label), "%s: pixel shader translates", name);
    CNA_CHECK(cna_report_errors(label, p) == 0, label);
    if ((v == NULL) || (p == NULL) || v->error_count || p->error_count)
        goto done;

    memset(&position, '\0', sizeof (position));
    position.usage = MOJOSHADER_USAGE_POSITION;
    position.vertexElementFormat = MOJOSHADER_VERTEXELEMENTFORMAT_VECTOR4;
    MOJOSHADER_linkSPIRVShaders(v, p, &position, 1);

    read_facts(v, &vf);
    read_facts(p, &pf);
    snprintf(label, sizeof (label), "%s: vertex interface", name);
    check_interface(&vf, label);
    snprintf(label, sizeof (label), "%s: pixel interface", name);
    check_interface(&pf, label);

    vn = user_locations(&vf, SpvStorageClassOutput, vlocs, 16);
    pn = user_locations(&pf, SpvStorageClassInput, plocs, 16);
    snprintf(label, sizeof (label), "%s: one vertex output per varying semantic", name);
    CNA_CHECK(vn == expected_user_varyings, label);
    snprintf(label, sizeof (label), "%s: one pixel input per varying semantic", name);
    CNA_CHECK(pn == expected_user_varyings, label);
    if ((vn == pn) && (vn == expected_user_varyings))
        for (i = 0; i < vn; i++)
        {
            snprintf(label, sizeof (label), "%s: varying %d links to the same Location", name, i);
            CNA_CHECK((vlocs[i] == plocs[i]) && (vlocs[i] >= 0), label);
            if (i > 0)
            {
                snprintf(label, sizeof (label), "%s: varying Locations are distinct", name);
                CNA_CHECK(vlocs[i] != vlocs[i - 1], label);
            } // if
        } // for

    snprintf(label, sizeof (label), "%s_vs", name);
    CNA_CHECK(cna_spirv_validate(v, label) != 0, "spirv-val accepts the vertex shader");
    snprintf(label, sizeof (label), "%s_ps", name);
    CNA_CHECK(cna_spirv_validate(p, label) != 0, "spirv-val accepts the pixel shader");

done:
    MOJOSHADER_freeParseData(v);
    MOJOSHADER_freeParseData(p);
} // check_pair

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

int main(void)
{
    cna_test_run("plain_pair", test_plain_pair);
    cna_test_run("packed_vertex_outputs", test_packed_vertex_outputs);
    cna_test_run("packed_pixel_inputs", test_packed_pixel_inputs);
    cna_test_run("packed_both", test_packed_both);
    return cna_test_finish();
} // main
