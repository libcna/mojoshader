/* SPDX-License-Identifier: Zlib */
/* Independent numeric predicate/result-modifier oracles and portable SPIR-V control flow. */
#include <string.h>
#include <math.h>
#include "spirv/GLSL.std.450.h"
#include "cna_test.h"
#include "spirv/spirv.h"
#define LIMIT 4096
typedef struct Value { float v[4]; unsigned int known; int width; } Value;

static void program_result(const MOJOSHADER_parseData *p,int expected,const float *output,int output_kind,int output_width)
{
    static Value values[LIMIT],memory[LIMIT];
    static int widths[LIMIT],kills[LIMIT],kinds[LIMIT],pointee[LIMIT],storage[LIMIT],var_type[LIMIT],position[LIMIT];
    const uint32_t *w=(const uint32_t *)p->output;
    const size_t n=cna_spirv_module_words(p);
    size_t at;
    int i,block=0,branches=0;
    CNA_CHECK(w[3]<LIMIT,"predicate oracle ID bound");if(w[3]>=LIMIT) return;
    memset(values,0,sizeof(values));memset(memory,0,sizeof(memory));
    memset(widths,0,sizeof(widths));memset(kills,0,sizeof(kills));
    memset(kinds,0,sizeof(kinds));memset(pointee,0,sizeof(pointee));memset(var_type,0,sizeof(var_type));
    memset(position,0,sizeof(position));for(i=0;i<LIMIT;i++) storage[i]=-1;
    for(at=5;at<n && (w[at]>>16);at+=w[at]>>16)
    {
        const uint32_t *d=w+at;unsigned int op=d[0]&0xffff;
        if(op==SpvOpTypeBool || op==SpvOpTypeFloat || op==SpvOpTypeInt)
        { widths[d[1]]=1;kinds[d[1]]=op==SpvOpTypeFloat?1:op==SpvOpTypeBool?2:3; }
        if(op==SpvOpTypeVector) { widths[d[1]]=d[3];kinds[d[1]]=kinds[d[2]]; }
        if(op==SpvOpTypePointer) pointee[d[1]]=d[3];
        if(op==SpvOpVariable) { storage[d[2]]=d[3];var_type[d[2]]=pointee[d[1]]; }
        if(op==SpvOpDecorate && d[2]==SpvDecorationBuiltIn && d[3]==SpvBuiltInPosition) position[d[1]]=1;
        if(op==SpvOpLabel) block=d[1];
        if(op==SpvOpKill) kills[block]=1;
    }
    for(at=5;at<n && (w[at]>>16);at+=w[at]>>16)
    {
        const uint32_t *d=w+at;unsigned int op=d[0]&0xffff;
        if(op==SpvOpConstant)
        {
            if(kinds[d[1]]==3) values[d[2]].v[0]=(float)(int32_t)d[3];
            else memcpy(values[d[2]].v,d+3,sizeof(float));
            values[d[2]].known=1;values[d[2]].width=1;
        }
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
        if(op==SpvOpCompositeExtract)
        {
            values[d[2]].width=1;values[d[2]].v[0]=values[d[3]].v[d[4]];
            values[d[2]].known=(values[d[3]].known>>d[4])&1;
        }
        if(op==SpvOpFAdd || op==SpvOpFMul)
        {
            Value *v=&values[d[2]],*a=&values[d[3]],*b=&values[d[4]];
            v->width=widths[d[1]];v->known=a->known&b->known;
            for(i=0;i<v->width;i++) v->v[i]=op==SpvOpFAdd?a->v[i]+b->v[i]:a->v[i]*b->v[i];
        }
        if(op==SpvOpExtInst)
        {
            Value *v=&values[d[2]],*a=&values[d[5]];v->width=widths[d[1]];v->known=a->known;
            CNA_CHECK(d[4]==GLSLstd450FClamp || d[4]==GLSLstd450Round,"known numeric predicate operation");
            for(i=0;i<v->width;i++)
            {
                float x=a->v[i];
                if(d[4]==GLSLstd450Round) v->v[i]=roundf(x);
                else
                {
                    float lo=values[d[6]].v[i],hi=values[d[7]].v[i];
                    v->v[i]=x<lo?lo:x>hi?hi:x;
                    v->known&=values[d[6]].known&values[d[7]].known;
                }
            }
        }
        if(op==SpvOpConvertFToS)
        { values[d[2]]=values[d[3]];for(i=0;i<values[d[2]].width;i++) values[d[2]].v[i]=truncf(values[d[3]].v[i]); }
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
    if(expected>=0) CNA_CHECK(branches==1,"one conditional kill branch");
    if(output)
    {
        int observed=0;
        for(i=1;i<LIMIT;i++)
        {
            int matches=output_kind==0?(storage[i]==SpvStorageClassOutput && !position[i]):
                (storage[i]==SpvStorageClassFunction || storage[i]==SpvStorageClassPrivate) && kinds[var_type[i]]==output_kind;
            if(matches && widths[var_type[i]]==output_width)
            {
                int lane;observed++;
                CNA_CHECK(memory[i].known==((1u<<output_width)-1),"all observed destination lanes are defined");
                for(lane=0;lane<output_width;lane++)
                    CNA_CHECK(fabsf(memory[i].v[lane]-output[lane])<.000001f,
                              "numeric destination has the expected updated and preserved values");
            }
        }
        CNA_CHECK(observed==1,"one observed numeric destination");
    }
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
        { program_result(p,expected,NULL,0,0);CNA_CHECK(cna_spirv_validate(p,"predicated_kill")==1,"valid predicated-kill module"); }
        MOJOSHADER_freeParseData(p);cna_words_free(&w);
    }
}
static void push_float(CnaWords *w,float value)
{ uint32_t bits;memcpy(&bits,&value,sizeof(bits));cna_words_push(w,bits); }
static void predicated_writes(void)
{
    static const char *profiles[]={"spirv","glspirv"};
    static const unsigned int predicates[]={0,5,15},selectors[]={0xe4,0,0x55,0xaa,0xff},masks[]={15,6,8};
    int profile,pattern,selector,negated,mask_index,mode;
    for(profile=0;profile<2;profile++) for(pattern=0;pattern<3;pattern++) for(selector=0;selector<5;selector++)
        for(negated=0;negated<2;negated++) for(mask_index=0;mask_index<3;mask_index++) for(mode=0;mode<6;mode++)
    {
        CnaWords w={0};const MOJOSHADER_parseData *p;
        float constants[4][4]={{.25f,.5f,.75f,1},{2,-1,.125f,3},{0},{.5f,.5f,.5f,.5f}},expected[4];
        const unsigned int bits=predicates[pattern],mask=mode==3?1:masks[mask_index];
        int i,c,vertex=mode==5;
        unsigned int destination=mode==3?CNA_DEPTHOUT:mode==4?19:mode==5?CNA_ADDR:CNA_TEMP;
        unsigned int opcode=mode==1?CNA_ADD:mode==4?94:mode==5?46:CNA_MOV;
        if(mode==2)
        {
            const float initial[]={-.25f,1.5f,2,-1};
            memcpy(constants[0],initial,sizeof(initial));
        }
        if(mode==3) constants[1][0]=.75f;
        if(mode==4) for(i=0;i<4;i++) constants[1][i]=2;
        if(mode==5)
        {
            const float initial[]={1.2f,-2.2f,3.2f,-4.2f},next[]={2.7f,-3.7f,4.7f,-5.7f};
            memcpy(constants[0],initial,sizeof(initial));memcpy(constants[1],next,sizeof(next));
        }
        for(i=0;i<4;i++) constants[2][i]=(bits&(1u<<i))?1:0;
        cna_words_push(&w,vertex?CNA_VS(3,0):CNA_PS(3,0));
        for(c=0;c<4;c++)
        {
            cna_words_push(&w,CNA_OP(CNA_DEF,5));cna_words_push(&w,CNA_DST(CNA_CONST,c,15));
            for(i=0;i<4;i++) push_float(&w,constants[c][i]);
        }
        if(vertex)
        {
            cna_words_push(&w,CNA_OP(CNA_DCL_OP,2));cna_words_push(&w,CNA_DCL(0,0));cna_words_push(&w,CNA_DST(CNA_OUTPUT,0,15));
            cna_words_push(&w,CNA_OP(CNA_MOV,2));cna_words_push(&w,CNA_DST(CNA_OUTPUT,0,15));cna_words_push(&w,CNA_SRC(CNA_CONST,0));
        }
        cna_words_push(&w,CNA_OP(94,3)|(1u<<16));cna_words_push(&w,CNA_DST(19,0,15));
        cna_words_push(&w,CNA_SRC(CNA_CONST,2));cna_words_push(&w,CNA_SRC(CNA_CONST,3));
        if(mode!=4)
        {
            cna_words_push(&w,CNA_OP(vertex?46:CNA_MOV,2));cna_words_push(&w,CNA_DST(destination,0,mode==3?1:15));
            cna_words_push(&w,CNA_SRC(CNA_CONST,0));
        }
        cna_words_push(&w,CNA_OP(opcode,mode==1 || mode==4?4:3)|(1u<<28)|(mode==4?1u<<16:0));
        cna_words_push(&w,CNA_DST(destination,0,mask)|(mode==2?1u<<20:0));
        cna_words_push(&w,CNA_SRC(mode==1?CNA_TEMP:CNA_CONST,mode==1?0:1));
        if(mode==1 || mode==4) cna_words_push(&w,CNA_SRC(CNA_CONST,mode==4?0:1));
        cna_words_push(&w,CNA_SRC_SWZ(19,0,selectors[selector])|(negated?13u<<24:0));
        if(mode<=2)
        {
            cna_words_push(&w,CNA_OP(CNA_MOV,2));cna_words_push(&w,CNA_DST(CNA_COLOROUT,0,15));
            cna_words_push(&w,CNA_SRC(CNA_TEMP,0));
        }
        cna_words_push(&w,CNA_END);
        for(i=0;i<4;i++)
        {
            float old=mode==4?constants[2][i]:mode==5?roundf(constants[0][i]):constants[0][i];
            float next=mode==4?1:mode==5?roundf(constants[1][i]):mode==1?old+constants[1][i]:constants[1][i];
            int selected=((bits>>((selectors[selector]>>(2*i))&3))&1)^negated;
            if(mode==2) next=next<0?0:next>1?1:next;
            expected[i]=selected && (mask&(1u<<i))?next:old;
        }
        p=cna_parse(profiles[profile],w.at,w.count*4);
        CNA_CHECK(cna_report_errors("predicated write",p)==0,"legal predicated destination accepted");
        if(p && !p->error_count)
        {
            program_result(p,-1,expected,mode==4?2:mode==5?3:0,mode==3?1:4);
            CNA_CHECK(cna_spirv_validate(p,"predicated_write")==1,"valid predicated-write module");
        }
        MOJOSHADER_freeParseData(p);cna_words_free(&w);
    }
}

static void shifted_results(void)
{
    static const char *profiles[]={"spirv","glspirv"};
    static const unsigned int shifts[]={0,1,2,3,13,14,15},masks[]={15,7,8};
    static const float scales[]={1,2,4,8,.125f,.25f,.5f};
    int profile,minor,shift,mask_index,add,saturate;
    /* Microsoft, Modifiers for ps_1_X:
     * https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/dx9-graphics-reference-asm-ps-instructions-modifiers-ps-1-x
     * Saturation follows any multiply/divide modifier. Only ps_1_4 adds x8/d4/d8.
     * A final unmodified multiply by 1/4 keeps a wrong >1 result observable on a colour target. */
    for(profile=0;profile<2;profile++) for(minor=1;minor<=4;minor++) for(shift=0;shift<7;shift++)
        for(mask_index=0;mask_index<3;mask_index++) for(add=0;add<2;add++) for(saturate=0;saturate<2;saturate++)
    {
        CnaWords w={0};const MOJOSHADER_parseData *p;
        float constants[3][4]={{-.125f,.375f,.5f,.75f},{.75f,.5f,-.5f,.125f},{.25f,.25f,.25f,.25f}};
        float expected[4];int c,i;
        if(minor<4 && (shifts[shift]==3 || shifts[shift]==13 || shifts[shift]==14)) continue;
        if(!saturate)
        {
            const float small[]={.0625f,.125f,-.0625f,.03125f};
            memcpy(constants[1],small,sizeof(small));
        }
        cna_words_push(&w,CNA_PS(1,minor));
        for(c=0;c<3;c++)
        {
            cna_words_push(&w,CNA_OP(CNA_DEF,0));cna_words_push(&w,CNA_DST(CNA_CONST,c,15));
            for(i=0;i<4;i++) push_float(&w,constants[c][i]);
        }
        /* x4 establishes old lanes outside [0,1] without out-of-range DEF constants.
         * A later masked saturation must leave these lanes untouched. */
        cna_words_push(&w,CNA_OP(CNA_MOV,0));cna_words_push(&w,CNA_DST(CNA_TEMP,0,15)|(2u<<24));
        cna_words_push(&w,CNA_SRC(CNA_CONST,0));
        cna_words_push(&w,CNA_OP(add?CNA_ADD:CNA_MOV,0));
        cna_words_push(&w,CNA_DST(CNA_TEMP,0,masks[mask_index])|(shifts[shift]<<24)|(saturate?1u<<20:0));
        cna_words_push(&w,CNA_SRC(CNA_CONST,1));
        if(add) cna_words_push(&w,CNA_SRC(CNA_CONST,1));
        cna_words_push(&w,CNA_OP(CNA_MUL,0));cna_words_push(&w,CNA_DST(CNA_TEMP,0,15));
        cna_words_push(&w,CNA_SRC(CNA_TEMP,0));cna_words_push(&w,CNA_SRC(CNA_CONST,2));
        cna_words_push(&w,CNA_END);
        for(i=0;i<4;i++)
        {
            float result=constants[1][i]*(add?2:1)*scales[shift];
            if(saturate) result=result<0?0:result>1?1:result;
            expected[i]=((masks[mask_index]&(1u<<i))?result:constants[0][i]*4)*.25f;
        }
        p=cna_parse(profiles[profile],w.at,w.count*4);
        CNA_CHECK(cna_report_errors("shifted result",p)==0,"legal ps_1_x result modifiers accepted");
        if(p && !p->error_count)
        {
            program_result(p,-1,expected,0,4);
            CNA_CHECK(cna_spirv_validate(p,"shifted_result")==1,"valid shifted-result module");
        }
        MOJOSHADER_freeParseData(p);cna_words_free(&w);
    }
}

int main(void)
{
    cna_test_run("predicated_kill_truth_table",predicated_kill);
    cna_test_run("predicated_destination_values",predicated_writes);
    cna_test_run("shifted_saturated_results",shifted_results);
    return cna_test_finish();
}
