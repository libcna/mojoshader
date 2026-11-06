/* SPDX-License-Identifier: Zlib */
/* Independent numeric predicate truth tables and portable SPIR-V control flow. */
#include <string.h>
#include "cna_test.h"
#include "spirv/spirv.h"
#define LIMIT 4096
typedef struct Value { float v[4]; unsigned int known; int width; } Value;

static void kill_condition(const MOJOSHADER_parseData *p,int expected)
{
    static Value values[LIMIT],memory[LIMIT];
    static int widths[LIMIT],kills[LIMIT];
    const uint32_t *w=(const uint32_t *)p->output;
    const size_t n=cna_spirv_module_words(p);
    size_t at;
    int i,block=0,branches=0;
    CNA_CHECK(w[3]<LIMIT,"predicate oracle ID bound");if(w[3]>=LIMIT) return;
    memset(values,0,sizeof(values));memset(memory,0,sizeof(memory));
    memset(widths,0,sizeof(widths));memset(kills,0,sizeof(kills));
    for(at=5;at<n && (w[at]>>16);at+=w[at]>>16)
    {
        const uint32_t *d=w+at;unsigned int op=d[0]&0xffff;
        if(op==SpvOpTypeBool || op==SpvOpTypeFloat) widths[d[1]]=1;
        if(op==SpvOpTypeVector) widths[d[1]]=d[3];
        if(op==SpvOpLabel) block=d[1];
        if(op==SpvOpKill) kills[block]=1;
    }
    for(at=5;at<n && (w[at]>>16);at+=w[at]>>16)
    {
        const uint32_t *d=w+at;unsigned int op=d[0]&0xffff;
        if(op==SpvOpConstant)
        { memcpy(values[d[2]].v,d+3,sizeof(float));values[d[2]].known=1;values[d[2]].width=1; }
        if(op==SpvOpConstantTrue || op==SpvOpConstantFalse)
        { values[d[2]].v[0]=op==SpvOpConstantTrue;values[d[2]].known=1;values[d[2]].width=1; }
        if(op==SpvOpConstantComposite || op==SpvOpCompositeConstruct)
        {
            Value *v=&values[d[2]];v->width=widths[d[1]];
            for(i=0;i<v->width;i++)
            { v->v[i]=values[d[3+i]].v[0];if(values[d[3+i]].known&1) v->known|=1u<<i; }
        }
        if(op==SpvOpLoad) { values[d[2]]=memory[d[3]];values[d[2]].width=widths[d[1]]; }
        if(op==SpvOpStore) memory[d[1]]=values[d[2]];
        if(op==SpvOpVectorShuffle)
        {
            Value *v=&values[d[2]],*a=&values[d[3]],*b=&values[d[4]];
            v->width=widths[d[1]];
            for(i=0;i<v->width;i++)
            {
                unsigned int lane=d[5+i];Value *source=a;
                if(lane>=(unsigned int)a->width) { lane-=a->width;source=b; }
                CNA_CHECK(lane<4,"predicate shuffle component bound");if(lane>=4) continue;
                v->v[i]=source->v[lane];if(source->known&(1u<<lane)) v->known|=1u<<i;
            }
        }
        if(op==SpvOpFOrdGreaterThan || op==SpvOpFOrdLessThan || op==SpvOpLogicalAnd || op==SpvOpLogicalOr)
        {
            Value *v=&values[d[2]],*a=&values[d[3]],*b=&values[d[4]];
            v->width=widths[d[1]];v->known=a->known&b->known;
            /* Naga's SPIR-V frontend cannot resolve vector &&/||; Vulkan validation alone
             * does not catch that portability defect. The WEBGPU kill witness reproduces it. */
            if(op==SpvOpLogicalAnd || op==SpvOpLogicalOr)
                CNA_CHECK(v->width==1,"portable predicates avoid vector logical binary operators");
            for(i=0;i<v->width;i++)
                v->v[i]=op==SpvOpFOrdGreaterThan?a->v[i]>b->v[i]:op==SpvOpFOrdLessThan?a->v[i]<b->v[i]:
                        op==SpvOpLogicalAnd?(a->v[i]!=0 && b->v[i]!=0):(a->v[i]!=0 || b->v[i]!=0);
        }
        if(op==SpvOpLogicalNot)
        {
            Value *v=&values[d[2]],*a=&values[d[3]];*v=*a;
            for(i=0;i<v->width;i++) v->v[i]=!a->v[i];
        }
        if(op==SpvOpSelect)
        {
            Value *v=&values[d[2]],*condition=&values[d[3]];
            v->width=widths[d[1]];
            for(i=0;i<v->width;i++)
            {
                int lane=condition->width==1?0:i;
                Value *source=&values[condition->v[lane]?d[4]:d[5]];
                v->v[i]=source->v[i];
                if((condition->known&(1u<<lane)) && (source->known&(1u<<i))) v->known|=1u<<i;
            }
        }
        if(op==SpvOpAny)
        {
            Value *v=&values[d[2]],*a=&values[d[3]];v->width=1;
            for(i=0;i<a->width;i++) if(a->v[i]) v->v[0]=1;
            if(a->known==((1u<<a->width)-1)) v->known=1;
        }
        if(op==SpvOpCopyObject) values[d[2]]=values[d[3]];
        if(op==SpvOpBranchConditional && (kills[d[2]] || kills[d[3]]))
        {
            Value *condition=&values[d[1]];branches++;
            CNA_CHECK(condition->known==1,"kill condition is defined");
            CNA_CHECK(kills[condition->v[0]?d[2]:d[3]]==expected,"literal per-lane predicate/negative-component truth table");
        }
    }
    CNA_CHECK(branches==1,"one conditional kill branch");
}
static void predicated_kill(void)
{
    static const char *profiles[]={"spirv","glspirv"};
    static const unsigned int selectors[]={0xe4,0x00,0x55,0xaa,0xff};
    int profile,bits,selector,negated,negative;
    for(profile=0;profile<2;profile++) for(bits=0;bits<16;bits++) for(selector=0;selector<5;selector++)
        for(negated=0;negated<2;negated++) for(negative=0;negative<2;negative++)
    {
        CnaWords w={0};const MOJOSHADER_parseData *p;
        const int negative_mask=negative?8:5; /* xyz negated lanes, or only w which cannot kill */
        int i,expected=0;
        cna_words_push(&w,CNA_PS(3,0));
        cna_words_push(&w,CNA_OP(CNA_DEF,5));cna_words_push(&w,CNA_DST(CNA_CONST,0,15));
        for(i=0;i<4;i++) cna_words_push(&w,(bits&(1<<i))?0x3f800000:0);
        cna_words_push(&w,CNA_OP(CNA_DEF,5));cna_words_push(&w,CNA_DST(CNA_CONST,1,15));
        for(i=0;i<4;i++) cna_words_push(&w,0x3f000000);
        cna_words_push(&w,CNA_OP(CNA_DEF,5));cna_words_push(&w,CNA_DST(CNA_CONST,2,15));
        for(i=0;i<4;i++) cna_words_push(&w,(negative_mask&(1<<i))?0xbf800000:0x3f800000);
        cna_words_push(&w,CNA_OP(94,3)|(1u<<16));cna_words_push(&w,CNA_DST(19,0,15));
        cna_words_push(&w,CNA_SRC(CNA_CONST,0));cna_words_push(&w,CNA_SRC(CNA_CONST,1));
        cna_words_push(&w,CNA_OP(CNA_MOV,2));cna_words_push(&w,CNA_DST(CNA_TEMP,0,15));cna_words_push(&w,CNA_SRC(CNA_CONST,2));
        cna_words_push(&w,CNA_OP(65,2)|(1u<<28));cna_words_push(&w,CNA_DST(CNA_TEMP,0,15));
        cna_words_push(&w,CNA_SRC_SWZ(19,0,selectors[selector])|(negated?13u<<24:0));
        cna_words_push(&w,CNA_OP(CNA_MOV,2));cna_words_push(&w,CNA_DST(CNA_COLOROUT,0,15));
        cna_words_push(&w,CNA_SRC(CNA_CONST,0));cna_words_push(&w,CNA_END);
        for(i=0;i<3;i++)
        {
            int selected=(bits>>((selectors[selector]>>(2*i))&3))&1;
            if((selected^negated) && (negative_mask&(1<<i))) expected=1;
        }
        p=cna_parse(profiles[profile],w.at,w.count*4);
        CNA_CHECK(cna_report_errors("predicated kill",p)==0,"predicate fixture accepted");
        if(p && !p->error_count)
        { kill_condition(p,expected);CNA_CHECK(cna_spirv_validate(p,"predicated_kill")==1,"valid predicated-kill module"); }
        MOJOSHADER_freeParseData(p);cna_words_free(&w);
    }
}
int main(void)
{ cna_test_run("predicated_kill_truth_table",predicated_kill);return cna_test_finish(); }
