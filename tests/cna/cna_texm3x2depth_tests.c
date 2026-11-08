/* SPDX-License-Identifier: Zlib */
/* Microsoft D3D9 texm3x2depth---ps and source-registers-modifiers-signed-scale:
 * z=dot(stage m UVW, source RGB), w=dot(stage m+1 UVW, source RGB),
 * depth=w==0 ? 1 : z/w. Each operand's _bx2 applies when that instruction runs.
 * No texture is read by PAD/DEPTH; t(m+1) is consumed. Mixed modifiers follow
 * from the per-instruction modifier contract (no same-modifier restriction).
 * https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/texm3x2depth---ps
 * https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/dx9-graphics-reference-asm-ps-registers-modifiers-signed-scale */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "cna_test.h"
#include "spirv/spirv.h"

static size_t shader(uint32_t *w, int source, int pad, int padmod, int depthmod)
{
    const uint32_t code[] = {
        CNA_PS(1,3), CNA_DEF, CNA_DST(CNA_CONST,0,15),
        0x3e800000,0x3f000000,0x3f400000,0x3f800000,
        CNA_TEX,CNA_DST(CNA_TEXTURE,source,15),
        71,CNA_DST(CNA_TEXTURE,pad,15),CNA_SRC(CNA_TEXTURE,source)|((uint32_t)padmod<<24),
        84,CNA_DST(CNA_TEXTURE,pad+1,15),CNA_SRC(CNA_TEXTURE,source)|((uint32_t)depthmod<<24),
        CNA_MOV,CNA_DST(CNA_TEMP,0,15),CNA_SRC(CNA_CONST,0),CNA_END
    };
    memcpy(w,code,sizeof(code)); return sizeof(code)/4;
}

/* Execute standard straight-line SPIR-V, with literal input/sample values.
 * Undefined lanes stay unknown. Expectations are fractions, never compiler output. */
#define IDS 4096
typedef struct Value { float v[4]; unsigned known; int width; } Value;
static void evaluate(const MOJOSHADER_parseData *p, int source, int pad,
                     int padmod, int depthmod, int variant)
{
    static Value val[IDS], mem[IDS];
    static int widths[IDS], stage[IDS], binding[IDS], image[IDS], storage[IDS], location[IDS];
    const uint32_t *words=(const uint32_t *)p->output;
    size_t at,n=cna_spirv_module_words(p);
    unsigned depth=0,color=0; int i,samples=0,stores=0,mode=0,interface=0;
    /* Base: (32/255)/(4*96/255)=1/12. Signed: (-191/255)/(-252/255)=191/252.
     * Mixed: reverse numerator row to keep both results in [0,1]:
     * PAD-only signed => 191/384; DEPTH-only signed => 32/252=8/63.
     * Out-of-range variant is an IR non-clamping control only: D3D depth is undefined. */
    static const float expected[]={1.0f/12,191.0f/384,8.0f/63,191.0f/252};
    float want=variant==1 || variant==2 ? 1 : variant==3 ? 4.0f/3 : expected[(padmod?1:0)|(depthmod?2:0)];
    CNA_CHECK(words[3]<IDS,"depth numeric ID bound"); if(words[3]>=IDS)return;
    memset(val,0,sizeof(val));memset(mem,0,sizeof(mem));memset(widths,0,sizeof(widths));
    for(i=0;i<IDS;i++)stage[i]=binding[i]=image[i]=storage[i]=location[i]=-1;
    for(at=5;at<n && words[at]>>16;at+=words[at]>>16)
    {
        const uint32_t *d=words+at; unsigned op=d[0]&65535;
        if(op==SpvOpName)
        {
            int s;
            if(sscanf((const char *)(d+2),"ps_t%d",&s)==1 && s>=0 && s<4)stage[d[1]]=s;
        }
        if(op==SpvOpDecorate && d[2]==SpvDecorationBinding)binding[d[1]]=d[3];
        if(op==SpvOpDecorate && d[2]==SpvDecorationLocation)location[d[1]]=d[3];
        if(op==SpvOpDecorate && d[2]==SpvDecorationBuiltIn && d[3]==SpvBuiltInFragDepth)depth=d[1];
        if(op==SpvOpExecutionMode && d[2]==SpvExecutionModeDepthReplacing)mode++;
        if(op==SpvOpTypeFloat || op==SpvOpTypeBool)widths[d[1]]=1;
        if(op==SpvOpTypeVector)widths[d[1]]=d[3];
        if(op==SpvOpVariable)
        {
            storage[d[2]]=d[3];
            if(d[3]==SpvStorageClassInput && stage[d[2]]>=0)
            {
                Value *v=&mem[d[2]]; int s=stage[d[2]];
                v->known=15;v->width=4;v->v[0]=.5f;v->v[1]=.5f;v->v[3]=9;
                if(s==pad) {v->v[0]=variant==3?16:padmod!=depthmod?-1:1;v->v[1]=0;}
                if(s==pad+1) {v->v[0]=0;v->v[1]=variant==1?0:variant==2?-0.0f:4;}
            }
        }
        if(op==SpvOpConstant)
        {memcpy(val[d[2]].v,d+3,4);val[d[2]].known=1;val[d[2]].width=1;}
        if(op==SpvOpConstantComposite || op==SpvOpCompositeConstruct)
        {
            Value *v=&val[d[2]];v->width=widths[d[1]];
            for(i=0;i<v->width;i++) {v->v[i]=val[d[3+i]].v[0];if(val[d[3+i]].known&1)v->known|=1u<<i;}
        }
        if(op==SpvOpLoad) {val[d[2]]=mem[d[3]];val[d[2]].width=widths[d[1]];image[d[2]]=binding[d[3]];}
        if(op==SpvOpStore) {mem[d[1]]=val[d[2]];if(d[1]==depth && depth)stores++;}
        if(op==SpvOpCopyObject) {val[d[2]]=val[d[3]];image[d[2]]=image[d[3]];}
        if(op==SpvOpVectorShuffle)
        {
            Value *v=&val[d[2]],*a=&val[d[3]],*b=&val[d[4]];v->width=widths[d[1]];
            for(i=0;i<v->width;i++)
            {
                unsigned lane=d[5+i];Value *from=a;
                if(lane>=(unsigned)a->width){lane-=a->width;from=b;}
                CNA_CHECK(lane<4,"depth shuffle lane bound");if(lane>=4)continue;
                v->v[i]=from->v[lane];if(from->known&(1u<<lane))v->known|=1u<<i;
            }
        }
        if(op==SpvOpCompositeExtract)
        {val[d[2]].v[0]=val[d[3]].v[d[4]];val[d[2]].known=(val[d[3]].known>>d[4])&1;val[d[2]].width=1;}
        if(op==SpvOpFAdd || op==SpvOpFSub || op==SpvOpFMul || op==SpvOpFDiv || op==SpvOpFOrdEqual)
        {
            Value *v=&val[d[2]],*a=&val[d[3]],*b=&val[d[4]];v->width=widths[d[1]];v->known=a->known&b->known;
            for(i=0;i<v->width;i++)v->v[i]=op==SpvOpFAdd?a->v[i]+b->v[i]:op==SpvOpFSub?a->v[i]-b->v[i]:
                op==SpvOpFMul?a->v[i]*b->v[i]:op==SpvOpFOrdEqual?a->v[i]==b->v[i]:b->v[i]!=0?a->v[i]/b->v[i]:NAN;
        }
        if(op==SpvOpDot)
        {
            Value *v=&val[d[2]],*a=&val[d[3]],*b=&val[d[4]];v->width=1;v->v[0]=0;
            v->known=a->width==3 && b->width==3 && (a->known&b->known&7)==7;
            for(i=0;i<a->width && i<4;i++)v->v[0]+=a->v[i]*b->v[i];
        }
        if(op==SpvOpSelect)val[d[2]]=val[val[d[3]].v[0]?d[4]:d[5]];
        if(op==SpvOpImageSampleImplicitLod)
        {
            static const float sample[]={32.0f/255,96.0f/255,160.0f/255,224.0f/255};
            CNA_CHECK(image[d[3]]==source,"only source-stage texture sampled");samples++;
            memcpy(val[d[2]].v,sample,sizeof(sample));val[d[2]].known=15;val[d[2]].width=4;
        }
    }
    for(at=5;at<n && words[at]>>16;at+=words[at]>>16)
        if((words[at]&65535)==SpvOpEntryPoint)
        {
            size_t j=at+3+(strlen((const char *)(words+at+3))+4)/4;
            for(;j<at+(words[at]>>16);j++)if(words[j]==depth)interface=1;
        }
    for(i=1;i<IDS;i++)if(storage[i]==SpvStorageClassOutput && location[i]==0)color=i;
    CNA_CHECK(depth && storage[depth]==SpvStorageClassOutput && mode==1 && interface && stores==1,
              "one implicit FragDepth output, interface, DepthReplacing mode and write");
    CNA_CHECK(depth && mem[depth].known==1,"depth arithmetic is defined");
    if(!depth || mem[depth].known!=1 || fabsf(mem[depth].v[0]-want)>0.000001f || !isfinite(mem[depth].v[0]))
    {fprintf(stderr,"depth=%g expected=%g padmod=%d depthmod=%d variant=%d\n",mem[depth].v[0],want,padmod,depthmod,variant);
     CNA_CHECK(0,"literal TEXM3X2DEPTH fraction or zero-divisor one");}
    CNA_CHECK(samples==1 && p->sampler_count==1,"PAD and DEPTH add no sampler or sample");
    CNA_CHECK(color && mem[color].known==15,"color output is defined independently of consumed destination");
    for(i=0;i<4;i++)CNA_CHECK(mem[color].v[i]==(i+1)*.25f,"depth emission preserves literal color output");
}

static void legal_tokens(void)
{
    int pad,source,a,b,cases=0;
    for(pad=1;pad<=2;pad++)for(source=0;source<pad;source++)for(a=0;a<=4;a+=4)for(b=0;b<=4;b+=4)
    {
        uint32_t w[32];size_t n=shader(w,source,pad,a,b);
        const MOJOSHADER_parseData *p=cna_parse("d3d",w,n*4);
        CNA_CHECK(cna_report_errors("D3D depth legal control",p)==0,"legal stages and independent pair modifiers");
        MOJOSHADER_freeParseData(p);cases++;
    }
    CNA_CHECK(cases==12,"all legal ps_1_3 stages and pair modifiers");
}
static void numeric(int variant)
{
    static const char *profiles[]={"spirv","glspirv"};int profile,pad,source,a,b;
    for(profile=0;profile<2;profile++)for(pad=1;pad<=2;pad++)for(source=0;source<pad;source++)
    for(a=0;a<=4;a+=4)for(b=0;b<=4;b+=4)
    {
        uint32_t w[32];size_t n=shader(w,source,pad,a,b);const MOJOSHADER_parseData *p;
        if(variant==3 && (a||b))continue;
        p=cna_parse(profiles[profile],w,n*4);
        CNA_CHECK(cna_report_errors("SPIR-V depth numeric",p)==0,"TEXM3X2DEPTH is implemented");
        if(p && !p->error_count){evaluate(p,source,pad,a,b,variant);CNA_CHECK(cna_spirv_validate(p,"texm3x2depth")==1,"depth module validates");}
        MOJOSHADER_freeParseData(p);
    }
}
static void fractions(void){numeric(0);}
static void zero(void){numeric(1);numeric(2);}
static void out_of_range(void){numeric(3);}
static void refuses(const uint32_t *w,size_t n,const char *why)
{
    static const char *profiles[]={"d3d","spirv","glspirv","glsl120","glsles3"};size_t j;int i;
    for(j=0;j<sizeof(profiles)/sizeof(profiles[0]);j++)
    {
        const MOJOSHADER_parseData *p=cna_parse(profiles[j],w,n*4);int found=0;
        if(p)for(i=0;i<p->error_count;i++)if(strstr(p->errors[i].error,why))found=1;
        CNA_CHECK(found,"illegal depth form keeps specific validation refusal");
        if(!found){fprintf(stderr,"missing refusal %s for %s\n",why,profiles[j]);cna_report_errors("unexpected errors",p);}
        MOJOSHADER_freeParseData(p);
    }
}
static void illegal_tokens(void)
{
    uint32_t w[32];size_t n;int minor,member,mod;
    for(minor=0;minor<=4;minor++){if(minor==3)continue;n=shader(w,0,1,0,0);w[0]=CNA_PS(1,minor);if(minor==4){w[7]=CNA_NOP;w[8]=CNA_NOP;}refuses(w,n,"only available in Shader Model 1.3");}
    for(minor=2;minor<=3;minor++)
    {
        uint32_t newer[]={CNA_PS(minor,0),CNA_OP(84,2),CNA_DST(CNA_TEXTURE,2,15),CNA_SRC(CNA_TEXTURE,0),CNA_END};
        refuses(newer,sizeof(newer)/4,"only available in Shader Model 1.3");
    }
    n=shader(w,0,1,0,0);memmove(w+9,w+12,(n-12)*4);refuses(w,n-3,"without matching TEXM3X2PAD");
    n=shader(w,0,1,0,0);w[13]=CNA_DST(CNA_TEXTURE,3,15);refuses(w,n,"destination does not follow");
    n=shader(w,0,2,0,0);w[14]=CNA_SRC(CNA_TEXTURE,1);refuses(w,n,"source differs");
    n=shader(w,0,1,0,0);w[10]=CNA_DST(CNA_TEXTURE,0,15);refuses(w,n,"higher register than source");
    for(member=0;member<2;member++)
    {
        int dst=member?13:10,src=dst+1;
        n=shader(w,0,1,0,0);w[dst]=CNA_DST(CNA_TEMP,1,15);refuses(w,n,"destination must be a texture register");
        n=shader(w,0,1,0,0);w[src]=CNA_SRC(CNA_CONST,0);refuses(w,n,"source must be a texture register");
        n=shader(w,0,1,0,0);w[dst]&=~0x80000u;refuses(w,n,"texture destination mask must be .rgba");
        n=shader(w,0,1,0,0);w[dst]|=0x100000u;refuses(w,n,"result modifiers");
        n=shader(w,0,1,0,0);w[dst]|=0x1000000u;refuses(w,n,"result shifts");
        n=shader(w,0,1,0,0);w[src]=CNA_SRC_SWZ(CNA_TEXTURE,0,0xff);refuses(w,n,"must not be swizzled");
        for(mod=1;mod<=6;mod++){if(mod==4)continue;n=shader(w,0,1,0,0);w[src]|=(uint32_t)mod<<24;refuses(w,n,"only permits the _bx2 modifier");}
    }
}
static void consumed(void)
{
    uint32_t w[32];size_t n=shader(w,0,1,0,0);
    w[17]=CNA_SRC(CNA_TEXTURE,2);refuses(w,n,"consumed by TEXM3X2DEPTH");
    n=shader(w,0,1,0,0);w[18]=65;w[19]=CNA_DST(CNA_TEXTURE,2,15);w[20]=CNA_END;
    refuses(w,21,"consumed by TEXM3X2DEPTH"); /* TEXKILL has a syntactic destination but reads it. */
}
static void glsl_pair(void)
{
    static const char *profiles[]={"glsl120","glsles3"};int profile,a,b;
    for(profile=0;profile<2;profile++)for(a=0;a<=4;a+=4)for(b=0;b<=4;b+=4)
    {
        uint32_t w[32];size_t n=shader(w,0,1,a,b);
        const MOJOSHADER_parseData *p=cna_parse(profiles[profile],w,n*4);
        CNA_CHECK(cna_report_errors("GLSL depth pair",p)==0,"GLSL depth pair translates");
        if(p && !p->error_count)
        {
            const char *first=strstr(p->output,"float texm3x2pad1 = dot(");
            const char *second=strstr(p->output,"float texm3x2denominator2 = dot(");
            const char *signed_first=first?strstr(first," * 2.0"):NULL;
            const char *signed_second=second?strstr(second," * 2.0"):NULL;
            CNA_CHECK(first && second && first<second,"PAD captures its own dot before the final row");
            CNA_CHECK((signed_first && signed_first<second)==(a!=0),"PAD applies only its own signed modifier");
            CNA_CHECK((signed_second!=NULL)==(b!=0),"depth row applies only its own signed modifier");
            CNA_CHECK(cna_output_contains(p,"(texm3x2denominator2 == 0.0) ? 1.0 : (texm3x2pad1 / texm3x2denominator2)"),
                      "GLSL writes zero-guarded captured numerator over denominator");
            /* Optional source export for independent glslang/IR evaluation, never a golden. */
            if(getenv("MOJOSHADER_CNA_TEXM_DUMP"))
            {
                char name[1024];FILE *f;
                snprintf(name,sizeof(name),"%s/%s-%d-%d.frag",getenv("MOJOSHADER_CNA_TEXM_DUMP"),profiles[profile],a,b);
                f=fopen(name,"wb");CNA_CHECK(f!=NULL,"GLSL source export opens");
                if(f){fwrite(p->output,1,p->output_len,f);fclose(f);}
            }
        }
        MOJOSHADER_freeParseData(p);
    }
}
int main(void)
{
    cna_test_run("texm3x2depth_legal_d3d_tokens",legal_tokens);
    cna_test_run("texm3x2depth_illegal_tokens",illegal_tokens);
    cna_test_run("texm3x2depth_consumed_destination",consumed);
    cna_test_run("texm3x2depth_literal_fractions",fractions);
    cna_test_run("texm3x2depth_zero_denominator",zero);
    cna_test_run("texm3x2depth_out_of_range_ir_only",out_of_range);
    cna_test_run("texm3x2depth_glsl_pair_capture",glsl_pair);
    return cna_test_finish();
}
