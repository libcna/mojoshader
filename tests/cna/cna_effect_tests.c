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

/* Wraps a base block as an effect file with one parameter of the given type/value offsets and no
 * objects (object_count lets the object-reference checks be exercised). Returns the file length. */
static unsigned int one_param_effect(unsigned char *out, const Image *base,
                                     uint32_t typeoffset, uint32_t valoffset,
                                     uint32_t object_count)
{
    Image file;
    memset(&file, '\0', sizeof (file));
    put_u32(&file, 0xFEFF0901);          /* Effect 9.1 */
    put_u32(&file, base->len);           /* offset from base to the counts */
    memcpy(file.bytes + file.len, base->bytes, base->len);
    file.len += base->len;
    put_u32(&file, 1);                   /* parameters */
    put_u32(&file, 0);                   /* techniques */
    put_u32(&file, 0);
    put_u32(&file, object_count);        /* objects */
    put_u32(&file, typeoffset);          /* parameter: type offset */
    put_u32(&file, valoffset);           /* value offset */
    put_u32(&file, 0);                   /* flags */
    put_u32(&file, 0);                   /* annotations */
    put_u32(&file, 0);                   /* small objects */
    put_u32(&file, 0);                   /* large objects */
    memcpy(out, file.bytes, file.len);
    return file.len;
} // one_param_effect

/* Compiles an exact-size copy of an effect image and runs it through the error-effect checks:
 * malformed content must come back as a deletable, non-cloning error, never a crash. */
static void expect_rejected(const unsigned char *bytes, unsigned int len, const char *what)
{
    unsigned char *copy = (unsigned char *) cna_exact_copy(bytes, len);
    MOJOSHADER_effectShaderContext ctx = backend();
    exercise_error_effect(MOJOSHADER_compileEffect(copy, len, NULL, 0, NULL, 0, &ctx), what);
    free(copy);
} // expect_rejected

/* A-04: a parameter type offset past the base region used to read through a fabricated window. */
static void test_value_type_offset_out_of_range(void)
{
    unsigned char bytes[1024];
    Image base;
    uint32_t empty, nm, type;
    unsigned int len;
    memset(&base, '\0', sizeof (base));
    empty = base.len; put_u32(&base, 0);
    nm = put_string(&base, "P");
    type = base.len;
    put_u32(&base, MOJOSHADER_SYMTYPE_FLOAT); put_u32(&base, MOJOSHADER_SYMCLASS_SCALAR);
    put_u32(&base, nm); put_u32(&base, empty); put_u32(&base, 0); put_u32(&base, 1); put_u32(&base, 1);
    put_f32(&base, 0.5f);
    (void) type;
    len = one_param_effect(bytes, &base, 0x7FFFFF00u, 0, 0);
    expect_rejected(bytes, len, "value type offset out of range");
} // test_value_type_offset_out_of_range

/* A-04: a value offset past the base region used to drive the scalar default-value copy. */
static void test_value_value_offset_out_of_range(void)
{
    unsigned char bytes[1024];
    Image base;
    uint32_t empty, nm, type;
    unsigned int len;
    memset(&base, '\0', sizeof (base));
    empty = base.len; put_u32(&base, 0);
    nm = put_string(&base, "P");
    type = base.len;
    put_u32(&base, MOJOSHADER_SYMTYPE_FLOAT); put_u32(&base, MOJOSHADER_SYMCLASS_VECTOR);
    put_u32(&base, nm); put_u32(&base, empty); put_u32(&base, 0); put_u32(&base, 4); put_u32(&base, 1);
    len = one_param_effect(bytes, &base, type, 0x7FFFFF00u, 0);
    expect_rejected(bytes, len, "value value offset out of range");
} // test_value_value_offset_out_of_range

/* A-04: an object-class value wrote objects[valuesI[i]].type with no bound on the index. */
static void test_value_object_index_out_of_range(void)
{
    unsigned char bytes[1024];
    Image base;
    uint32_t empty, nm, type, val;
    unsigned int len;
    memset(&base, '\0', sizeof (base));
    empty = base.len; put_u32(&base, 0);
    nm = put_string(&base, "T");
    type = base.len;
    put_u32(&base, MOJOSHADER_SYMTYPE_TEXTURE); put_u32(&base, MOJOSHADER_SYMCLASS_OBJECT);
    put_u32(&base, nm); put_u32(&base, empty); put_u32(&base, 0);  /* elements 0 -> one object */
    val = base.len; put_u32(&base, 0x7FFFFFFFu);                   /* object index, far out of range */
    len = one_param_effect(bytes, &base, type, val, 1);           /* file declares one object */
    expect_rejected(bytes, len, "value object index out of range");
} // test_value_object_index_out_of_range

/* A-04: a struct member count larger than the type metadata it indexes read past the base. */
static void test_value_struct_member_count_out_of_range(void)
{
    unsigned char bytes[1024];
    Image base;
    uint32_t empty, nm, type;
    unsigned int len;
    memset(&base, '\0', sizeof (base));
    empty = base.len; put_u32(&base, 0);
    nm = put_string(&base, "S");
    type = base.len;
    put_u32(&base, MOJOSHADER_SYMTYPE_VOID); put_u32(&base, MOJOSHADER_SYMCLASS_STRUCT);
    put_u32(&base, nm); put_u32(&base, empty); put_u32(&base, 0);
    put_u32(&base, 100);                                           /* members, but none follow */
    len = one_param_effect(bytes, &base, type, 0, 0);
    expect_rejected(bytes, len, "value struct member count out of range");
} // test_value_struct_member_count_out_of_range

/* A valid effect with one float4 parameter "Scale", one technique/pass, and a pass state of type
 * PIXELSHADERCONSTANT whose large-object block carries a preshader -- so loading it runs the
 * preshader reader and cloning it runs copypreshader. Returns the file length. */
static unsigned int preshader_effect(unsigned char *out)
{
    Image base, file;
    CnaWords pre;
    uint32_t empty, pname, techname, passname, ptype, pval, stype, sval;
    unsigned int prelen;

    memset(&pre, '\0', sizeof (pre));
    cna_push_preshader(&pre, "Scale", NULL);
    prelen = (unsigned int) (pre.count * 4);

    memset(&base, '\0', sizeof (base));
    empty = base.len; put_u32(&base, 0);
    pname = put_string(&base, "Scale");
    techname = put_string(&base, "T");
    passname = put_string(&base, "P");
    ptype = base.len;
    put_u32(&base, MOJOSHADER_SYMTYPE_FLOAT); put_u32(&base, MOJOSHADER_SYMCLASS_VECTOR);
    put_u32(&base, pname); put_u32(&base, empty); put_u32(&base, 0); put_u32(&base, 4); put_u32(&base, 1);
    pval = base.len; put_u32(&base, 0); put_u32(&base, 0); put_u32(&base, 0); put_u32(&base, 0);
    stype = base.len;
    put_u32(&base, MOJOSHADER_SYMTYPE_FLOAT); put_u32(&base, MOJOSHADER_SYMCLASS_VECTOR);
    put_u32(&base, empty); put_u32(&base, empty); put_u32(&base, 0); put_u32(&base, 4); put_u32(&base, 1);
    sval = base.len; put_u32(&base, 0); put_u32(&base, 0); put_u32(&base, 0); put_u32(&base, 0);

    memset(&file, '\0', sizeof (file));
    put_u32(&file, 0xFEFF0901);
    put_u32(&file, base.len);
    memcpy(file.bytes + file.len, base.bytes, base.len);
    file.len += base.len;
    put_u32(&file, 1);                   /* parameters */
    put_u32(&file, 1);                   /* techniques */
    put_u32(&file, 0);
    put_u32(&file, 0);                   /* objects */
    put_u32(&file, ptype); put_u32(&file, pval); put_u32(&file, 0); put_u32(&file, 0);
    put_u32(&file, techname); put_u32(&file, 0); put_u32(&file, 1);      /* technique */
    put_u32(&file, passname); put_u32(&file, 0); put_u32(&file, 1);      /* pass */
    put_u32(&file, 160); put_u32(&file, 0); put_u32(&file, stype); put_u32(&file, sval);  /* state */
    put_u32(&file, 0);                   /* small objects */
    put_u32(&file, 1);                   /* large objects */
    /* large object: technique 0, pass 0, FIXME, state 0, type 0, length; then the preshader */
    put_u32(&file, 0); put_u32(&file, 0); put_u32(&file, 0); put_u32(&file, 0); put_u32(&file, 0);
    put_u32(&file, prelen);
    memcpy(file.bytes + file.len, pre.at, prelen);
    file.len += prelen;

    memcpy(out, file.bytes, file.len);
    cna_words_free(&pre);
    return file.len;
} // preshader_effect

static MOJOSHADER_effectShaderContext counting_backend(void)
{
    MOJOSHADER_effectShaderContext ctx = backend();
    ctx.m = cna_alloc_malloc;
    ctx.f = cna_alloc_free;
    return ctx;
} // counting_backend

/* A-06: failing each allocation of a valid effect's load in turn must return a deletable result
 * and free everything -- never crash on a half-built array or leak. */
static void inject_load(const unsigned char *bytes, unsigned int len, const char *what)
{
    unsigned char *copy = (unsigned char *) cna_exact_copy(bytes, len);
    int fail_at;
    int got_full_run = 0;
    for (fail_at = 1; fail_at <= 512; fail_at++)
    {
        MOJOSHADER_effectShaderContext ctx = counting_backend();
        const MOJOSHADER_effect *e;
        cna_alloc_reset(fail_at);
        e = MOJOSHADER_compileEffect(copy, len, NULL, 0, NULL, 0, &ctx);
        MOJOSHADER_deleteEffect(e);
        CNA_CHECK(cna_alloc_live() == 0, what);
        if (!cna_alloc_hit())
        {
            got_full_run = 1;
            break;
        } // if
    } // for
    CNA_CHECK(got_full_run, what);
    free(copy);
} // inject_load

/* A-06: the same for cloning a cleanly loaded effect -- copyvalue, the symbol/type copies and
 * copypreshader must each fail cleanly. */
static void inject_clone(const unsigned char *bytes, unsigned int len, const char *what)
{
    unsigned char *copy = (unsigned char *) cna_exact_copy(bytes, len);
    MOJOSHADER_effectShaderContext ctx = counting_backend();
    const MOJOSHADER_effect *e;
    int base_live;
    int fail_at;
    int got_full_run = 0;

    cna_alloc_reset(0);
    e = MOJOSHADER_compileEffect(copy, len, NULL, 0, NULL, 0, &ctx);
    CNA_CHECK((e != NULL) && (e->error_count == 0), "the clone source loads cleanly");
    base_live = cna_alloc_live();

    for (fail_at = 1; fail_at <= 512; fail_at++)
    {
        MOJOSHADER_effect *clone;
        cna_alloc_arm(cna_alloc_attempts() + fail_at);
        clone = MOJOSHADER_cloneEffect(e);
        if (clone != NULL)
            MOJOSHADER_deleteEffect(clone);
        CNA_CHECK(cna_alloc_live() == base_live, what);
        if (!cna_alloc_hit())
        {
            got_full_run = 1;
            break;
        } // if
    } // for
    CNA_CHECK(got_full_run, what);

    MOJOSHADER_deleteEffect(e);
    CNA_CHECK(cna_alloc_live() == 0, "the source frees with nothing left over");
    free(copy);
} // inject_clone

static void test_effect_load_allocation_failures(void)
{
    unsigned char bytes[2048];
    unsigned int len;
    len = struct_effect(bytes);
    inject_load(bytes, len, "a struct effect load frees everything under allocation failure");
    len = preshader_effect(bytes);
    inject_load(bytes, len, "a preshader effect load frees everything under allocation failure");
} // test_effect_load_allocation_failures

static void test_effect_clone_allocation_failures(void)
{
    /* The preshader effect carries a named parameter (COPY_STRING, copyvalue), a preshader
     * symbol (copysymbol/copysymbolinfo) and a pass-state preshader (copypreshader), and has a
     * technique so clone reaches its object table. struct_effect has no technique, which
     * MOJOSHADER_cloneEffect does not support (an unrelated pre-existing assert), so it is loaded
     * above but not cloned here. */
    unsigned char bytes[2048];
    unsigned int len = preshader_effect(bytes);
    inject_clone(bytes, len, "a preshader effect clone frees everything under allocation failure");
} // test_effect_clone_allocation_failures

/* A-04: the XNA4 wrapper read a second version token after skipping its header without checking
 * four bytes remained. */
static void test_xna4_wrapper_truncated_after_skip(void)
{
    unsigned char bytes[16];
    unsigned int len = 0;
    Image img;
    memset(&img, '\0', sizeof (img));
    put_u32(&img, 0xBCF00BCFu);   /* the XNA4 wrapper magic (0xBCF0, 0x0B, 0xCF) */
    put_u32(&img, 10);            /* skip = 10 - 8 = 2 bytes, leaving under four behind */
    put_u32(&img, 0);             /* the two skipped bytes plus padding */
    memcpy(bytes, img.bytes, img.len);
    len = img.len;
    expect_rejected(bytes, len, "truncated XNA4 wrapper");
} // test_xna4_wrapper_truncated_after_skip

int main(void)
{
    cna_test_run("struct_member_defaults", test_struct_member_defaults);
    cna_test_run("error_effects_are_deletable", test_error_effects_are_deletable);
    cna_test_run("value_type_offset_out_of_range", test_value_type_offset_out_of_range);
    cna_test_run("value_value_offset_out_of_range", test_value_value_offset_out_of_range);
    cna_test_run("value_object_index_out_of_range", test_value_object_index_out_of_range);
    cna_test_run("value_struct_member_count_out_of_range", test_value_struct_member_count_out_of_range);
    cna_test_run("xna4_wrapper_truncated_after_skip", test_xna4_wrapper_truncated_after_skip);
    cna_test_run("effect_load_allocation_failures", test_effect_load_allocation_failures);
    cna_test_run("effect_clone_allocation_failures", test_effect_clone_allocation_failures);
    return cna_test_finish();
} // main
