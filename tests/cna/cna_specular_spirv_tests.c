/* SPDX-License-Identifier: Zlib */
/* Numeric reflection contracts for TEXM3X3SPEC/VSPEC in the libcna fork. */
#include <math.h>
#include <string.h>
#include "cna_test.h"
#include "spirv/spirv.h"

typedef struct Value { float v[4]; int width; } Value;

static const uint32_t *definition(const MOJOSHADER_parseData *p, uint32_t id)
{
    const uint32_t *w=(const uint32_t *)p->output;
    size_t at, n=cna_spirv_module_words(p);
    for(at=5; at<n && (w[at]>>16); at+=w[at]>>16)
    {
        unsigned int op=w[at]&0xffff;
        if((op==SpvOpConstant || op==SpvOpCompositeConstruct || op==SpvOpDot ||
            op==SpvOpFMul || op==SpvOpFDiv || op==SpvOpFAdd || op==SpvOpFSub ||
            op==SpvOpVectorTimesScalar) && w[at+2]==id) return w+at;
    }
    return NULL;
}
static int width(const MOJOSHADER_parseData *p, uint32_t type)
{
    const uint32_t *w=(const uint32_t *)p->output;
    size_t at,n=cna_spirv_module_words(p);
    for(at=5;at<n && (w[at]>>16);at+=w[at]>>16)
    {
        if((w[at]&0xffff)==SpvOpTypeFloat && w[at+1]==type) return 1;
        if((w[at]&0xffff)==SpvOpTypeVector && w[at+1]==type) return w[at+3];
    }
    return 0;
}
/* Interpret the emitted coordinate expression. N and E are independent input vectors;
 * no expected value is obtained from MojoShader, a driver, or CNA's renderer output. */
static Value evaluate(const MOJOSHADER_parseData *p,uint32_t id,uint32_t normal,uint32_t eye,
                      const float n[3],const float e[3],int depth)
{
    Value result={{0},0},a,b;
    const uint32_t *d;
    unsigned int op;
    int i;
    if(id==normal || id==eye)
    { memcpy(result.v,id==normal?n:e,3*sizeof(float)); result.width=3; return result; }
    d=definition(p,id);
    if(!d || depth>32) return result;
    op=d[0]&0xffff; result.width=width(p,d[1]);
    if(result.width<1 || result.width>4) { result.width=0; return result; }
    if(op==SpvOpConstant)
    { memcpy(result.v,d+3,sizeof(float)); return result; }
    if(op==SpvOpCompositeConstruct)
    {
        for(i=0;i<result.width;i++)
        {
            a=evaluate(p,d[3+i],normal,eye,n,e,depth+1);
            if(a.width!=1) { result.width=0; return result; }
            result.v[i]=a.v[0];
        }
        return result;
    }
    a=evaluate(p,d[3],normal,eye,n,e,depth+1);
    b=evaluate(p,d[4],normal,eye,n,e,depth+1);
    if(op==SpvOpDot)
    {
        if(a.width!=3 || b.width!=3 || result.width!=1) { result.width=0; return result; }
        for(i=0;i<3;i++) result.v[0]+=a.v[i]*b.v[i];
        return result;
    }
    if(a.width!=result.width || b.width!=(op==SpvOpVectorTimesScalar?1:result.width))
    { result.width=0; return result; }
    for(i=0;i<result.width;i++)
    {
        float x=a.v[i],y=b.v[op==SpvOpVectorTimesScalar?0:i];
        result.v[i]=op==SpvOpFMul || op==SpvOpVectorTimesScalar?x*y:
                    op==SpvOpFDiv?x/y:op==SpvOpFAdd?x+y:x-y;
    }
    return result;
}
static void reflection_coordinates(const MOJOSHADER_parseData *p)
{
    const uint32_t *w=(const uint32_t *)p->output;
    size_t at,n=cna_spirv_module_words(p);
    uint32_t normal=0,coordinate=0,eye=0;
    int variant,i;
    static const float normals[2][3]={{.1f,1,.1f},{.1f,1,.1f}};
    static const float eyes[2][3]={{1,.2f,.1f},{.1f,.2f,1}};
    /* 2*dot(N,E)/dot(N,N)=2*.31/1.02=31/51. The dominant directions are -X/-Z. */
    static const float expected[2][3]={{-.9392156863f,.4078431373f,-.0392156863f},
                                      {-.0392156863f,.4078431373f,-.9392156863f}};
    for(at=5;at<n && (w[at]>>16);at+=w[at]>>16)
    {
        unsigned int op=w[at]&0xffff;
        if(op==SpvOpCompositeConstruct && (w[at]>>16)==6)
        {
            const uint32_t *x=definition(p,w[at+3]),*y=definition(p,w[at+4]),*z=definition(p,w[at+5]);
            if(x && y && z && (x[0]&0xffff)==SpvOpDot && (y[0]&0xffff)==SpvOpDot &&
               (z[0]&0xffff)==SpvOpDot) normal=w[at+2];
        }
        if(op==SpvOpImageSampleImplicitLod) coordinate=w[at+4];
    }
    {
        const uint32_t *subtract=definition(p,coordinate);
        CNA_CHECK(normal && subtract && (subtract[0]&0xffff)==SpvOpFSub,
                  "sample coordinate is the reflection with the eye ray subtracted");
        if(!normal || !subtract || (subtract[0]&0xffff)!=SpvOpFSub) return;
        eye=subtract[4];
    }
    for(variant=0;variant<2;variant++)
    {
        Value result=evaluate(p,coordinate,normal,eye,normals[variant],eyes[variant],0);
        CNA_CHECK(result.width==3,"all reflected coordinate components are evaluable");
        for(i=0;i<3;i++)
            CNA_CHECK(fabsf(result.v[i]-expected[variant][i])<.000001f,
                      "non-axis reflection uses dot products, including unnormalized N");
    }
}
static void specular(void)
{
    static const char *profiles[]={"spirv","glspirv"};
    size_t profile;
    int varying;
    for(profile=0;profile<2;profile++) for(varying=0;varying<2;varying++)
    {
        CnaWords w={0};
        const MOJOSHADER_parseData *p;
        const uint32_t head[]={CNA_PS(1,3),CNA_DEF,CNA_DST(CNA_CONST,1,15),
            0x3f800000,0x3e4ccccd,0x3dcccccd,0,CNA_TEXCRD,CNA_DST(CNA_TEXTURE,0,15),
            73,CNA_DST(CNA_TEXTURE,1,15),CNA_SRC(CNA_TEXTURE,0),
            73,CNA_DST(CNA_TEXTURE,2,15),CNA_SRC(CNA_TEXTURE,0)};
        size_t i;
        for(i=0;i<sizeof(head)/sizeof(head[0]);i++) cna_words_push(&w,head[i]);
        cna_words_push(&w,varying?77:76); cna_words_push(&w,CNA_DST(CNA_TEXTURE,3,15));
        cna_words_push(&w,CNA_SRC(CNA_TEXTURE,0));
        if(!varying) cna_words_push(&w,CNA_SRC(CNA_CONST,1));
        cna_words_push(&w,CNA_MOV);cna_words_push(&w,CNA_DST(CNA_TEMP,0,15));
        cna_words_push(&w,CNA_SRC(CNA_TEXTURE,3));cna_words_push(&w,CNA_END);
        p=cna_parse(profiles[profile],w.at,w.count*4);
        CNA_CHECK(cna_report_errors("legacy specular",p)==0,"specular fixture accepted");
        if(p && !p->error_count)
        {
            reflection_coordinates(p);
            CNA_CHECK(cna_spirv_validate(p,"legacy_specular")==1,"valid specular module");
        }
        MOJOSHADER_freeParseData(p);cna_words_free(&w);
    }
}
int main(void)
{
    cna_test_run("non_axis_specular_reflection",specular);
    return cna_test_finish();
}
