/* SPDX-License-Identifier: Zlib */
/* Sampler operand swizzles select sampled RGBA components, never image handles. */
#include <math.h>
#include <string.h>
#include "cna_test.h"
#include "spirv/spirv.h"
#define LIMIT 4096
typedef struct Value { float v[4]; unsigned int known; int width; } Value;
static void sample_output(const MOJOSHADER_parseData *p,unsigned int swizzle,unsigned int mask)
{
    static Value values[LIMIT],memory[LIMIT];
    static int widths[LIMIT],storage[LIMIT],builtin[LIMIT];
    const uint32_t *w=(const uint32_t *)p->output;
    size_t at,n=cna_spirv_module_words(p);
    int i,outputs=0;
    static const float sampled[]={.125f,.25f,.5f,1};
    static const float initial[]={.125f,.25f,.375f,.5f};
    CNA_CHECK(w[3]<LIMIT,"numeric sampler oracle ID bound");
    if(w[3]>=LIMIT) return;
    memset(values,0,sizeof(values));memset(memory,0,sizeof(memory));memset(widths,0,sizeof(widths));
    for(i=0;i<LIMIT;i++) { storage[i]=-1;builtin[i]=0; }
    for(at=5;at<n && (w[at]>>16);at+=w[at]>>16)
    {
        const uint32_t *d=w+at;
        unsigned int op=d[0]&0xffff;
        if(op==SpvOpDecorate && d[2]==SpvDecorationBuiltIn) builtin[d[1]]=1;
        if(op==SpvOpTypeFloat) widths[d[1]]=1;
        if(op==SpvOpTypeVector) widths[d[1]]=d[3];
        if(op==SpvOpVariable) storage[d[2]]=d[3];
        if(op==SpvOpConstant)
        { memcpy(values[d[2]].v,d+3,sizeof(float));values[d[2]].known=1;values[d[2]].width=1; }
        if(op==SpvOpConstantComposite || op==SpvOpCompositeConstruct)
        {
            Value *v=&values[d[2]];v->width=widths[d[1]];
            for(i=0;i<v->width;i++)
            { v->v[i]=values[d[3+i]].v[0];if(values[d[3+i]].known&1) v->known|=1u<<i; }
        }
        if(op==SpvOpLoad) { values[d[2]]=memory[d[3]];values[d[2]].width=widths[d[1]]; }
        if(op==SpvOpStore) memory[d[1]]=values[d[2]];
        if(op==SpvOpImageSampleImplicitLod || op==SpvOpImageSampleExplicitLod ||
           op==SpvOpImageSampleProjImplicitLod)
        {
            memcpy(values[d[2]].v,sampled,sizeof(sampled));
            values[d[2]].known=15;values[d[2]].width=4;
        }
        if(op==SpvOpVectorShuffle)
        {
            Value *v=&values[d[2]],*a=&values[d[3]],*b=&values[d[4]];
            v->width=widths[d[1]];
            for(i=0;i<v->width;i++)
            {
                unsigned int lane=d[5+i];Value *source=a;
                if(lane>=(unsigned int)a->width) { lane-=a->width;source=b; }
                CNA_CHECK(lane<4,"numeric sampler shuffle index");
                if(lane>=4) continue;
                v->v[i]=source->v[lane];if(source->known&(1u<<lane)) v->known|=1u<<i;
            }
        }
        if(op==SpvOpCopyObject) values[d[2]]=values[d[3]];
    }
    for(i=1;i<LIMIT;i++) if(storage[i]==SpvStorageClassOutput && !builtin[i])
    {
        int lane;outputs++;
        CNA_CHECK(memory[i].known==15,"all final output lanes are defined");
        for(lane=0;lane<4;lane++)
        {
            const float expected=(mask&(1u<<lane))?sampled[(swizzle>>(2*lane))&3]:initial[lane];
            CNA_CHECK(fabsf(memory[i].v[lane]-expected)<.000001f,
                      "sampler selector permutes sampled values and preserves unwritten output lanes");
        }
    }
    CNA_CHECK(outputs==1,"one numeric sampler oracle output");
}
static void sampler_selectors(void)
{
    static const char *profiles[]={"spirv","glspirv"};
    static const unsigned int selectors[]={0xe4,0xc6,0xff}; /* identity, bgra, aaaa */
    size_t profile,selector;
    int route,partial,dimension;
    for(profile=0;profile<2;profile++) for(route=0;route<4;route++)
        for(selector=0;selector<3;selector++) for(partial=0;partial<2;partial++) for(dimension=2;dimension<=4;dimension++)
    {
        CnaWords w={0};const MOJOSHADER_parseData *p;
        const int vertex=route==3;
        const unsigned int opcode=route==1?93:route>=2?95:66;
        const unsigned int mask=partial?5:15;
        const uint32_t setup[]={
            CNA_OP(CNA_DEF,5),CNA_DST(CNA_CONST,0,15),0x3e800000,0x3f000000,0,0,
            CNA_OP(CNA_DEF,5),CNA_DST(CNA_CONST,1,15),0x3e000000,0x3e800000,0x3ec00000,0x3f000000,
            CNA_OP(CNA_DCL_OP,2),0x80000000u|((uint32_t)dimension<<27),CNA_DST(CNA_SAMPLER,0,15)
        };
        size_t i;
        cna_words_push(&w,vertex?CNA_VS(3,0):CNA_PS(3,0));
        for(i=0;i<sizeof(setup)/sizeof(setup[0]);i++) cna_words_push(&w,setup[i]);
        if(vertex)
        {
            cna_words_push(&w,CNA_OP(CNA_DCL_OP,2));cna_words_push(&w,CNA_DCL(0,0));cna_words_push(&w,CNA_DST(CNA_OUTPUT,0,15));
            cna_words_push(&w,CNA_OP(CNA_DCL_OP,2));cna_words_push(&w,CNA_DCL(5,0));cna_words_push(&w,CNA_DST(CNA_OUTPUT,1,15));
            cna_words_push(&w,CNA_OP(CNA_MOV,2));cna_words_push(&w,CNA_DST(CNA_OUTPUT,0,15));cna_words_push(&w,CNA_SRC(CNA_CONST,0));
        }
        cna_words_push(&w,CNA_OP(CNA_MOV,2));cna_words_push(&w,CNA_DST(CNA_TEMP,0,15));cna_words_push(&w,CNA_SRC(CNA_CONST,1));
        cna_words_push(&w,CNA_OP(opcode,opcode==93?5:3));cna_words_push(&w,CNA_DST(CNA_TEMP,0,mask));
        cna_words_push(&w,CNA_SRC(CNA_CONST,0));cna_words_push(&w,CNA_SRC_SWZ(CNA_SAMPLER,0,selectors[selector]));
        if(opcode==93)
        { cna_words_push(&w,CNA_SRC(CNA_CONST,0));cna_words_push(&w,CNA_SRC(CNA_CONST,0)); }
        cna_words_push(&w,CNA_OP(CNA_MOV,2));cna_words_push(&w,CNA_DST(vertex?CNA_OUTPUT:CNA_COLOROUT,vertex?1:0,15));
        cna_words_push(&w,CNA_SRC(CNA_TEMP,0));cna_words_push(&w,CNA_END);
        p=cna_parse(profiles[profile],w.at,w.count*4);
        CNA_CHECK(cna_report_errors("sampler selector",p)==0,"legal sampler selector accepted");
        if(p && !p->error_count)
        {
            sample_output(p,selectors[selector],mask);
            CNA_CHECK(cna_spirv_validate(p,"sampler_selector")==1,"valid sampler-selector module");
        }
        MOJOSHADER_freeParseData(p);cna_words_free(&w);
    }
}
int main(void)
{ cna_test_run("sample_result_selectors",sampler_selectors);return cna_test_finish(); }
