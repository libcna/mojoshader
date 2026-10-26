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
    unsigned char bytes[4096];
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

/* One numeric parameter of the given class, columns and rows, with `words` words of default data
 * behind its type -- as many as the claimed shape would copy, so only the shape is wrong. */
static unsigned int shaped_param_effect(unsigned char *out, uint32_t klass, uint32_t columns,
                                        uint32_t rows, unsigned int words)
{
    Image base;
    uint32_t empty, nm, type, val;
    unsigned int i;
    memset(&base, '\0', sizeof (base));
    empty = base.len; put_u32(&base, 0);
    nm = put_string(&base, "P");
    type = base.len;
    put_u32(&base, MOJOSHADER_SYMTYPE_FLOAT); put_u32(&base, klass);
    put_u32(&base, nm); put_u32(&base, empty); put_u32(&base, 0);
    put_u32(&base, columns); put_u32(&base, rows);
    val = base.len;
    for (i = 0; i < words; i++)
        put_f32(&base, 0.5f + (float) i);
    return one_param_effect(out, &base, type, val, 0);
} // shaped_param_effect

/* R2-02: a value's rows are copied into four-float registers, columncount words each; a vector
 * five wide copied 20 bytes into the 16 allocated for it, past the end of the allocation. */
static void test_value_wider_than_a_register(void)
{
    unsigned char bytes[1024];
    unsigned int len = shaped_param_effect(bytes, MOJOSHADER_SYMCLASS_VECTOR, 5, 1, 5);
    expect_rejected(bytes, len, "a vector five columns wide");
    len = shaped_param_effect(bytes, MOJOSHADER_SYMCLASS_MATRIX_ROWS, 8, 2, 16);
    expect_rejected(bytes, len, "a matrix eight columns wide");
} // test_value_wider_than_a_register

/* R2-02: columncount << 2 was computed in 32 bits before being widened, so 2^30 + 1 columns
 * became a four-byte row that passed every bound and loaded with a column count of a billion. */
static void test_value_column_count_wraps(void)
{
    unsigned char bytes[1024];
    const unsigned int len = shaped_param_effect(bytes, MOJOSHADER_SYMCLASS_VECTOR,
                                                 0x40000001u, 1, 1);
    expect_rejected(bytes, len, "a column count that wraps the byte count");
} // test_value_column_count_wraps

/* R2-02: no Direct3D 9 type has more than four rows; the count was taken as it stood. */
static void test_value_taller_than_a_matrix(void)
{
    unsigned char bytes[1024];
    const unsigned int len = shaped_param_effect(bytes, MOJOSHADER_SYMCLASS_MATRIX_ROWS, 4, 5, 20);
    expect_rejected(bytes, len, "a matrix five rows tall");
} // test_value_taller_than_a_matrix

/* R2-02: the same for a struct member: each member row is copied into its own register, and a
 * member eight floats wide wrote 32 bytes into the struct's last 16. */
static void test_value_struct_member_wider_than_a_register(void)
{
    unsigned char bytes[1024];
    Image base;
    uint32_t empty, nm, mname, type;
    unsigned int len, i;
    memset(&base, '\0', sizeof (base));
    empty = base.len; put_u32(&base, 0);
    nm = put_string(&base, "S");
    mname = put_string(&base, "M");
    type = base.len;
    put_u32(&base, MOJOSHADER_SYMTYPE_VOID); put_u32(&base, MOJOSHADER_SYMCLASS_STRUCT);
    put_u32(&base, nm); put_u32(&base, empty); put_u32(&base, 0);
    put_u32(&base, 1);                                             /* one member */
    put_u32(&base, MOJOSHADER_SYMTYPE_FLOAT); put_u32(&base, MOJOSHADER_SYMCLASS_VECTOR);
    put_u32(&base, mname); put_u32(&base, empty); put_u32(&base, 0);
    put_u32(&base, 8); put_u32(&base, 1);                          /* eight columns, one row */
    for (i = 0; i < 8; i++)                                        /* its eight defaults */
        put_f32(&base, 0.25f * (float) i);
    len = one_param_effect(bytes, &base, type, 0, 0);
    expect_rejected(bytes, len, "a struct member eight columns wide");
} // test_value_struct_member_wider_than_a_register

/* The smallest record count whose array, sizeof (element) bytes apiece, needs more than 2^32
 * bytes: computed in size_t and kept in 32 bits, its byte count wraps to less than one element. */
static uint32_t wrapping_count(size_t elemsize)
{
    return (uint32_t) ((0x100000000ull + elemsize - 1) / elemsize);
} // wrapping_count

/* One float parameter with the given annotation count, then (when techniques is nonzero) one
 * technique with the given pass count whose first pass has the given state count. Each count's
 * records are followed by one honest record, so only the count is wrong. */
static unsigned int record_count_effect(unsigned char *out, uint32_t annotations,
                                        uint32_t passes, uint32_t states)
{
    Image base, file;
    uint32_t empty, nm, type, val;
    memset(&base, '\0', sizeof (base));
    empty = base.len; put_u32(&base, 0);
    nm = put_string(&base, "P");
    type = base.len;
    put_u32(&base, MOJOSHADER_SYMTYPE_FLOAT); put_u32(&base, MOJOSHADER_SYMCLASS_SCALAR);
    put_u32(&base, nm); put_u32(&base, empty); put_u32(&base, 0); put_u32(&base, 1); put_u32(&base, 1);
    val = base.len; put_f32(&base, 0.5f);

    memset(&file, '\0', sizeof (file));
    put_u32(&file, 0xFEFF0901);
    put_u32(&file, base.len);
    memcpy(file.bytes + file.len, base.bytes, base.len);
    file.len += base.len;
    put_u32(&file, 1);                           /* parameters */
    put_u32(&file, (passes || states) ? 1 : 0);  /* techniques */
    put_u32(&file, 0);
    put_u32(&file, 0);                           /* objects */
    put_u32(&file, type); put_u32(&file, val); put_u32(&file, 0); put_u32(&file, annotations);
    if (annotations)
    {
        put_u32(&file, type); put_u32(&file, val);
    } // if
    if (passes || states)
    {
        put_u32(&file, nm); put_u32(&file, 0); put_u32(&file, passes ? passes : 1);
        put_u32(&file, nm); put_u32(&file, 0); put_u32(&file, states);
        if (states)
        {
            put_u32(&file, 0); put_u32(&file, 0); put_u32(&file, type); put_u32(&file, val);
        } // if
    } // if
    put_u32(&file, 0);                           /* small objects */
    put_u32(&file, 0);                           /* large objects */
    memcpy(out, file.bytes, file.len);
    return file.len;
} // record_count_effect

/* A-04 (found with R2-02): annotation, pass and state counts sized their arrays through a byte
 * count kept in 32 bits, so a count large enough to wrap it allocated less than one record and
 * the loop wrote past it. */
static void test_annotation_count_wraps_its_array(void)
{
    unsigned char bytes[1024];
    const unsigned int len = record_count_effect(
        bytes, wrapping_count(sizeof (MOJOSHADER_effectAnnotation)), 0, 0);
    expect_rejected(bytes, len, "an annotation count that wraps its array size");
} // test_annotation_count_wraps_its_array

static void test_pass_count_wraps_its_array(void)
{
    unsigned char bytes[1024];
    const unsigned int len = record_count_effect(
        bytes, 0, wrapping_count(sizeof (MOJOSHADER_effectPass)), 0);
    expect_rejected(bytes, len, "a pass count that wraps its array size");
} // test_pass_count_wraps_its_array

static void test_state_count_wraps_its_array(void)
{
    unsigned char bytes[1024];
    const unsigned int len = record_count_effect(
        bytes, 0, 0, wrapping_count(sizeof (MOJOSHADER_effectState)));
    expect_rejected(bytes, len, "a state count that wraps its array size");
} // test_state_count_wraps_its_array

/* The control: the same layout with honest counts loads. */
static void test_record_counts_honest(void)
{
    unsigned char bytes[1024];
    const unsigned int len = record_count_effect(bytes, 1, 1, 1);
    MOJOSHADER_effectShaderContext ctx = backend();
    const MOJOSHADER_effect *effect = MOJOSHADER_compileEffect(bytes, len, NULL, 0, NULL, 0, &ctx);
    CNA_CHECK((effect != NULL) && (effect->error_count == 0), "honest record counts load");
    if ((effect != NULL) && (effect->error_count == 0))
    {
        CNA_CHECK(effect->params[0].annotation_count == 1, "one annotation");
        CNA_CHECK(effect->technique_count == 1, "one technique");
        CNA_CHECK(effect->techniques[0].pass_count == 1, "one pass");
        CNA_CHECK(effect->techniques[0].passes[0].state_count == 1, "one state");
    } // if
    MOJOSHADER_deleteEffect(effect);
} // test_record_counts_honest

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

/* Deep comparison of two effects (or of the parts of them a clone copies), reporting the first
 * difference. Pointers differ between a source and its clone; what they hold must not. */
static int same_note(int same, const char *what)
{
    if (!same)
        fprintf(stderr, "  effects differ: %s\n", what);
    return same;
} // same_note

static int same_string(const char *a, const char *b)
{
    if ((a == NULL) || (b == NULL))
        return a == b;
    return strcmp(a, b) == 0;
} // same_string

static int same_bytes(const void *a, const void *b, size_t len)
{
    if (len == 0)
        return 1;
    if ((a == NULL) || (b == NULL))
        return 0;
    return memcmp(a, b, len) == 0;
} // same_bytes

static int same_typeinfo(const MOJOSHADER_symbolTypeInfo *a, const MOJOSHADER_symbolTypeInfo *b)
{
    unsigned int i;
    if ((a->parameter_class != b->parameter_class) || (a->parameter_type != b->parameter_type) ||
        (a->rows != b->rows) || (a->columns != b->columns) || (a->elements != b->elements) ||
        (a->member_count != b->member_count))
        return same_note(0, "type info");
    if (a->member_count == 0)
        return 1;
    if ((a->members == NULL) || (b->members == NULL))
        return same_note(0, "struct member array");
    for (i = 0; i < a->member_count; i++)
    {
        if (!same_string(a->members[i].name, b->members[i].name))
            return same_note(0, "struct member name");
        if (!same_typeinfo(&a->members[i].info, &b->members[i].info))
            return 0;
    } // for
    return 1;
} // same_typeinfo

static int is_sampler_value(const MOJOSHADER_effectValue *v)
{
    return (v->type.parameter_class == MOJOSHADER_SYMCLASS_OBJECT) &&
           (v->type.parameter_type >= MOJOSHADER_SYMTYPE_SAMPLER) &&
           (v->type.parameter_type <= MOJOSHADER_SYMTYPE_SAMPLERCUBE);
} // is_sampler_value

static int same_value(const MOJOSHADER_effectValue *a, const MOJOSHADER_effectValue *b)
{
    unsigned int i;
    if (!same_string(a->name, b->name))
        return same_note(0, "value name");
    if (!same_string(a->semantic, b->semantic))
        return same_note(0, "value semantic");
    if (!same_typeinfo(&a->type, &b->type))
        return 0;
    if (a->value_count != b->value_count)
        return same_note(0, "value count");
    if (!is_sampler_value(a))
        return same_note(same_bytes(a->values, b->values, (size_t) a->value_count * 4), "value data");
    if ((a->value_count > 0) && ((a->values == NULL) || (b->values == NULL)))
        return same_note(0, "sampler state array");
    for (i = 0; i < a->value_count; i++)
    {
        if (a->valuesSS[i].type != b->valuesSS[i].type)
            return same_note(0, "sampler state type");
        if (!same_value(&a->valuesSS[i].value, &b->valuesSS[i].value))
            return 0;
    } // for
    return 1;
} // same_value

static int same_values(const MOJOSHADER_effectValue *a, const MOJOSHADER_effectValue *b,
                       unsigned int count)
{
    unsigned int i;
    if ((count > 0) && ((a == NULL) || (b == NULL)))
        return same_note(0, "annotation array");
    for (i = 0; i < count; i++)
        if (!same_value(&a[i], &b[i]))
            return 0;
    return 1;
} // same_values

static int same_symbols(const MOJOSHADER_symbol *a, const MOJOSHADER_symbol *b, unsigned int count)
{
    unsigned int i;
    if ((count > 0) && ((a == NULL) || (b == NULL)))
        return same_note(0, "symbol array");
    for (i = 0; i < count; i++)
    {
        if (!same_string(a[i].name, b[i].name) || (a[i].register_set != b[i].register_set) ||
            (a[i].register_index != b[i].register_index) ||
            (a[i].register_count != b[i].register_count))
            return same_note(0, "symbol");
        if (!same_typeinfo(&a[i].info, &b[i].info))
            return 0;
    } // for
    return 1;
} // same_symbols

static int same_preshader(const MOJOSHADER_preshader *a, const MOJOSHADER_preshader *b)
{
    unsigned int i, j;
    if ((a == NULL) || (b == NULL))
        return same_note(a == b, "preshader presence");
    if ((a->literal_count != b->literal_count) || (a->temp_count != b->temp_count) ||
        (a->symbol_count != b->symbol_count) || (a->instruction_count != b->instruction_count) ||
        (a->register_count != b->register_count))
        return same_note(0, "preshader counts");
    if (!same_bytes(a->literals, b->literals, a->literal_count * sizeof (double)))
        return same_note(0, "preshader literals");
    if (!same_symbols(a->symbols, b->symbols, a->symbol_count))
        return 0;
    if ((a->instruction_count > 0) && ((a->instructions == NULL) || (b->instructions == NULL)))
        return same_note(0, "preshader instruction array");
    for (i = 0; i < a->instruction_count; i++)
    {
        const MOJOSHADER_preshaderInstruction *ia = &a->instructions[i];
        const MOJOSHADER_preshaderInstruction *ib = &b->instructions[i];
        if ((ia->opcode != ib->opcode) || (ia->element_count != ib->element_count) ||
            (ia->operand_count != ib->operand_count))
            return same_note(0, "preshader instruction");
        for (j = 0; (j < ia->operand_count) && (j < 4); j++)
        {
            const MOJOSHADER_preshaderOperand *oa = &ia->operands[j];
            const MOJOSHADER_preshaderOperand *ob = &ib->operands[j];
            if ((oa->type != ob->type) || (oa->index != ob->index) ||
                (oa->array_register_count != ob->array_register_count) ||
                !same_bytes(oa->array_registers, ob->array_registers,
                            oa->array_register_count * sizeof (unsigned int)))
                return same_note(0, "preshader operand");
        } // for
    } // for
    return same_note(same_bytes(a->registers, b->registers, a->register_count * 16),
                     "preshader registers");
} // same_preshader

static int same_uints(const unsigned int *a, const unsigned int *b, unsigned int count)
{
    return same_bytes(a, b, count * sizeof (unsigned int));
} // same_uints

/* What a clone must reproduce of its source: every parameter, technique, pass, state and object,
 * with the same names, types and data (the current pass is reset by design). */
static int same_effect(const MOJOSHADER_effect *a, const MOJOSHADER_effect *b)
{
    int i;
    unsigned int j, k;
    if ((a->error_count != b->error_count) || (a->param_count != b->param_count) ||
        (a->technique_count != b->technique_count) || (a->object_count != b->object_count))
        return same_note(0, "effect counts");
    for (i = 0; i < a->error_count; i++)
        if (!same_string(a->errors[i].error, b->errors[i].error))
            return same_note(0, "error text");
    for (i = 0; i < a->param_count; i++)
    {
        if (!same_value(&a->params[i].value, &b->params[i].value))
            return 0;
        if (a->params[i].annotation_count != b->params[i].annotation_count)
            return same_note(0, "parameter annotation count");
        if (!same_values(a->params[i].annotations, b->params[i].annotations,
                         a->params[i].annotation_count))
            return 0;
    } // for
    if ((a->current_technique - a->techniques) != (b->current_technique - b->techniques))
        return same_note(0, "current technique");
    for (i = 0; i < a->technique_count; i++)
    {
        const MOJOSHADER_effectTechnique *ta = &a->techniques[i];
        const MOJOSHADER_effectTechnique *tb = &b->techniques[i];
        if (!same_string(ta->name, tb->name) || (ta->pass_count != tb->pass_count) ||
            (ta->annotation_count != tb->annotation_count))
            return same_note(0, "technique");
        if (!same_values(ta->annotations, tb->annotations, ta->annotation_count))
            return 0;
        for (j = 0; j < ta->pass_count; j++)
        {
            const MOJOSHADER_effectPass *pa = &ta->passes[j];
            const MOJOSHADER_effectPass *pb = &tb->passes[j];
            if (!same_string(pa->name, pb->name) || (pa->state_count != pb->state_count) ||
                (pa->annotation_count != pb->annotation_count))
                return same_note(0, "pass");
            if (!same_values(pa->annotations, pb->annotations, pa->annotation_count))
                return 0;
            for (k = 0; k < pa->state_count; k++)
            {
                const MOJOSHADER_effectState *sa = &pa->states[k];
                const MOJOSHADER_effectState *sb = &pb->states[k];
                if ((sa->type != sb->type) || (sa->index != sb->index) ||
                    (sa->referenced_parameter != sb->referenced_parameter) ||
                    (sa->preshader_param_count != sb->preshader_param_count) ||
                    !same_uints(sa->preshader_params, sb->preshader_params,
                                sa->preshader_param_count))
                    return same_note(0, "pass state");
                if (!same_preshader(sa->preshader, sb->preshader))
                    return 0;
                if (!same_value(&sa->value, &sb->value))
                    return 0;
            } // for
        } // for
    } // for
    for (i = 0; i < a->object_count; i++)
    {
        const MOJOSHADER_effectObject *oa = &a->objects[i];
        const MOJOSHADER_effectObject *ob = &b->objects[i];
        if (oa->type != ob->type)
            return same_note(0, "object type");
        if ((oa->type == MOJOSHADER_SYMTYPE_PIXELSHADER) ||
            (oa->type == MOJOSHADER_SYMTYPE_VERTEXSHADER))
        {
            if ((oa->shader.technique != ob->shader.technique) ||
                (oa->shader.pass != ob->shader.pass) ||
                (oa->shader.is_preshader != ob->shader.is_preshader) ||
                (oa->shader.param_count != ob->shader.param_count) ||
                !same_uints(oa->shader.params, ob->shader.params, oa->shader.param_count) ||
                (oa->shader.preshader_param_count != ob->shader.preshader_param_count) ||
                !same_uints(oa->shader.preshader_params, ob->shader.preshader_params,
                            oa->shader.preshader_param_count))
                return same_note(0, "shader object");
            if (oa->shader.is_preshader && !same_preshader(oa->shader.preshader,
                                                           ob->shader.preshader))
                return 0;
        } // if
        else if ((oa->type >= MOJOSHADER_SYMTYPE_SAMPLER) &&
                 (oa->type <= MOJOSHADER_SYMTYPE_SAMPLERCUBE))
        {
            if (!same_string(oa->mapping.name, ob->mapping.name))
                return same_note(0, "sampler object name");
        } // else if
        else if (oa->type == MOJOSHADER_SYMTYPE_STRING)
        {
            if (!same_string(oa->string.string, ob->string.string))
                return same_note(0, "string object");
        } // else if
    } // for
    return 1;
} // same_effect

/*
 * A valid effect that reaches every copy the clone makes: a float4 parameter with a semantic and
 * two annotations, a struct parameter with named members and defaults, a sampler parameter whose
 * states include a texture reference, a string parameter, and a technique and pass that carry
 * annotations, an ordinary render state and a PIXELSHADERCONSTANT state with a preshader. The
 * texture and string are object-table entries with their payloads in small-object blocks.
 */
static unsigned int clone_coverage_effect(unsigned char *out)
{
    Image base, file;
    CnaWords pre;
    uint32_t empty, scale, scalesem, gain, level, light, a, b, samp, tex, str, techname, passname;
    uint32_t mode, depth;
    uint32_t t_scale, v_scale, t_gain, v_gain, t_level, v_level, t_light;
    uint32_t t_samp, v_samp, t_filter, v_filter, t_tex, v_tex, t_str, v_str;
    uint32_t t_mode, v_mode, t_depth, v_depth, t_state, v_state;
    unsigned int prelen;

    memset(&pre, '\0', sizeof (pre));
    cna_push_preshader(&pre, "Scale", NULL);
    prelen = (unsigned int) (pre.count * 4);

    memset(&base, '\0', sizeof (base));
    empty = base.len; put_u32(&base, 0);
    scale = put_string(&base, "Scale");
    scalesem = put_string(&base, "SCALE");
    gain = put_string(&base, "Gain");
    level = put_string(&base, "Level");
    light = put_string(&base, "Light");
    a = put_string(&base, "A");
    b = put_string(&base, "B");
    samp = put_string(&base, "Samp");
    tex = put_string(&base, "Tex");
    str = put_string(&base, "Str");
    techname = put_string(&base, "T");
    passname = put_string(&base, "P");
    mode = put_string(&base, "Mode");
    depth = put_string(&base, "Depth");

    /* float4 Scale : SCALE = { 1, 2, 3, 4 } */
    t_scale = base.len;
    put_u32(&base, MOJOSHADER_SYMTYPE_FLOAT); put_u32(&base, MOJOSHADER_SYMCLASS_VECTOR);
    put_u32(&base, scale); put_u32(&base, scalesem); put_u32(&base, 0); put_u32(&base, 4); put_u32(&base, 1);
    v_scale = base.len; put_f32(&base, 1.0f); put_f32(&base, 2.0f); put_f32(&base, 3.0f); put_f32(&base, 4.0f);
    /* annotations: float Gain = 0.5; int Level = 3; */
    t_gain = base.len;
    put_u32(&base, MOJOSHADER_SYMTYPE_FLOAT); put_u32(&base, MOJOSHADER_SYMCLASS_SCALAR);
    put_u32(&base, gain); put_u32(&base, empty); put_u32(&base, 0); put_u32(&base, 1); put_u32(&base, 1);
    v_gain = base.len; put_f32(&base, 0.5f);
    t_level = base.len;
    put_u32(&base, MOJOSHADER_SYMTYPE_INT); put_u32(&base, MOJOSHADER_SYMCLASS_SCALAR);
    put_u32(&base, level); put_u32(&base, empty); put_u32(&base, 0); put_u32(&base, 1); put_u32(&base, 1);
    v_level = base.len; put_u32(&base, 3);
    /* struct { float A; float2 B; } Light = { 0.25, { 0.75, 1.25 } }; */
    t_light = base.len;
    put_u32(&base, MOJOSHADER_SYMTYPE_VOID); put_u32(&base, MOJOSHADER_SYMCLASS_STRUCT);
    put_u32(&base, light); put_u32(&base, empty); put_u32(&base, 0); put_u32(&base, 2);
    put_u32(&base, MOJOSHADER_SYMTYPE_FLOAT); put_u32(&base, MOJOSHADER_SYMCLASS_SCALAR);
    put_u32(&base, a); put_u32(&base, empty); put_u32(&base, 0); put_u32(&base, 1); put_u32(&base, 1);
    put_u32(&base, MOJOSHADER_SYMTYPE_FLOAT); put_u32(&base, MOJOSHADER_SYMCLASS_VECTOR);
    put_u32(&base, b); put_u32(&base, empty); put_u32(&base, 0); put_u32(&base, 2); put_u32(&base, 1);
    put_f32(&base, 0.25f); put_f32(&base, 0.75f); put_f32(&base, 1.25f);
    /* sampler2D Samp { MagFilter = 2; Texture = <Tex>; }  -- Tex is object 0 */
    t_filter = base.len;
    put_u32(&base, MOJOSHADER_SYMTYPE_INT); put_u32(&base, MOJOSHADER_SYMCLASS_SCALAR);
    put_u32(&base, empty); put_u32(&base, empty); put_u32(&base, 0); put_u32(&base, 1); put_u32(&base, 1);
    v_filter = base.len; put_u32(&base, 2);
    t_tex = base.len;
    put_u32(&base, MOJOSHADER_SYMTYPE_TEXTURE2D); put_u32(&base, MOJOSHADER_SYMCLASS_OBJECT);
    put_u32(&base, tex); put_u32(&base, empty); put_u32(&base, 0);
    v_tex = base.len; put_u32(&base, 0);
    t_samp = base.len;
    put_u32(&base, MOJOSHADER_SYMTYPE_SAMPLER2D); put_u32(&base, MOJOSHADER_SYMCLASS_OBJECT);
    put_u32(&base, samp); put_u32(&base, empty); put_u32(&base, 0);
    v_samp = base.len;
    put_u32(&base, 2);                                    /* two sampler states */
    put_u32(&base, MOJOSHADER_SAMP_MAGFILTER); put_u32(&base, 0); put_u32(&base, t_filter); put_u32(&base, v_filter);
    put_u32(&base, MOJOSHADER_SAMP_TEXTURE); put_u32(&base, 0); put_u32(&base, t_tex); put_u32(&base, v_tex);
    /* string Str = "hello";  -- object 1 */
    t_str = base.len;
    put_u32(&base, MOJOSHADER_SYMTYPE_STRING); put_u32(&base, MOJOSHADER_SYMCLASS_OBJECT);
    put_u32(&base, str); put_u32(&base, empty); put_u32(&base, 0);
    v_str = base.len; put_u32(&base, 1);
    /* technique annotation: int Mode = 7; pass annotation: float Depth = 0.125; */
    t_mode = base.len;
    put_u32(&base, MOJOSHADER_SYMTYPE_INT); put_u32(&base, MOJOSHADER_SYMCLASS_SCALAR);
    put_u32(&base, mode); put_u32(&base, empty); put_u32(&base, 0); put_u32(&base, 1); put_u32(&base, 1);
    v_mode = base.len; put_u32(&base, 7);
    t_depth = base.len;
    put_u32(&base, MOJOSHADER_SYMTYPE_FLOAT); put_u32(&base, MOJOSHADER_SYMCLASS_SCALAR);
    put_u32(&base, depth); put_u32(&base, empty); put_u32(&base, 0); put_u32(&base, 1); put_u32(&base, 1);
    v_depth = base.len; put_f32(&base, 0.125f);
    /* the PIXELSHADERCONSTANT state's float4 value */
    t_state = base.len;
    put_u32(&base, MOJOSHADER_SYMTYPE_FLOAT); put_u32(&base, MOJOSHADER_SYMCLASS_VECTOR);
    put_u32(&base, empty); put_u32(&base, empty); put_u32(&base, 0); put_u32(&base, 4); put_u32(&base, 1);
    v_state = base.len; put_u32(&base, 0); put_u32(&base, 0); put_u32(&base, 0); put_u32(&base, 0);

    memset(&file, '\0', sizeof (file));
    put_u32(&file, 0xFEFF0901);
    put_u32(&file, base.len);
    memcpy(file.bytes + file.len, base.bytes, base.len);
    file.len += base.len;
    put_u32(&file, 4);                   /* parameters */
    put_u32(&file, 1);                   /* techniques */
    put_u32(&file, 0);
    put_u32(&file, 2);                   /* objects: the texture and the string */
    put_u32(&file, t_scale); put_u32(&file, v_scale); put_u32(&file, 0); put_u32(&file, 2);
    put_u32(&file, t_gain); put_u32(&file, v_gain);
    put_u32(&file, t_level); put_u32(&file, v_level);
    put_u32(&file, t_light); put_u32(&file, 0); put_u32(&file, 0); put_u32(&file, 0);
    put_u32(&file, t_samp); put_u32(&file, v_samp); put_u32(&file, 0); put_u32(&file, 0);
    put_u32(&file, t_str); put_u32(&file, v_str); put_u32(&file, 0); put_u32(&file, 0);
    put_u32(&file, techname); put_u32(&file, 1); put_u32(&file, 1);     /* technique */
    put_u32(&file, t_mode); put_u32(&file, v_mode);
    put_u32(&file, passname); put_u32(&file, 1); put_u32(&file, 2);     /* pass */
    put_u32(&file, t_depth); put_u32(&file, v_depth);
    put_u32(&file, 0); put_u32(&file, 0); put_u32(&file, t_filter); put_u32(&file, v_filter); /* ZENABLE */
    put_u32(&file, 160); put_u32(&file, 0); put_u32(&file, t_state); put_u32(&file, v_state);
    put_u32(&file, 2);                   /* small objects */
    put_u32(&file, 1);                   /* large objects */
    put_u32(&file, 0); put_u32(&file, 4); memcpy(file.bytes + file.len, "Tex", 4); file.len += 4;
    put_u32(&file, 1); put_u32(&file, 8); memcpy(file.bytes + file.len, "hello\0\0", 8); file.len += 8;
    /* large object: technique 0, pass 0, FIXME, state 1, type 0, length; then the preshader */
    put_u32(&file, 0); put_u32(&file, 0); put_u32(&file, 0); put_u32(&file, 1); put_u32(&file, 0);
    put_u32(&file, prelen);
    memcpy(file.bytes + file.len, pre.at, prelen);
    file.len += prelen;

    memcpy(out, file.bytes, file.len);
    cna_words_free(&pre);
    return file.len;
} // clone_coverage_effect

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
 * copypreshader must each fail cleanly. R2-04: and a clone is either complete, equal to its
 * source in every part, or NULL -- MOJOSHADER_cloneEffect's out-of-memory result -- never a
 * "successful" clone missing a name or a value array whose allocation failed. */
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
    if ((e == NULL) || (e->error_count != 0))
    {
        MOJOSHADER_deleteEffect(e);
        free(copy);
        return;
    } // if
    base_live = cna_alloc_live();

    for (fail_at = 1; fail_at <= 512; fail_at++)
    {
        MOJOSHADER_effect *clone;
        cna_alloc_arm(cna_alloc_attempts() + fail_at);
        clone = MOJOSHADER_cloneEffect(e);
        if (clone == NULL)
        {
            if (!cna_alloc_hit())
                fprintf(stderr, "  %s: NULL clone with no allocation failure\n", what);
            CNA_CHECK(cna_alloc_hit(), "a clone fails only when an allocation failed");
        } // if
        else
        {
            const int same = same_effect(e, clone);
            if (!same)
                fprintf(stderr, "  %s: allocation %d failed, and the clone came back partial\n",
                        what, fail_at);
            CNA_CHECK(same, "a clone that comes back is complete and equal to its source");
            MOJOSHADER_deleteEffect(clone);
        } // else
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
    unsigned char bytes[4096];
    unsigned int len = preshader_effect(bytes);
    inject_clone(bytes, len, "a preshader effect clone frees everything under allocation failure");
    len = clone_coverage_effect(bytes);
    inject_clone(bytes, len, "a clone of every value kind frees everything under allocation failure");
} // test_effect_clone_allocation_failures

/* The clone sweep's richer fixture is itself a valid effect: it loads without errors, and a clone
 * of it is complete and equal to it. */
static void test_clone_coverage_effect_loads_and_clones(void)
{
    unsigned char bytes[4096];
    const unsigned int len = clone_coverage_effect(bytes);
    MOJOSHADER_effectShaderContext ctx = backend();
    const MOJOSHADER_effect *effect = MOJOSHADER_compileEffect(bytes, len, NULL, 0, NULL, 0, &ctx);
    MOJOSHADER_effect *clone;
    CNA_CHECK((effect != NULL) && (effect->error_count == 0), "the fixture loads cleanly");
    if ((effect == NULL) || (effect->error_count != 0))
    {
        if ((effect != NULL) && (effect->error_count > 0))
            fprintf(stderr, "  first error: %s\n", effect->errors[0].error);
        MOJOSHADER_deleteEffect(effect);
        return;
    } // if
    CNA_CHECK(effect->param_count == 4, "four parameters");
    CNA_CHECK(effect->object_count == 2, "two objects");
    if ((effect->param_count == 4) && (effect->object_count == 2))
    {
        CNA_CHECK(same_string(effect->params[0].value.semantic, "SCALE"), "Scale's semantic");
        CNA_CHECK(effect->params[0].annotation_count == 2, "Scale's two annotations");
        CNA_CHECK(effect->params[1].value.type.member_count == 2, "Light's two members");
        CNA_CHECK(effect->params[2].value.value_count == 2, "Samp's two states");
        CNA_CHECK(effect->objects[0].type == MOJOSHADER_SYMTYPE_SAMPLER2D, "Tex is a sampler object");
        CNA_CHECK(same_string(effect->objects[0].mapping.name, "Tex"), "Tex's name");
        CNA_CHECK(effect->objects[1].type == MOJOSHADER_SYMTYPE_STRING, "Str is a string object");
        CNA_CHECK(same_string(effect->objects[1].string.string, "hello"), "Str's text");
        CNA_CHECK(effect->techniques[0].passes[0].states[1].preshader != NULL, "the pass preshader");
    } // if
    clone = MOJOSHADER_cloneEffect(effect);
    CNA_CHECK((clone != NULL) && same_effect(effect, clone), "the clone equals its source");
    MOJOSHADER_deleteEffect(clone);
    MOJOSHADER_deleteEffect(effect);
} // test_clone_coverage_effect_loads_and_clones

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
    cna_test_run("value_wider_than_a_register", test_value_wider_than_a_register);
    cna_test_run("value_column_count_wraps", test_value_column_count_wraps);
    cna_test_run("value_taller_than_a_matrix", test_value_taller_than_a_matrix);
    cna_test_run("value_struct_member_wider_than_a_register",
                 test_value_struct_member_wider_than_a_register);
    cna_test_run("record_counts_honest", test_record_counts_honest);
    cna_test_run("annotation_count_wraps_its_array", test_annotation_count_wraps_its_array);
    cna_test_run("pass_count_wraps_its_array", test_pass_count_wraps_its_array);
    cna_test_run("state_count_wraps_its_array", test_state_count_wraps_its_array);
    cna_test_run("xna4_wrapper_truncated_after_skip", test_xna4_wrapper_truncated_after_skip);
    cna_test_run("effect_load_allocation_failures", test_effect_load_allocation_failures);
    cna_test_run("clone_coverage_effect_loads_and_clones", test_clone_coverage_effect_loads_and_clones);
    cna_test_run("effect_clone_allocation_failures", test_effect_clone_allocation_failures);
    return cna_test_finish();
} // main
