/* SPDX-License-Identifier: Zlib */
/** Duplicate Effect payload ownership regressions (FNA3D-2/6).
 * This file is part of the libcna fork of MojoShader; see tests/cna/README.md. */
#include <string.h>
#include "cna_test.h"

typedef struct Image { unsigned char bytes[4096]; unsigned int len; } Image;
static void word(Image *b, uint32_t v)
{
    int i;
    for (i = 0; i < 4; i++) b->bytes[b->len++] = (unsigned char) (v >> (8 * i));
}
static uint32_t text(Image *b, const char *s)
{
    uint32_t at = b->len;
    unsigned int n = (unsigned int) strlen(s) + 1;
    word(b, n); memcpy(b->bytes + b->len, s, n); b->len += n;
    while (b->len & 3) b->bytes[b->len++] = 0;
    return at;
}
static uint32_t object_type(Image *b, uint32_t type, uint32_t name)
{
    uint32_t at = b->len;
    word(b, type); word(b, MOJOSHADER_SYMCLASS_OBJECT);
    word(b, name); word(b, 0); word(b, 0);
    return at;
}
static const uint32_t program[] = {
    CNA_PS(1, 1), CNA_DEF, CNA_DST(CNA_CONST, 0, 15),
    0, 0x3f800000, 0, 0x3f800000,
    CNA_MOV, CNA_DST(CNA_TEMP, 0, 15), CNA_SRC(CNA_CONST, 0), CNA_END
};

/* Hand-laid Effect 9.1 table. References to the same slot are legal; a second payload is
 * malformed. Two parameters and two pass states may reference each slot independently. */
static void image(Image *file, uint32_t type, int small, int large, int duplicate, int empty)
{
    Image base = {{0}, 0};
    uint32_t name, pt[2], pv[2], shader_type, sv[2];
    int i;
    word(&base, 0); name = text(&base, "Object");
    for (i = 0; i < 2; i++)
    {
        pt[i] = object_type(&base, type, name);
        if (type == MOJOSHADER_SYMTYPE_SAMPLER2D)
        {
            uint32_t tt = object_type(&base, MOJOSHADER_SYMTYPE_TEXTURE, name);
            uint32_t tv = base.len; word(&base, i);
            pv[i] = base.len;
            word(&base, 1); word(&base, MOJOSHADER_SAMP_TEXTURE); word(&base, 0);
            word(&base, tt); word(&base, tv);
        }
        else { pv[i] = base.len; word(&base, i); }
    }
    shader_type = object_type(&base, MOJOSHADER_SYMTYPE_PIXELSHADER, 0);
    for (i = 0; i < 2; i++) { sv[i] = base.len; word(&base, i); }
    memset(file, 0, sizeof (*file));
    word(file, 0xfeff0901); word(file, base.len);
    memcpy(file->bytes + file->len, base.bytes, base.len); file->len += base.len;
    word(file, 2); word(file, large ? 1 : 0); word(file, 0); word(file, 2);
    for (i = 0; i < 2; i++)
    { word(file, pt[i]); word(file, pv[i]); word(file, 0); word(file, 0); }
    if (large)
    {
        word(file, 0); word(file, 0); word(file, 1); /* technique */
        word(file, 0); word(file, 0); word(file, 2); /* pass */
        for (i = 0; i < 2; i++)
        {
            word(file, MOJOSHADER_RS_PIXELSHADER); word(file, 0);
            word(file, shader_type); word(file, sv[i]);
        }
    }
    word(file, small); word(file, large);
    for (i = 0; i < small; i++)
    {
        unsigned int j;
        const int slot = duplicate ? 0 : i;
        const unsigned int size = empty ? 0 :
            (type == MOJOSHADER_SYMTYPE_PIXELSHADER ? sizeof (program) : 4);
        word(file, slot); word(file, size);
        if (size && type == MOJOSHADER_SYMTYPE_PIXELSHADER)
            for (j = 0; j < sizeof (program) / 4; j++) word(file, program[j]);
        else if (size) word(file, slot ? 0x006f7774 : 0x00656e6f); /* two / one */
    }
    for (i = 0; i < large; i++)
    {
        unsigned int j;
        word(file, 0); word(file, 0); word(file, 0);
        word(file, duplicate ? 0 : i + small); word(file, 0); word(file, sizeof (program));
        for (j = 0; j < sizeof (program) / 4; j++) word(file, program[j]);
    }
}

static int compiled, deleted;
static void *MOJOSHADERCALL compile_shader(const void *ctx, const char *mainfn,
    const unsigned char *tokens, unsigned int bytes, const MOJOSHADER_swizzle *swiz,
    unsigned int swizcount, const MOJOSHADER_samplerMap *smap, unsigned int smapcount)
{
    const MOJOSHADER_parseData *pd;
    (void) ctx;
    pd = MOJOSHADER_parse("bytecode", mainfn, tokens, bytes, swiz, swizcount, smap, smapcount,
                         cna_alloc_malloc, cna_alloc_free, NULL);
    if (!pd || pd->error_count)
    { MOJOSHADER_freeParseData(pd); return NULL; }
    compiled++;
    return (void *) pd;
}
static void MOJOSHADERCALL delete_shader(const void *ctx, void *shader)
{
    (void) ctx;
    if (shader) { deleted++; MOJOSHADER_freeParseData((MOJOSHADER_parseData *) shader); }
}
static MOJOSHADER_parseData *MOJOSHADERCALL parse_data(void *shader)
{ return (MOJOSHADER_parseData *) shader; }
static const char *MOJOSHADERCALL error(const void *ctx)
{ (void) ctx; return "test shader allocation/parse failed"; }
static int exercise(uint32_t type, int small, int large, int duplicate, int empty, int fail_at)
{
    Image bytes;
    MOJOSHADER_effectShaderContext ctx;
    MOJOSHADER_effect *effect;
    int attempts;
    image(&bytes, type, small, large, duplicate, empty);
    memset(&ctx, 0, sizeof (ctx));
    ctx.compileShader = compile_shader; ctx.deleteShader = delete_shader;
    ctx.getParseData = parse_data; ctx.getError = error;
    ctx.m = cna_alloc_malloc; ctx.f = cna_alloc_free;
    cna_alloc_reset(fail_at); compiled = deleted = 0;
    effect = MOJOSHADER_compileEffect(bytes.bytes, bytes.len, NULL, 0, NULL, 0, &ctx);
    CNA_CHECK(effect != NULL, "a deletable Effect result is returned");
    if (effect && !fail_at)
    {
        if (duplicate)
        {
            CNA_CHECK(effect->error_count == 1, "one duplicate payload error");
            if (effect->error_count == 1)
                CNA_CHECK(strcmp(effect->errors[0].error, "Effect object payload is duplicated.") == 0,
                          "the error identifies duplicate payloads");
            if (type == MOJOSHADER_SYMTYPE_PIXELSHADER)
                CNA_CHECK(compiled == 1, "the duplicate is rejected before compiling again");
        }
        else
        {
            CNA_CHECK(effect->error_count == 0, "distinct payloads and shared references are valid");
            if (type == MOJOSHADER_SYMTYPE_PIXELSHADER)
                CNA_CHECK(compiled == small + large, "every distinct shader is compiled");
            if (!effect->error_count && type == MOJOSHADER_SYMTYPE_STRING)
            {
                CNA_CHECK(strcmp(effect->objects[0].string.string, "one") == 0, "slot zero text");
                CNA_CHECK(strcmp(effect->objects[1].string.string, "two") == 0, "slot one text");
            }
            if (!effect->error_count && type == MOJOSHADER_SYMTYPE_SAMPLER2D)
            {
                CNA_CHECK(strcmp(effect->objects[0].mapping.name, "one") == 0, "slot zero mapping");
                CNA_CHECK(strcmp(effect->objects[1].mapping.name, "two") == 0, "slot one mapping");
            }
        }
    }
    MOJOSHADER_deleteEffect(effect);
    /* C ownership oracle: every successful allocation and shader acquisition is released once.
     * LSan independently reports the overwritten parseData/string on the unfixed implementation. */
    CNA_CHECK(compiled == deleted, "every compiled shader is deleted exactly once");
    CNA_CHECK(cna_alloc_live() == 0, "all effect and shader allocations are released");
    if (fail_at) CNA_CHECK(cna_alloc_hit(), "the selected allocation failure was exercised");
    attempts = cna_alloc_attempts();
    return attempts;
}
static void small_strings(void) { exercise(MOJOSHADER_SYMTYPE_STRING, 2, 0, 1, 0, 0); }
static void small_mappings(void) { exercise(MOJOSHADER_SYMTYPE_SAMPLER2D, 2, 0, 1, 0, 0); }
static void small_shaders(void) { exercise(MOJOSHADER_SYMTYPE_PIXELSHADER, 2, 0, 1, 0, 0); }
static void large_shaders(void) { exercise(MOJOSHADER_SYMTYPE_PIXELSHADER, 0, 2, 1, 0, 0); }
static void cross_table(void) { exercise(MOJOSHADER_SYMTYPE_PIXELSHADER, 1, 1, 1, 0, 0); }
static void empty_record(void) { exercise(MOJOSHADER_SYMTYPE_STRING, 2, 0, 1, 1, 0); }
static void distinct_controls(void)
{
    exercise(MOJOSHADER_SYMTYPE_STRING, 2, 0, 0, 0, 0);
    exercise(MOJOSHADER_SYMTYPE_SAMPLER2D, 2, 0, 0, 0, 0);
    exercise(MOJOSHADER_SYMTYPE_PIXELSHADER, 2, 0, 0, 0, 0);
    exercise(MOJOSHADER_SYMTYPE_PIXELSHADER, 0, 2, 0, 0, 0);
    exercise(MOJOSHADER_SYMTYPE_PIXELSHADER, 1, 1, 0, 0, 0);
}
static void allocation_failures(void)
{
    int i, n = exercise(MOJOSHADER_SYMTYPE_PIXELSHADER, 0, 2, 0, 0, 0);
    for (i = 1; i <= n; i++) exercise(MOJOSHADER_SYMTYPE_PIXELSHADER, 0, 2, 0, 0, i);
}
int main(void)
{
    cna_test_run("duplicate_small_strings", small_strings);
    cna_test_run("duplicate_small_mappings", small_mappings);
    cna_test_run("duplicate_small_shaders", small_shaders);
    cna_test_run("duplicate_large_shaders", large_shaders);
    cna_test_run("duplicate_cross_table_shader", cross_table);
    cna_test_run("duplicate_empty_record", empty_record);
    cna_test_run("distinct_object_controls", distinct_controls);
    cna_test_run("duplicate_guard_allocation_failures", allocation_failures);
    return cna_test_finish();
}
