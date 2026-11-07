/* SPDX-License-Identifier: Zlib */
/* Microsoft D3D9 texreg2ar/gb/rgb contracts: destination-numbered sampling at
 * source.AR, source.GB, or source.RGB. The source sample and result are distinct
 * literal colours; interpreting standard SPIR-V operations checks their dataflow.
 * https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/texreg2ar---ps
 * https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/texreg2gb---ps
 * https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/texreg2rgb---ps
 * https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/dx9-graphics-reference-asm-ps-registers-modifiers-signed-scale */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "cna_test.h"
#include "spirv/spirv.h"

#define IR_LIMIT 4096
typedef struct Value { float lane[4]; unsigned int known; int width; } Value;

static void numeric_lookup(const MOJOSHADER_parseData *p, int opcode, int source_stage,
                           int destination_stage, int dimension, int signed_source)
{
    static Value value[IR_LIMIT], memory[IR_LIMIT];
    static int width[IR_LIMIT], storage[IR_LIMIT], binding[IR_LIMIT], image[IR_LIMIT];
    static int builtin[IR_LIMIT], input_stage[IR_LIMIT];
    static const float source[] = {.125f, .375f, .625f, .875f};
    static const float sampled[] = {.9375f, .6875f, .4375f, .1875f};
    const uint32_t *words = (const uint32_t *) p->output;
    const size_t count = cna_spirv_module_words(p);
    size_t at;
    int i, source_reads = 0, destination_reads = 0, outputs = 0, dots = 0;
    static const float plain_dot[] = {.34375f, .375f, .40625f};
    static const float signed_dot[] = {-2.0625f, -2.25f, -2.4375f};
    const float dot = (signed_source ? signed_dot : plain_dot)[destination_stage - 1];
    CNA_CHECK(words[3] < IR_LIMIT, "numeric lookup ID bound");
    if (words[3] >= IR_LIMIT) return;
    memset(value, 0, sizeof(value)); memset(memory, 0, sizeof(memory));
    memset(width, 0, sizeof(width)); memset(builtin, 0, sizeof(builtin));
    for (i = 0; i < IR_LIMIT; i++) storage[i] = binding[i] = image[i] = input_stage[i] = -1;
    for (at = 5; at < count && (words[at] >> 16); at += words[at] >> 16)
    {
        const uint32_t *d = words + at;
        const unsigned int op = d[0] & 0xffff;
        if (op == SpvOpName)
        {
            int stage;
            if (sscanf((const char *)(d + 2), "ps_t%d", &stage) == 1 && stage >= 0 && stage < 4)
                input_stage[d[1]] = stage;
        }
        if (op == SpvOpDecorate && d[2] == SpvDecorationBinding) binding[d[1]] = d[3];
        if (op == SpvOpDecorate && d[2] == SpvDecorationBuiltIn) builtin[d[1]] = 1;
        if (op == SpvOpTypeFloat || op == SpvOpTypeInt) width[d[1]] = 1;
        if (op == SpvOpTypeVector) width[d[1]] = d[3];
        if (op == SpvOpVariable)
        {
            storage[d[2]] = d[3];
            if (d[3] == SpvStorageClassInput && input_stage[d[2]] >= 0)
            {
                Value *v = &memory[d[2]];
                v->lane[0] = 2.0f + input_stage[d[2]] * .25f;
                v->lane[1] = 1; v->lane[2] = -.5f; v->lane[3] = 9;
                v->width = 4; v->known = 15;
            }
        }
        if (op == SpvOpConstant)
        {
            memcpy(value[d[2]].lane, d + 3, sizeof(float));
            value[d[2]].known = 1; value[d[2]].width = 1;
        }
        if (op == SpvOpConstantComposite || op == SpvOpCompositeConstruct)
        {
            Value *v = &value[d[2]];
            v->width = width[d[1]];
            for (i = 0; i < v->width; i++)
            {
                v->lane[i] = value[d[3 + i]].lane[0];
                if (value[d[3 + i]].known & 1) v->known |= 1u << i;
            }
        }
        if (op == SpvOpLoad)
        {
            value[d[2]] = memory[d[3]];
            value[d[2]].width = width[d[1]];
            image[d[2]] = binding[d[3]];
        }
        if (op == SpvOpStore) memory[d[1]] = value[d[2]];
        if (op == SpvOpCopyObject) { value[d[2]] = value[d[3]]; image[d[2]] = image[d[3]]; }
        if (op == SpvOpVectorShuffle)
        {
            Value *v = &value[d[2]], *a = &value[d[3]], *b = &value[d[4]];
            v->width = width[d[1]];
            for (i = 0; i < v->width; i++)
            {
                unsigned int lane = d[5 + i];
                Value *from = a;
                if (lane >= (unsigned int) a->width) { lane -= a->width; from = b; }
                CNA_CHECK(lane < 4, "lookup shuffle lane is in range");
                if (lane >= 4) continue;
                v->lane[i] = from->lane[lane];
                if (from->known & (1u << lane)) v->known |= 1u << i;
            }
        }
        if (op == SpvOpCompositeExtract)
        {
            value[d[2]].width = 1;
            value[d[2]].lane[0] = value[d[3]].lane[d[4]];
            value[d[2]].known = (value[d[3]].known >> d[4]) & 1;
        }
        if (op == SpvOpFMul || op == SpvOpFAdd || op == SpvOpFSub)
        {
            Value *v = &value[d[2]], *a = &value[d[3]], *b = &value[d[4]];
            v->width = width[d[1]]; v->known = a->known & b->known;
            for (i = 0; i < v->width; i++)
                v->lane[i] = op == SpvOpFMul ? a->lane[i] * b->lane[i] :
                    op == SpvOpFAdd ? a->lane[i] + b->lane[i] : a->lane[i] - b->lane[i];
        }
        if (op == SpvOpDot)
        {
            Value *v = &value[d[2]], *a = &value[d[3]], *b = &value[d[4]];
            dots++;
            CNA_CHECK(a->width == 3 && b->width == 3, "dependent dot reads only RGB/UVW");
            v->width = 1;
            v->known = a->width > 0 && a->width <= 4 && a->width == b->width &&
                (a->known & b->known) == (1u << a->width) - 1;
            v->lane[0] = 0;
            for (i = 0; i < a->width && i < 4; i++) v->lane[0] += a->lane[i] * b->lane[i];
        }
        if (op == SpvOpImageSampleImplicitLod)
        {
            const int stage = image[d[3]];
            Value *result = &value[d[2]];
            if (stage == source_stage)
            {
                source_reads++;
                memcpy(result->lane, source, sizeof(source));
            }
            else
            {
                const Value *coordinate = &value[d[4]];
                const int dimensions = dimension == MOJOSHADER_SAMPLER_2D ? 2 : 3;
                destination_reads++;
                CNA_CHECK(stage == destination_stage, "lookup uses the destination-numbered sampler");
                CNA_CHECK(coordinate->width == dimensions, "lookup uses the texture's coordinate dimension");
                CNA_CHECK(coordinate->known == (1u << dimensions) - 1,
                          "lookup coordinates come from a fully defined sampled value");
                for (i = 0; i < dimensions; i++)
                {
                    const int component = opcode == 69 ? (i == 0 ? 3 : 0) : opcode == 70 ? i + 1 : i;
                    const float expected = opcode == 83 ? (i == 0 ? dot : 0.0f) :
                        signed_source ? source[component] * 2 - 1 : source[component];
                    CNA_CHECK(coordinate->lane[i] == expected, "literal remapped texture coordinate");
                }
                memcpy(result->lane, sampled, sizeof(sampled));
            }
            result->known = 15; result->width = 4;
        }
    }
    CNA_CHECK(source_reads == 1 && destination_reads == (opcode == 85 ? 0 : 1), "exact source/dependent sample count");
    CNA_CHECK(dots == (opcode == 83 || opcode == 85 ? 1 : 0), "one dot exactly when required");
    for (i = 1; i < IR_LIMIT; i++)
        if (storage[i] == SpvStorageClassOutput && !builtin[i])
        {
            int lane;
            outputs++;
            CNA_CHECK(memory[i].known == 15, "every final colour lane is defined");
            for (lane = 0; lane < 4; lane++)
                CNA_CHECK(memory[i].lane[lane] == (opcode == 85 ? dot : sampled[lane]), "literal dot or dependent sample reaches every output lane");
        }
    CNA_CHECK(outputs == 1, "one final colour output");
}

static size_t shader(uint32_t *w, int opcode, int minor, int destination, int source, int modifier)
{
    size_t n = 0;
    w[n++] = CNA_PS(1, minor);
    w[n++] = CNA_TEX; w[n++] = CNA_DST(CNA_TEXTURE, source, 15);
    w[n++] = opcode; w[n++] = CNA_DST(CNA_TEXTURE, destination, 15);
    w[n++] = CNA_SRC(CNA_TEXTURE, source) | ((uint32_t) modifier << 24);
    w[n++] = CNA_MOV; w[n++] = CNA_DST(CNA_TEMP, 0, 15);
    w[n++] = CNA_SRC(CNA_TEXTURE, destination); w[n++] = CNA_END;
    return n;
}

static void coordinate_lookups(void)
{
    static const char *profiles[] = {"spirv", "glspirv"};
    static const int opcodes[] = {69, 70, 82};
    int profile, opcode, minor, destination, source, dimension, modifier, variants = 0;
    for (profile = 0; profile < 2; profile++) for (opcode = 0; opcode < 3; opcode++)
    for (minor = opcode == 0 ? 1 : 2; minor <= 3; minor++)
    for (destination = 1; destination <= 3; destination++) for (source = 0; source < destination; source++)
    for (dimension = MOJOSHADER_SAMPLER_2D; dimension <= (opcode == 2 ? MOJOSHADER_SAMPLER_VOLUME : MOJOSHADER_SAMPLER_2D); dimension++)
    for (modifier = 0; modifier <= (opcode == 2 ? 4 : 0); modifier += 4)
    {
        uint32_t words[16];
        const size_t n = shader(words, opcodes[opcode], minor, destination, source, modifier);
        MOJOSHADER_samplerMap map = {destination, (MOJOSHADER_samplerType) dimension};
        const MOJOSHADER_parseData *p, *control;
        control = MOJOSHADER_parse("d3d", "main", (const unsigned char *) words,
            n * sizeof(uint32_t), NULL, 0, &map, 1, NULL, NULL, NULL);
        CNA_CHECK(cna_report_errors("D3D texreg control", control) == 0, "legal source bytecode control");
        MOJOSHADER_freeParseData(control);
        p = MOJOSHADER_parse(profiles[profile], "main", (const unsigned char *) words,
            n * sizeof(uint32_t), NULL, 0, &map, 1, NULL, NULL, NULL);
        CNA_CHECK(cna_report_errors("SPIR-V texreg", p) == 0, "legal register-dependent lookup accepted");
        if (p && !p->error_count)
        {
            int i, reflected = 0;
            numeric_lookup(p, opcodes[opcode], source, destination, dimension, modifier != 0);
            for (i = 0; i < p->sampler_count; i++)
                if (p->samplers[i].index == destination)
                { reflected++; CNA_CHECK(p->samplers[i].type == dimension, "mapped sampler type retained"); }
            CNA_CHECK(reflected == 1, "one reflected dependent sampler");
            CNA_CHECK(cna_spirv_validate(p, "texreg") == 1, "valid register-dependent lookup module");
        }
        MOJOSHADER_freeParseData(p); variants++;
    }
    CNA_CHECK(variants == 204, "all legal profile/version/stage/type/modifier combinations visited");
}

/* Microsoft TEXDP3/DP3TEX: dot(UVW,RGB), ignoring both alpha lanes.
 * Input tk=(2+k/4,1,-1/2,9), source=(1/8,3/8,5/8,7/8):
 * ordinary dot=5/16+k/32, signed dot=-15/8-3k/16.
 * https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/texdp3---ps
 * https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/texdp3tex---ps */
static void dependent_dots(void)
{
    static const char *profiles[]={"spirv","glspirv"};
    static const int opcodes[]={83,85};
    int profile,opcode,minor,destination,source,modifier,variants=0;
    for (profile=0; profile<2; profile++) for (opcode=0; opcode<2; opcode++)
    for (minor=2; minor<=3; minor++) for (destination=1; destination<=3; destination++)
    for (source=0; source<destination; source++) for (modifier=0; modifier<=4; modifier+=4)
    {
        uint32_t words[16];
        const size_t n=shader(words,opcodes[opcode],minor,destination,source,modifier);
        const MOJOSHADER_parseData *p,*control;
        control=cna_parse("d3d",words,n*sizeof(uint32_t));
        CNA_CHECK(cna_report_errors("D3D dependent dot control",control)==0,"legal dot bytecode control");
        MOJOSHADER_freeParseData(control);
        p=cna_parse(profiles[profile],words,n*sizeof(uint32_t));
        CNA_CHECK(cna_report_errors("SPIR-V dependent dot",p)==0,"dependent dot instruction is implemented");
        if(p && !p->error_count)
        {
            numeric_lookup(p,opcodes[opcode],source,destination,MOJOSHADER_SAMPLER_2D,modifier!=0);
            CNA_CHECK(p->sampler_count==(opcodes[opcode]==85?1:2),"TEXDP3 does not declare a destination sampler");
            CNA_CHECK(cna_spirv_validate(p,"dependent_dot")==1,"valid dependent dot module");
        }
        MOJOSHADER_freeParseData(p);variants++;
    }
    CNA_CHECK(variants==96,"all legal dot versions/stages/source modifiers/profiles visited");
}

static void dependent_dot_sampler_dimensions(void)
{
    static const char *profiles[]={"spirv","glspirv"};
    int profile,dimension;
    for(profile=0;profile<2;profile++)
    for(dimension=MOJOSHADER_SAMPLER_CUBE;dimension<=MOJOSHADER_SAMPLER_VOLUME;dimension++)
    {
        uint32_t words[16];
        const size_t n=shader(words,83,2,1,0,0);
        MOJOSHADER_samplerMap map={1,(MOJOSHADER_samplerType)dimension};
        const MOJOSHADER_parseData *p=MOJOSHADER_parse(profiles[profile],"main",(const unsigned char*)words,n*4,NULL,0,&map,1,NULL,NULL,NULL);
        CNA_CHECK(p && p->error_count==1,"dependent dot lookup refuses non-2D map");
        if(p && p->error_count==1) CNA_CHECK(strstr(p->errors[0].error,"TEXDP3TEX needs a 2D sampler")!=NULL,"specific dependent dot dimension refusal");
        MOJOSHADER_freeParseData(p);
    }
}

static void incompatible_sampler_types(void)
{
    static const char *profiles[] = {"spirv", "glspirv"};
    int profile, opcode, dimension;
    for (profile = 0; profile < 2; profile++) for (opcode = 69; opcode <= 70; opcode++)
    for (dimension = MOJOSHADER_SAMPLER_CUBE; dimension <= MOJOSHADER_SAMPLER_VOLUME; dimension++)
    {
        uint32_t words[16];
        const size_t n = shader(words, opcode, 2, 1, 0, 0);
        MOJOSHADER_samplerMap map = {1, (MOJOSHADER_samplerType) dimension};
        const MOJOSHADER_parseData *p = MOJOSHADER_parse(profiles[profile], "main",
            (const unsigned char *) words, n * sizeof(uint32_t), NULL, 0, &map, 1, NULL, NULL, NULL);
        CNA_CHECK(p && p->error_count == 1, "two-coordinate opcode refuses a non-2D texture");
        if (p && p->error_count == 1)
            CNA_CHECK(strstr(p->errors[0].error, "requires a 2D sampler") != NULL,
                      "incompatible mapped sampler has a specific dimension diagnostic");
        MOJOSHADER_freeParseData(p);
    }
}

int main(void)
{
    cna_test_run("texreg_literal_coordinates_and_sample_dataflow", coordinate_lookups);
    cna_test_run("texreg_two_coordinate_sampler_dimensions", incompatible_sampler_types);
    cna_test_run("dependent_dot_literal_dataflow", dependent_dots);
    cna_test_run("dependent_dot_sampler_dimensions", dependent_dot_sampler_dimensions);
    return cna_test_finish();
}
