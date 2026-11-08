/* SPDX-License-Identifier: Zlib */
/* Effect Framework 9.1 fixture: generic CTAB samplers, typed effect texture parameters.
 * Shader and texture-name payloads deliberately appear in either order. No private map. */
#ifndef CNA_SAMPLER_INFERENCE_EFFECT_H
#define CNA_SAMPLER_INFERENCE_EFFECT_H
#include <stdint.h>
#include <string.h>
typedef struct CnaSamplerEffect { uint32_t words[2048]; unsigned int length; } CnaSamplerEffect;
typedef struct CnaFxBytes { unsigned char bytes[4096]; unsigned int length; } CnaFxBytes;
static void cna_fx_word(CnaFxBytes *b, uint32_t v)
{
    int i; for (i=0;i<4;i++) b->bytes[b->length++]=(unsigned char)(v>>(i*8));
}
static uint32_t cna_fx_string(CnaFxBytes *b, const char *s, int prefix)
{
    uint32_t at=b->length, n=(uint32_t)strlen(s)+1;
    if(prefix) cna_fx_word(b,n);
    memcpy(b->bytes+b->length,s,n); b->length+=n;
    while(b->length%4) b->bytes[b->length++]=0;
    return at;
}
static uint32_t cna_fx_type(CnaFxBytes *b, uint32_t type, uint32_t name)
{
    uint32_t at=b->length;
    cna_fx_word(b,type); cna_fx_word(b,MOJOSHADER_SYMCLASS_OBJECT);
    cna_fx_word(b,name); cna_fx_word(b,0); cna_fx_word(b,0);
    return at;
}
static void cna_fx_blob(CnaFxBytes *b, uint32_t index, const void *data, uint32_t length)
{
    cna_fx_word(b,index); cna_fx_word(b,length);
    memcpy(b->bytes+b->length,data,length); b->length+=length;
    while(b->length%4) b->bytes[b->length++]=0;
}
/* kind is a MOJOSHADER_SYMTYPE_TEXTURE*. large places the pixel shader in the large table.
 * reverse bit 1 adds a pass sampler binding; bit 3 omits CTAB entirely.
 * late emits a DEF with comment-looking immediates before any real CTAB. */
static void cna_sampler_effect(CnaSamplerEffect *out, uint32_t kind, int reverse, int large, int late)
{
    CnaFxBytes b={{0},0}, c={{0},0}, ps={{0},0}, f={{0},0};
    uint32_t names[4], types[6], vals[6], states[2], i, structure, passType, passValue;
    const int assignment=(reverse & 2)!=0, stripped=(reverse & 8)!=0;
    reverse &= 1;
    uint32_t vs[]={0xFFFE0200,0x0200001F,0x80000000,0x900F0000,
        0x0200001F,0x8000000A,0x900F0001,0x02000001,0xC00F0000,0x90E40000,
        0x02000001,0xE00F0000,0x90E40001,0x02000001,0xE00F0001,0x90E40001,0xFFFF};
    const char *strings[]={"SourceTexture","LookupTexture","SourceSampler","LookupSampler"};
    cna_fx_word(&b,0);
    for(i=0;i<4;i++) names[i]=cna_fx_string(&b,strings[i],1);
    types[0]=cna_fx_type(&b,MOJOSHADER_SYMTYPE_TEXTURE2D,names[0]);
    types[1]=cna_fx_type(&b,kind,names[1]);
    types[2]=cna_fx_type(&b,MOJOSHADER_SYMTYPE_SAMPLER,names[2]);
    types[3]=cna_fx_type(&b,MOJOSHADER_SYMTYPE_SAMPLER,names[3]);
    types[4]=cna_fx_type(&b,MOJOSHADER_SYMTYPE_PIXELSHADER,0);
    types[5]=cna_fx_type(&b,MOJOSHADER_SYMTYPE_VERTEXSHADER,0);
    for(i=0;i<2;i++) { vals[i]=b.length; cna_fx_word(&b,i+1); }
    /* Texture state deliberately uses generic TEXTURE even though the parameter is typed. */
    states[0]=cna_fx_type(&b,MOJOSHADER_SYMTYPE_TEXTURE,0);
    for(i=2;i<4;i++)
    {
        vals[i]=b.length; cna_fx_word(&b,1); cna_fx_word(&b,MOJOSHADER_SAMP_TEXTURE);
        cna_fx_word(&b,0); cna_fx_word(&b,states[0]); cna_fx_word(&b,vals[i-2]);
    }
    vals[4]=b.length; cna_fx_word(&b,3);
    vals[5]=b.length; cna_fx_word(&b,4);
    passType=cna_fx_type(&b,MOJOSHADER_SYMTYPE_SAMPLER,0);
    passValue=b.length;cna_fx_word(&b,0);
    structure=b.length;
    cna_fx_word(&b,4); cna_fx_word(&b,1); cna_fx_word(&b,0); cna_fx_word(&b,5);
    for(i=0;i<4;i++)
    { cna_fx_word(&b,types[i]); cna_fx_word(&b,vals[i]); cna_fx_word(&b,0); cna_fx_word(&b,0); }
    cna_fx_word(&b,0); cna_fx_word(&b,0); cna_fx_word(&b,1); /* technique */
    cna_fx_word(&b,0); cna_fx_word(&b,0); cna_fx_word(&b,assignment?3:2); /* pass */
    for(i=4;i<6;i++)
    {
        cna_fx_word(&b,i==4?MOJOSHADER_RS_PIXELSHADER:MOJOSHADER_RS_VERTEXSHADER);
        cna_fx_word(&b,0); cna_fx_word(&b,types[i]); cna_fx_word(&b,vals[i]);
    }
    if(assignment)
    {
        cna_fx_word(&b,MOJOSHADER_RS_SETSAMPLER);cna_fx_word(&b,1);
        cna_fx_word(&b,passType);cna_fx_word(&b,passValue);
    }
    /* D3DXSHADER_CONSTANTTABLE + two D3DXSHADER_CONSTANTINFO entries. */
    cna_fx_word(&c,28); cna_fx_word(&c,0); cna_fx_word(&c,0xFFFF0102);
    cna_fx_word(&c,2); cna_fx_word(&c,28); cna_fx_word(&c,0); cna_fx_word(&c,0);
    for(i=0;i<2;i++)
    {
        cna_fx_word(&c,i?100:84); cna_fx_word(&c,3|(i<<16)); cna_fx_word(&c,1);
        cna_fx_word(&c,68); cna_fx_word(&c,0);
    }
    cna_fx_word(&c,MOJOSHADER_SYMCLASS_OBJECT|(MOJOSHADER_SYMTYPE_SAMPLER<<16));
    cna_fx_word(&c,0x00010001); cna_fx_word(&c,1); cna_fx_word(&c,0);
    cna_fx_string(&c,strings[2],0); cna_fx_string(&c,strings[3],0);
    cna_fx_word(&ps,0xFFFF0102);
    if(late)
    {
        cna_fx_word(&ps,81); cna_fx_word(&ps,0xA00F0000);
        cna_fx_word(&ps,0x0001FFFE); cna_fx_word(&ps,0x42415443);
        cna_fx_word(&ps,0); cna_fx_word(&ps,0);
    }
    if(!stripped)
    {
        cna_fx_word(&ps,((c.length/4+1)<<16)|0xFFFE); cna_fx_word(&ps,0x42415443);
        memcpy(ps.bytes+ps.length,c.bytes,c.length); ps.length+=c.length;
    }
    cna_fx_word(&ps,66); cna_fx_word(&ps,0xB00F0000); /* tex t0 */
    cna_fx_word(&ps,82); cna_fx_word(&ps,0xB00F0001); cna_fx_word(&ps,0xB0E40000);
    cna_fx_word(&ps,1); cna_fx_word(&ps,0x800F0000); cna_fx_word(&ps,0xB0E40001);
    cna_fx_word(&ps,0xFFFF);
    cna_fx_word(&b,large?3:4); cna_fx_word(&b,(large?1:0)+(assignment?1:0));
    if(reverse)
    { if(!large)cna_fx_blob(&b,3,ps.bytes,ps.length); cna_fx_blob(&b,4,vs,sizeof(vs)); }
    cna_fx_blob(&b,1,strings[0],(uint32_t)strlen(strings[0])+1);
    cna_fx_blob(&b,2,strings[1],(uint32_t)strlen(strings[1])+1);
    if(!reverse)
    { if(!large)cna_fx_blob(&b,3,ps.bytes,ps.length); cna_fx_blob(&b,4,vs,sizeof(vs)); }
    if(large)
    {
        cna_fx_word(&b,0); cna_fx_word(&b,0); cna_fx_word(&b,0); cna_fx_word(&b,0);
        cna_fx_word(&b,0); cna_fx_word(&b,ps.length);
        memcpy(b.bytes+b.length,ps.bytes,ps.length); b.length+=ps.length;
    }
    if(assignment)
    {
        cna_fx_word(&b,0);cna_fx_word(&b,0);cna_fx_word(&b,0);cna_fx_word(&b,2);
        cna_fx_word(&b,1);cna_fx_word(&b,(uint32_t)strlen(strings[3])+1);
        memcpy(b.bytes+b.length,strings[3],strlen(strings[3])+1);b.length+=(uint32_t)strlen(strings[3])+1;
        while(b.length%4)b.bytes[b.length++]=0;
    }
    cna_fx_word(&f,0xFEFF0901); cna_fx_word(&f,structure);
    memcpy(f.bytes+f.length,b.bytes,b.length); f.length+=b.length;
    memcpy(out->words,f.bytes,f.length); out->length=f.length;
}
#endif
