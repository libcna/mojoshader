/* SPDX-License-Identifier: Zlib */
/** Literal def-use contracts for BEM and TEXDEPTH, for both SPIR-V profiles.
 * This file is part of the libcna fork of MojoShader; see tests/cna/README.md. */
#include <string.h>
#include <stdio.h>
#include <math.h>
#include "cna_test.h"
#include "spirv/spirv.h"
#include "spirv/GLSL.std.450.h"
#define __MOJOSHADER_INTERNAL__ 1
#include "mojoshader_internal.h"

/* Do not compare whole compiler output or unstable IDs. Inspect the arithmetic's operands. */
static const uint32_t *definition(const MOJOSHADER_parseData *p, uint32_t id)
{
    const uint32_t *w = (const uint32_t *) p->output;
    size_t at, n = cna_spirv_module_words(p);
    for (at = 5; at < n && (w[at] >> 16); at += w[at] >> 16)
    {
        unsigned int op = w[at] & 0xffff;
        if ((op == SpvOpLoad || op == SpvOpVectorShuffle || op == SpvOpCompositeExtract ||
             op == SpvOpFMul || op == SpvOpFAdd || op == SpvOpFDiv || op == SpvOpFOrdEqual ||
             op == SpvOpSelect || op == SpvOpConstant) && w[at + 2] == id) return w + at;
    }
    return NULL;
}
static int shuffle(const MOJOSHADER_parseData *p, uint32_t id, int a, int b, uint32_t *source)
{
    const uint32_t *d = definition(p, id);
    if (!d || (d[0] & 0xffff) != SpvOpVectorShuffle || (d[0] >> 16) != 7 ||
        d[3] != d[4] || d[5] != (uint32_t) a || d[6] != (uint32_t) b) return 0;
    *source = d[3]; return 1;
}
static int matrix_formula(const MOJOSHADER_parseData *p)
{
    const uint32_t *w = (const uint32_t *) p->output;
    size_t at, n = cna_spirv_module_words(p);
    for (at = 5; at < n && (w[at] >> 16); at += w[at] >> 16)
    {
        const uint32_t *a, *b;
        uint32_t matrix0, matrix1, bump0, bump1;
        if ((w[at] & 0xffff) != SpvOpFAdd) continue;
        a = definition(p, w[at + 3]); b = definition(p, w[at + 4]);
        if (!a || !b || (a[0] & 0xffff) != SpvOpFMul || (b[0] & 0xffff) != SpvOpFMul) continue;
        /* (M00,M01)*bump.xx + (M10,M11)*bump.yy, with shared matrix and bump values. */
        if (shuffle(p, a[3], 0, 1, &matrix0) && shuffle(p, b[3], 2, 3, &matrix1) &&
            shuffle(p, a[4], 0, 0, &bump0) && shuffle(p, b[4], 1, 1, &bump1) &&
            matrix0 == matrix1 && bump0 == bump1) return 1;
    }
    return 0;
}
static void depth_formula(const MOJOSHADER_parseData *p)
{
    const uint32_t *w = (const uint32_t *) p->output;
    uint32_t out = 0;
    size_t at, n = cna_spirv_module_words(p);
    int mode = 0, stored = 0, in_interface = 0;
    for (at = 5; at < n && (w[at] >> 16); at += w[at] >> 16)
    {
        unsigned int op = w[at] & 0xffff;
        if (op == SpvOpDecorate && w[at + 2] == SpvDecorationBuiltIn &&
            w[at + 3] == SpvBuiltInFragDepth) out = w[at + 1];
        if (op == SpvOpExecutionMode && w[at + 2] == SpvExecutionModeDepthReplacing) mode++;
    }
    for (at = 5; at < n && (w[at] >> 16); at += w[at] >> 16)
    {
        unsigned int op = w[at] & 0xffff;
        if (op == SpvOpEntryPoint)
        {
            size_t j = at + 3;
            const unsigned char *name = (const unsigned char *) (w + j);
            j += (strlen((const char *) name) + 4) / 4;
            for (; j < at + (w[at] >> 16); j++) if (w[j] == out) in_interface = 1;
        }
        if (op == SpvOpStore && w[at + 1] == out && out)
        {
            const uint32_t *select = definition(p, w[at + 2]);
            const uint32_t *eq, *one, *div, *x, *y, *zero;
            stored++;
            CNA_CHECK(select && (select[0] & 0xffff) == SpvOpSelect, "depth store uses guarded ratio");
            if (!select || (select[0] & 0xffff) != SpvOpSelect) continue;
            eq = definition(p, select[3]); one = definition(p, select[4]); div = definition(p, select[5]);
            CNA_CHECK(eq && (eq[0] & 0xffff) == SpvOpFOrdEqual, "zero divisor comparison");
            CNA_CHECK(one && (one[0] & 0xffff) == SpvOpConstant && one[3] == 0x3f800000,
                      "zero divisor produces literal depth 1.0");
            CNA_CHECK(div && (div[0] & 0xffff) == SpvOpFDiv, "nonzero divisor uses division");
            if (!eq || !div) continue;
            zero = definition(p, eq[4]); x = definition(p, div[3]); y = definition(p, div[4]);
            CNA_CHECK(zero && (zero[0] & 0xffff) == SpvOpConstant && zero[3] == 0, "compare to literal +0");
            CNA_CHECK(eq[3] == div[4], "the tested divisor is the divided divisor");
            CNA_CHECK(x && y && (x[0] & 0xffff) == SpvOpCompositeExtract &&
                      (y[0] & 0xffff) == SpvOpCompositeExtract && x[3] == y[3] &&
                      x[4] == 0 && y[4] == 1, "depth ratio uses r5.x / r5.y, not z or w");
        }
    }
    CNA_CHECK(out != 0 && mode == 1 && in_interface && stored == 1,
              "one FragDepth output, interface entry, DepthReplacing mode and store");
}
/* Interpret only the straight-line arithmetic subset our literal fixtures use. This is a
 * test oracle over the emitted program, independent of the emitter's chosen algebra/IDs.
 * Undefined register lanes stay unknown until written; no uninitialized value is an oracle. */
#define IR_IDS 4096
typedef struct IrValue { float lane[4]; unsigned int known; } IrValue;
static void numeric_result(const MOJOSHADER_parseData *p, int depth, int variant)
{
    static IrValue value[IR_IDS], memory[IR_IDS];
    static int width[IR_IDS], kind[IR_IDS], storage[IR_IDS], root[IR_IDS], row[IR_IDS];
    static int location[IR_IDS], value_width[IR_IDS];
    const uint32_t *w = (const uint32_t *) p->output;
    const size_t n = cna_spirv_module_words(p);
    static const float matrix[4] = {.4f, .8f, .2f, .6f};
    uint32_t color = 0, depthout = 0;
    size_t at;
    int i;
    memset(value, 0, sizeof (value)); memset(memory, 0, sizeof (memory));
    memset(width, 0, sizeof (width)); memset(kind, 0, sizeof (kind));
    memset(root, 0, sizeof (root)); memset(row, 0, sizeof (row));
    for (i = 0; i < IR_IDS; i++) { storage[i] = location[i] = -1; value_width[i] = 0; }
    CNA_CHECK(w[3] < IR_IDS, "numeric oracle ID bound");
    if (w[3] >= IR_IDS) return;
    for (at = 5; at < n && (w[at] >> 16); at += w[at] >> 16)
    {
        const uint32_t *d = w + at;
        unsigned int op = d[0] & 0xffff, len = d[0] >> 16;
        if (op == SpvOpDecorate && d[2] == SpvDecorationBuiltIn && d[3] == SpvBuiltInFragDepth)
            depthout = d[1];
        if (op == SpvOpDecorate && d[2] == SpvDecorationLocation) location[d[1]] = d[3];
        if (op == SpvOpTypeFloat) { width[d[1]] = 1; kind[d[1]] = 1; }
        if (op == SpvOpTypeInt) { width[d[1]] = 1; kind[d[1]] = 2; }
        if (op == SpvOpTypeBool) { width[d[1]] = 1; kind[d[1]] = 3; }
        if (op == SpvOpTypeVector) { width[d[1]] = d[3]; kind[d[1]] = kind[d[2]]; }
        if (op == SpvOpVariable) { storage[d[2]] = d[3]; root[d[2]] = d[2]; }
        if (op == SpvOpConstant || op == SpvOpConstantComposite || op == SpvOpCompositeConstruct ||
            op == SpvOpLoad || op == SpvOpVectorShuffle || op == SpvOpCompositeExtract ||
            op == SpvOpFAdd || op == SpvOpFSub || op == SpvOpFNegate || op == SpvOpExtInst ||
            op == SpvOpFMul || op == SpvOpFDiv || op == SpvOpFOrdEqual || op == SpvOpSelect)
            value_width[d[2]] = width[d[1]];
        if (op == SpvOpConstant)
        {
            value[d[2]].known = 1;
            if (kind[d[1]] == 1) memcpy(&value[d[2]].lane[0], d + 3, sizeof (float));
            else value[d[2]].lane[0] = (float) (int32_t) d[3];
        }
        if (op == SpvOpConstantComposite || op == SpvOpCompositeConstruct)
        {
            for (i = 0; i < width[d[1]]; i++)
            {
                value[d[2]].lane[i] = value[d[3 + i]].lane[0];
                if (value[d[3 + i]].known & 1) value[d[2]].known |= 1u << i;
            }
        }
        if (op == SpvOpAccessChain)
        {
            root[d[2]] = root[d[3]];
            row[d[2]] = (int) value[d[len - 1]].lane[0];
        }
        if (op == SpvOpLoad)
        {
            int source = root[d[3]];
            if (source && (storage[source] == SpvStorageClassUniform ||
                           storage[source] == SpvStorageClassUniformConstant))
            {
                CNA_CHECK(!depth && row[d[3]] == 0, "only the first matrix float4 is read");
                memcpy(value[d[2]].lane, matrix, sizeof (matrix)); value[d[2]].known = 15;
            }
            else value[d[2]] = memory[d[3]];
        }
        if (op == SpvOpStore) memory[d[1]] = value[d[2]];
        if (op == SpvOpVectorShuffle)
        {
            int first_width = value_width[d[3]];
            CNA_CHECK(first_width > 0 && first_width <= 4, "known vector operand width");
            for (i = 0; i < width[d[1]]; i++)
            {
                unsigned int component = d[5 + i];
                IrValue *source = &value[component < (unsigned int) first_width ? d[3] : d[4]];
                if (component >= (unsigned int) first_width) component -= first_width;
                CNA_CHECK(component < 4, "numeric shuffle component is in range");
                if (component >= 4) continue;
                value[d[2]].lane[i] = source->lane[component];
                if (source->known & (1u << component)) value[d[2]].known |= 1u << i;
            }
        }
        if (op == SpvOpCompositeExtract)
        {
            value[d[2]].lane[0] = value[d[3]].lane[d[4]];
            value[d[2]].known = (value[d[3]].known >> d[4]) & 1;
        }
        if (op == SpvOpFAdd || op == SpvOpFSub || op == SpvOpFMul || op == SpvOpFDiv || op == SpvOpFOrdEqual)
        {
            value[d[2]].known = value[d[3]].known & value[d[4]].known;
            for (i = 0; i < width[d[1]]; i++)
            {
                float a = value[d[3]].lane[i], b = value[d[4]].lane[i];
                value[d[2]].lane[i] = op == SpvOpFAdd ? a + b : op == SpvOpFSub ? a - b : op == SpvOpFMul ? a * b :
                    op == SpvOpFOrdEqual ? a == b : b != 0 ? a / b : NAN;
            }
        }
        if (op == SpvOpFNegate)
        {
            value[d[2]] = value[d[3]];
            for (i = 0; i < width[d[1]]; i++) value[d[2]].lane[i] = -value[d[3]].lane[i];
        }
        if (op == SpvOpExtInst)
        {
            CNA_CHECK(d[4] == GLSLstd450FClamp, "only clamp is used by numeric fixtures");
            if (d[4] != GLSLstd450FClamp) return;
            value[d[2]].known = value[d[5]].known & value[d[6]].known & value[d[7]].known;
            for (i = 0; i < width[d[1]]; i++)
            {
                float x = value[d[5]].lane[i], lo = value[d[6]].lane[i], hi = value[d[7]].lane[i];
                value[d[2]].lane[i] = x < lo ? lo : x > hi ? hi : x;
            }
        }
        if (op == SpvOpSelect) value[d[2]] = value[value[d[3]].lane[0] ? d[4] : d[5]];
    }
    for (i = 1; i < IR_IDS; i++)
        if (storage[i] == SpvStorageClassOutput && location[i] == 0) color = i;
    if (!depth)
    {
        /* D3D9: (.1,.2)+(.4,.8)*.5+(.2,.6)*.25=(.35,.75); preserve .ba=(.3,.4). */
        static const float expected[5][4] = {
            {.35f, .75f, .3f, .4f},
            {-.2f, -.5f, .3f, .4f}, /* -base + M*(2*bump-1) */
            {1, 1, .3f, .4f},       /* clamp((.9,.8)+(.25,.55)) */
            {.7f, 1.5f, .3f, .4f},  /* result _x2 */
            {.4f, .8f, .3f, .4f},   /* base.xx + M*bump.xx */
        };
        CNA_CHECK(color && memory[color].known == 15, "all output color lanes are defined");
        for (i = 0; i < 4; i++)
            CNA_CHECK(fabsf(memory[color].lane[i] - expected[variant][i]) < .000001f, "literal BEM color value");
    }
    else
    {
        float expected = depth == 2 ? 1.0f : .25f;
        CNA_CHECK(depthout && memory[depthout].known == 1, "depth output is defined");
        CNA_CHECK(memory[depthout].lane[0] == expected, "literal TEXDEPTH .25/1 or zero-divisor depth one");
    }
}

static const uint32_t vs[] = {
    CNA_VS(2, 0), CNA_OP(CNA_DCL_OP, 2), CNA_DCL(0, 0), CNA_DST(CNA_INPUT, 0, 15),
    CNA_OP(CNA_MOV, 2), CNA_DST(CNA_RASTOUT, 0, 15), CNA_SRC(CNA_INPUT, 0), CNA_END
};
static void check(const char *profile, int depth, int stage, int zero_divisor, int variant)
{
    CnaWords tokens = {0};
    const MOJOSHADER_parseData *p, *v;
    MOJOSHADER_vertexAttribute pos;
    uint32_t words[] = {
        CNA_PS(1, 4), CNA_DEF, CNA_DST(CNA_CONST, 0, 15),
        0x3dcccccd, 0x3e4ccccd, 0x3e99999a, 0x3ecccccd,
        CNA_DEF, CNA_DST(CNA_CONST, 1, 15), 0x3f000000, 0x3e800000, 0, 0,
        CNA_MOV, CNA_DST(CNA_TEMP, 2, 15), CNA_SRC(CNA_CONST, 1)
    };
    size_t i;
    if (variant == 2) { words[3] = 0x3f666666; words[4] = 0x3f4ccccd; } /* .9, .8 */
    if (depth) { words[9] = 0x3e800000; words[10] = 0x3f800000; } /* .25 / 1 */
    for (i = 0; i < sizeof (words) / 4; i++) cna_words_push(&tokens, words[i]);
    if (depth)
    {
        cna_words_push(&tokens, CNA_MOV); cna_words_push(&tokens, CNA_DST(CNA_TEMP, 5, 3));
        cna_words_push(&tokens, CNA_SRC_SWZ(CNA_CONST, 1, zero_divisor ? 0xaa : 0xe4));
        cna_words_push(&tokens, 0xfffd); /* phase */
        cna_words_push(&tokens, 87); cna_words_push(&tokens, CNA_DST(CNA_TEMP, 5, 15));
        cna_words_push(&tokens, CNA_MOV); cna_words_push(&tokens, CNA_DST(CNA_TEMP, 0, 15));
        cna_words_push(&tokens, CNA_SRC(CNA_CONST, 0));
    }
    else
    {
        /* Initialize .ba BEFORE BEM so its masked assignment must preserve those lanes. */
        cna_words_push(&tokens, CNA_MOV); cna_words_push(&tokens, CNA_DST(CNA_TEMP, stage, 12));
        cna_words_push(&tokens, CNA_SRC(CNA_CONST, 0));
        cna_words_push(&tokens, 89);
        cna_words_push(&tokens, CNA_DST(CNA_TEMP, stage, 3) |
                       (variant == 2 ? 1u << 20 : variant == 3 ? 1u << 24 : 0));
        cna_words_push(&tokens, CNA_SRC_SWZ(CNA_CONST, 0, variant == 4 ? 0 : 0xe4) |
                       (variant == 1 ? 1u << 24 : 0));
        cna_words_push(&tokens, CNA_SRC_SWZ(CNA_TEMP, 2, variant == 4 ? 0 : 0xe4) |
                       (variant == 1 ? 4u << 24 : 0));
        if (stage)
        {
            cna_words_push(&tokens, CNA_MOV); cna_words_push(&tokens, CNA_DST(CNA_TEMP, 0, 15));
            cna_words_push(&tokens, CNA_SRC(CNA_TEMP, stage));
        }
    }
    cna_words_push(&tokens, CNA_END);
    p = cna_parse(profile, tokens.at, tokens.count * 4); v = cna_parse(profile, vs, sizeof (vs));
    CNA_CHECK(cna_report_errors("legacy pixel shader", p) == 0, "legacy opcode accepted");
    CNA_CHECK(cna_report_errors("vertex shader", v) == 0, "vertex shader accepted");
    if (p && v && !p->error_count && !v->error_count)
    {
        memset(&pos, 0, sizeof (pos)); pos.usage = MOJOSHADER_USAGE_POSITION;
        pos.vertexElementFormat = MOJOSHADER_VERTEXELEMENTFORMAT_VECTOR4;
        if (!strcmp(profile, "glspirv")) MOJOSHADER_spirv_link_attributes(v, p, 1);
        else MOJOSHADER_linkSPIRVShaders(v, p, &pos, 1);
        numeric_result(p, depth ? (zero_divisor ? 2 : 1) : 0, variant);
        if (depth) depth_formula(p);
        else
        {
            CNA_CHECK(matrix_formula(p), "BEM has literal D3D9 column/swizzle dataflow");
            CNA_CHECK(p->sampler_count == 1 && p->samplers[0].index == stage &&
                      p->samplers[0].texbem != 0, "matrix uniform belongs to destination-numbered stage");
        }
        /* Microsoft D3D9 bem/texdepth specs settle semantics; spirv-val settles module validity. */
        CNA_CHECK(cna_spirv_validate(p, depth ? "texdepth" : "bem") == 1, "valid pixel module");
        CNA_CHECK(cna_spirv_validate(v, "legacy_vs") == 1, "valid linked vertex module");
    }
    MOJOSHADER_freeParseData(p); MOJOSHADER_freeParseData(v); cna_words_free(&tokens);
}
static void bem_stages(void)
{
    int stage;
    for (stage = 0; stage < 6; stage++)
    { check("spirv", 0, stage, 0, 0); check("glspirv", 0, stage, 0, 0); }
}
static void texdepth_ratio(void)
{ check("spirv", 1, 0, 0, 0); check("glspirv", 1, 0, 0, 0); }
static void texdepth_zero(void)
{ check("spirv", 1, 0, 1, 0); check("glspirv", 1, 0, 1, 0); }
/* Keep the owner's uncommissioned six-opcode envelope explicit. A D3D parse first proves
 * these token streams are valid; only the missing SPIR-V emitter may reject each one. */
static void remaining_refusals(void)
{
    static const struct { unsigned int opcode; const char *name; } cases[] = {
        {69, "TEXREG2AR"}, {70, "TEXREG2GB"}, {82, "TEXREG2RGB"},
        {83, "TEXDP3TEX"}, {84, "TEXM3X2DEPTH"}, {85, "TEXDP3"}
    };
    static const char *profiles[] = {"spirv", "glspirv"};
    size_t i, j;
    for (i = 0; i < sizeof (cases) / sizeof (cases[0]); i++)
    {
        CnaWords w = {0};
        const MOJOSHADER_parseData *p;
        const int depth = cases[i].opcode == 84;
        cna_words_push(&w, CNA_PS(1, depth ? 3 : 2));
        cna_words_push(&w, depth ? CNA_TEXCRD : CNA_TEX);
        cna_words_push(&w, CNA_DST(CNA_TEXTURE, 0, 15));
        if (depth)
        {
            cna_words_push(&w, 71); cna_words_push(&w, CNA_DST(CNA_TEXTURE, 1, 15));
            cna_words_push(&w, CNA_SRC(CNA_TEXTURE, 0));
        }
        cna_words_push(&w, cases[i].opcode);
        cna_words_push(&w, CNA_DST(CNA_TEXTURE, depth ? 2 : 1, 15));
        cna_words_push(&w, CNA_SRC(CNA_TEXTURE, 0));
        cna_words_push(&w, CNA_MOV); cna_words_push(&w, CNA_DST(CNA_TEMP, 0, 15));
        cna_words_push(&w, CNA_SRC(depth ? CNA_CONST : CNA_TEXTURE, depth ? 0 : 1));
        cna_words_push(&w, CNA_END);
        p = cna_parse("d3d", w.at, w.count * 4);
        CNA_CHECK(cna_report_errors(cases[i].name, p) == 0, "valid D3D9 legacy opcode control");
        MOJOSHADER_freeParseData(p);
        for (j = 0; j < sizeof (profiles) / sizeof (profiles[0]); j++)
        {
            char expected[80];
            snprintf(expected, sizeof (expected), "%s unimplemented in spirv profile", cases[i].name);
            p = cna_parse(profiles[j], w.at, w.count * 4);
            CNA_CHECK(p && p->error_count == 1, "one named remaining-opcode refusal");
            if (p && p->error_count == 1)
                CNA_CHECK(!strcmp(p->errors[0].error, expected), "only the missing emitter refuses the shader");
            MOJOSHADER_freeParseData(p);
        }
        cna_words_free(&w);
    }
}
static void bem_modifiers(void)
{
    int variant;
    for (variant = 1; variant <= 4; variant++)
    { check("spirv", 0, 2, 0, variant); check("glspirv", 0, 2, 0, variant); }
}
int main(void)
{
    cna_test_run("bem_destination_stages", bem_stages);
    cna_test_run("bem_source_and_result_modifiers", bem_modifiers);
    cna_test_run("texdepth_guarded_ratio", texdepth_ratio);
    cna_test_run("texdepth_zero_divisor", texdepth_zero);
    cna_test_run("six_remaining_opcodes_refuse_by_name", remaining_refusals);
    return cna_test_finish();
}
