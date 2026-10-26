/**
 * Effect Framework parser regressions.
 *
 * This file is part of the libcna fork of MojoShader; see tests/cna/README.md.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cna_test.h"

/* No test here compiles a shader: an effect without techniques never asks the backend for one. */
static void *MOJOSHADERCALL no_compile(const void *ctx, const char *mainfn,
                                      const unsigned char *tokens, const unsigned int bufsize,
                                      const MOJOSHADER_swizzle *swiz, const unsigned int swizcount,
                                      const MOJOSHADER_samplerMap *smap,
                                      const unsigned int smapcount)
{
    (void) ctx; (void) mainfn; (void) tokens; (void) bufsize;
    (void) swiz; (void) swizcount; (void) smap; (void) smapcount;
    return NULL;
} // no_compile

static void MOJOSHADERCALL no_delete(const void *ctx, void *shader)
{
    (void) ctx; (void) shader;
} // no_delete

static MOJOSHADER_parseData *MOJOSHADERCALL no_parse_data(void *shader)
{
    (void) shader;
    return NULL;
} // no_parse_data

static const char *MOJOSHADERCALL no_error(const void *ctx)
{
    (void) ctx;
    return "no shader backend in this test";
} // no_error

static void *MOJOSHADERCALL test_malloc(int bytes, void *data)
{
    (void) data;
    return malloc((size_t) bytes);
} // test_malloc

static MOJOSHADER_effectShaderContext backend(void)
{
    MOJOSHADER_effectShaderContext ctx;
    memset(&ctx, '\0', sizeof (ctx));
    ctx.compileShader = no_compile;
    ctx.deleteShader = no_delete;
    ctx.getParseData = no_parse_data;
    ctx.getError = no_error;
    return ctx;
} // backend

/* Little-endian words and strings into a byte image, as fxc lays an effect out. */
typedef struct Image
{
    unsigned char bytes[1024];
    unsigned int len;
} Image;

static void put_u32(Image *image, uint32_t value)
{
    image->bytes[image->len++] = (unsigned char) (value & 0xFF);
    image->bytes[image->len++] = (unsigned char) ((value >> 8) & 0xFF);
    image->bytes[image->len++] = (unsigned char) ((value >> 16) & 0xFF);
    image->bytes[image->len++] = (unsigned char) ((value >> 24) & 0xFF);
} // put_u32

static void put_f32(Image *image, float value)
{
    uint32_t bits;
    memcpy(&bits, &value, sizeof (bits));
    put_u32(image, bits);
} // put_f32

/* A length-prefixed, NUL-terminated, 4-byte-padded string; returns its offset. */
static uint32_t put_string(Image *image, const char *text)
{
    const uint32_t offset = image->len;
    const uint32_t len = (uint32_t) strlen(text) + 1;
    put_u32(image, len);
    memcpy(image->bytes + image->len, text, len);
    image->len += len;
    while (image->len % 4)
        image->bytes[image->len++] = 0;
    return offset;
} // put_string

/*
 * A valid effect with one parameter and nothing else:
 *
 *     struct Light { float A; float3 B; float C[2]; };
 *     Light Lighting = { 0.5, { 0.6, 0.7, 0.8 }, { 0.9, 1.0 } };
 *
 * A and B are not arrays (elements == 0), C is. fxc stores struct defaults tightly packed, one
 * member row after another, directly behind the member descriptions.
 */
static unsigned int struct_effect(unsigned char *out)
{
    Image base;
    Image file;
    uint32_t empty, lighting, a, b, c, type;

    memset(&base, '\0', sizeof (base));
    empty = base.len; put_u32(&base, 0);
    lighting = put_string(&base, "Lighting");
    a = put_string(&base, "A");
    b = put_string(&base, "B");
    c = put_string(&base, "C");

    type = base.len;
    put_u32(&base, MOJOSHADER_SYMTYPE_VOID);
    put_u32(&base, MOJOSHADER_SYMCLASS_STRUCT);
    put_u32(&base, lighting);
    put_u32(&base, empty);           /* semantic */
    put_u32(&base, 0);               /* elements: not an array of structs */
    put_u32(&base, 3);               /* members */
    /* member: type, class, name, semantic, elements, columns, rows */
    put_u32(&base, MOJOSHADER_SYMTYPE_FLOAT); put_u32(&base, MOJOSHADER_SYMCLASS_SCALAR);
    put_u32(&base, a); put_u32(&base, empty); put_u32(&base, 0); put_u32(&base, 1); put_u32(&base, 1);
    put_u32(&base, MOJOSHADER_SYMTYPE_FLOAT); put_u32(&base, MOJOSHADER_SYMCLASS_VECTOR);
    put_u32(&base, b); put_u32(&base, empty); put_u32(&base, 0); put_u32(&base, 3); put_u32(&base, 1);
    put_u32(&base, MOJOSHADER_SYMTYPE_FLOAT); put_u32(&base, MOJOSHADER_SYMCLASS_SCALAR);
    put_u32(&base, c); put_u32(&base, empty); put_u32(&base, 2); put_u32(&base, 1); put_u32(&base, 1);
    put_f32(&base, 0.5f);
    put_f32(&base, 0.6f); put_f32(&base, 0.7f); put_f32(&base, 0.8f);
    put_f32(&base, 0.9f); put_f32(&base, 1.0f);

    memset(&file, '\0', sizeof (file));
    put_u32(&file, 0xFEFF0901);      /* Effect 9.1 */
    put_u32(&file, base.len);        /* offset from base to the parameter table */
    memcpy(file.bytes + file.len, base.bytes, base.len);
    file.len += base.len;
    put_u32(&file, 1);               /* parameters */
    put_u32(&file, 0);               /* techniques */
    put_u32(&file, 0);
    put_u32(&file, 0);               /* objects */
    put_u32(&file, type);            /* parameter: type offset */
    put_u32(&file, 0);               /* value offset (struct defaults follow the type) */
    put_u32(&file, 0);               /* flags */
    put_u32(&file, 0);               /* annotations */
    put_u32(&file, 0);               /* small objects */
    put_u32(&file, 0);               /* large objects */

    memcpy(out, file.bytes, file.len);
    return file.len;
} // struct_effect

/* A-09: every member keeps its own default, in its own padded register. */
static void test_struct_member_defaults(void)
{
    static const float expected[16] = {
        0.5f, 0.0f, 0.0f, 0.0f,
        0.6f, 0.7f, 0.8f, 0.0f,
        0.9f, 0.0f, 0.0f, 0.0f,
        1.0f, 0.0f, 0.0f, 0.0f,
    };
    unsigned char bytes[1024];
    const unsigned int len = struct_effect(bytes);
    MOJOSHADER_effectShaderContext ctx = backend();
    const MOJOSHADER_effect *effect = MOJOSHADER_compileEffect(bytes, len, NULL, 0, NULL, 0, &ctx);
    int i;

    CNA_CHECK(effect != NULL, "an effect comes back");
    if (effect == NULL)
        return;
    CNA_CHECK(effect->error_count == 0, "the effect parses without errors");
    CNA_CHECK(effect->param_count == 1, "one parameter");
    if ((effect->error_count == 0) && (effect->param_count == 1))
    {
        const MOJOSHADER_effectValue *value = &effect->params[0].value;
        CNA_CHECK(value->name && strcmp(value->name, "Lighting") == 0, "the struct's name");
        CNA_CHECK(value->type.member_count == 3, "three members");
        CNA_CHECK(value->value_count == 16, "four padded registers");
        if (value->value_count == 16)
            for (i = 0; i < 16; i++)
            {
                if (value->valuesF[i] != expected[i])
                    fprintf(stderr, "  value[%d] = %.9g, expected %.9g\n", i,
                            value->valuesF[i], expected[i]);
                CNA_CHECK(value->valuesF[i] == expected[i], "struct member default");
            } // for
    } // if
    MOJOSHADER_deleteEffect(effect);
} // test_struct_member_defaults

static void exercise_error_effect(const MOJOSHADER_effect *effect, const char *what)
{
    CNA_CHECK(effect != NULL, what);
    if (effect == NULL)
        return;
    CNA_CHECK(effect->error_count == 1, what);
    CNA_CHECK(MOJOSHADER_cloneEffect(effect) == NULL, "an error effect does not clone");
    MOJOSHADER_deleteEffect(effect);  /* used to call its NULL free function */
    MOJOSHADER_deleteEffect(effect);  /* and it stays harmless */
} // exercise_error_effect

/* A-07: each error effect MOJOSHADER_compileEffect returns can be deleted (and cloned). */
static void test_error_effects_are_deletable(void)
{
    static const unsigned char short_file[4] = { 0x01, 0x09, 0xFF, 0xFE };
    static const unsigned char not_an_effect[12] = { 0x00, 0x03, 0xFE, 0xFF, 0, 0, 0, 0, 0, 0, 0, 0 };
    MOJOSHADER_effectShaderContext ctx = backend();
    MOJOSHADER_effectShaderContext half_allocator = backend();
    half_allocator.m = test_malloc;

    exercise_error_effect(MOJOSHADER_compileEffect(short_file, sizeof (short_file),
                                                   NULL, 0, NULL, 0, NULL),
                          "no backend");
    exercise_error_effect(MOJOSHADER_compileEffect(short_file, sizeof (short_file),
                                                   NULL, 0, NULL, 0, &ctx),
                          "unexpected end of file");
    exercise_error_effect(MOJOSHADER_compileEffect(not_an_effect, sizeof (not_an_effect),
                                                   NULL, 0, NULL, 0, &ctx),
                          "not an effect");
    exercise_error_effect(MOJOSHADER_compileEffect(short_file, sizeof (short_file),
                                                   NULL, 0, NULL, 0, &half_allocator),
                          "allocator without a free function");
} // test_error_effects_are_deletable

int main(void)
{
    cna_test_run("struct_member_defaults", test_struct_member_defaults);
    cna_test_run("error_effects_are_deletable", test_error_effects_are_deletable);
    return cna_test_finish();
} // main
