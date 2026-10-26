/**
 * Float constants in the text profiles (A3f-01..A3f-04).
 *
 * MOJOSHADER_printFloat must write every finite binary32 as a decimal of at most nine
 * significant digits that reads back as exactly the same float -- through a correctly rounding
 * parser and through one that rounds to double first, as shader compilers commonly do -- with a
 * '.', a C-locale spelling whatever LC_NUMERIC says, and short enough for the profiles' 32-byte
 * buffers. The profiles must embed that spelling unchanged in DEF constants and constant arrays,
 * and spell a non-finite constant in the target language or refuse the shader.
 *
 * MOJOSHADER_CNA_PRINTFLOAT_EXHAUSTIVE=1 also checks all 2^32 bit patterns, which takes a while;
 * MOJOSHADER_CNA_PRINTFLOAT_SHARD=i/n then checks only the patterns congruent to i modulo n.
 *
 * This file is part of the libcna fork of MojoShader; see tests/cna/README.md.
 */

#include <float.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cna_test.h"

#define __MOJOSHADER_INTERNAL__ 1
#include "mojoshader_internal.h"

static float float_from_bits(const uint32_t bits)
{
    float f;
    memcpy(&f, &bits, sizeof (f));
    return f;
} // float_from_bits

static uint32_t bits_from_float(const float f)
{
    uint32_t bits;
    memcpy(&bits, &f, sizeof (bits));
    return bits;
} // bits_from_float

static int is_digit(const char c)
{
    return (c >= '0') && (c <= '9');
} // is_digit

/* -?[0-9]+\.[0-9]+(e-?[0-9]+)? -- a float literal in GLSL (ES 1.00 included), HLSL, Metal and
 * ARB assembly alike. */
static int is_float_literal(const char *s)
{
    if (*s == '-')
        s++;
    if (!is_digit(*s))
        return 0;
    while (is_digit(*s))
        s++;
    if (*s++ != '.')
        return 0;
    if (!is_digit(*s))
        return 0;
    while (is_digit(*s))
        s++;
    if (*s == 'e')
    {
        s++;
        if (*s == '-')
            s++;
        if (!is_digit(*s))
            return 0;
        while (is_digit(*s))
            s++;
    } // if
    return *s == '\0';
} // is_float_literal

/* The significant digits of a decimal spelling, without leading or trailing zeros, and the
 * decimal exponent of the first of them; returns the digit count (0 for zero). */
static int decimal_digits(const char *s, char *digits, int *exponent)
{
    int count = 0;
    int position = 0;       // index of the current digit among all mantissa digits
    int integer_digits = -1;
    int first = -1;
    if (*s == '-')
        s++;
    for (; is_digit(*s) || (*s == '.'); s++)
    {
        if (*s == '.')
        {
            integer_digits = position;
            continue;
        } // if
        if ((first >= 0) || (*s != '0'))
        {
            if (first < 0)
                first = position;
            digits[count++] = *s;
        } // if
        position++;
    } // for
    if (integer_digits < 0)
        integer_digits = position;
    while ((count > 0) && (digits[count - 1] == '0'))
        count--;
    digits[count] = '\0';
    *exponent = (integer_digits - 1 - first) + (((*s == 'e') || (*s == 'E')) ? atoi(s + 1) : 0);
    return count;
} // decimal_digits

static uint32_t digest = 2166136261u;  // FNV-1a over every spelling the round-trip check makes

static void digest_text(const char *text)
{
    for (; *text; text++)
        digest = (digest ^ (uint32_t) (unsigned char) *text) * 16777619u;
    digest = (digest ^ 0xffu) * 16777619u;
} // digest_text

/* Checks one finite pattern: well-formed literal, round trip both ways, and the correctly
 * rounded decimal of its length. Returns 0 on a failure (already reported). */
static int check_finite_bits(const uint32_t bits, const int full)
{
    char text[64];
    char again[64];
    const float f = float_from_bits(bits);
    const size_t len = MOJOSHADER_printFloat(text, sizeof (text), f);
    int ok = 1;

    if (full)
        digest_text(text);

    if ((len != strlen(text)) || (len > 15) || !is_float_literal(text))
        ok = 0;
    else if (bits_from_float(strtof(text, NULL)) != bits)
        ok = 0;  // a correctly rounding parser reads another float
    else if (bits_from_float((float) strtod(text, NULL)) != bits)
        ok = 0;  // a parser that rounds to double first reads another float
    else if (full && (bits & 0x7fffffffu) != 0)
    {
        // The digits must be the correctly rounded decimal of that length; glibc's %e is exact.
        char digits[32];
        char reference[64];
        char reference_digits[32];
        int exponent = 0;
        int reference_exponent = 0;
        const int count = decimal_digits(text, digits, &exponent);
        snprintf(reference, sizeof (reference), "%.*e", (count > 0) ? count - 1 : 0, (double) f);
        decimal_digits(reference, reference_digits, &reference_exponent);
        if ((count < 1) || (count > 9) || (strcmp(digits, reference_digits) != 0) ||
            (exponent != reference_exponent))
            ok = 0;
    } // else if

    if (!ok)
    {
        MOJOSHADER_printFloat(again, sizeof (again), f);
        fprintf(stderr, "  bits %08x printed `%s` (length %u), which reads back as %08x\n",
                (unsigned) bits, again, (unsigned) len,
                (unsigned) bits_from_float(strtof(again, NULL)));
    } // if
    return ok;
} // check_finite_bits

/* Every spelling the profiles rely on, with why it is pinned. */
typedef struct ExpectedFloat
{
    uint32_t bits;
    const char *text;
} ExpectedFloat;

static const ExpectedFloat expected_floats[] =
{
    { 0x00000000u, "0.0" },
    { 0x80000000u, "-0.0" },            // A3f-01: the sign was dropped
    { 0x3f800000u, "1.0" },
    { 0xbf800000u, "-1.0" },
    { 0x3f000000u, "0.5" },
    { 0x3f666666u, "0.9" },             // A3f-01: was 0.900000023, the next float up
    { 0x3dcccccdu, "0.1" },
    { 0x3eaaaaabu, "0.33333334" },
    { 0x40490fdbu, "3.1415927" },
    { 0x42c80000u, "100.0" },
    { 0x4b800000u, "16777216.0" },
    { 0x4cbebc20u, "100000000.0" },
    { 0x4e6e6b28u, "1.0e9" },
    { 0x38d1b717u, "0.0001" },
    { 0x3727c5acu, "1.0e-5" },          // A3f-01: 0.000010000 on SSE, 0.000009999 on x87
    { 0x374f2049u, "1.2345678e-5" },    // A3f-01: only four of its digits survived
    { 0x2edbe6ffu, "1.0e-10" },         // A3f-01: was 0.0
    { 0x00800000u, "1.1754944e-38" },   // FLT_MIN; A3f-01: was 0.0
    { 0x007fffffu, "1.1754942e-38" },   // largest subnormal
    { 0x00000001u, "1.0e-45" },         // smallest subnormal
    { 0x80000001u, "-1.0e-45" },
    { 0x5f7fffffu, "1.8446743e19" },    // just below 2^64; A3f-02: overflowed floatstr
    { 0x5f800000u, "1.8446744e19" },    // 2^64; A3f-03: printed with LC_NUMERIC's comma
    { 0x5f800001u, "1.8446746e19" },
    { 0xdd800000u, "-1.1529215e18" },   // -2^60; A3f-02
    { 0x6e013f39u, "1.0e28" },          // 1e28f; A3f-02
    { 0x7f7fffffu, "3.4028235e38" },    // FLT_MAX; A3f-02
    { 0xff7fffffu, "-3.4028235e38" },
};

#define EXPECTED_FLOAT_COUNT ((int) (sizeof (expected_floats) / sizeof (expected_floats[0])))

static void test_named_values(void)
{
    int i;
    for (i = 0; i < EXPECTED_FLOAT_COUNT; i++)
    {
        char text[64];
        const size_t len = MOJOSHADER_printFloat(text, sizeof (text),
                                                 float_from_bits(expected_floats[i].bits));
        if ((strcmp(text, expected_floats[i].text) != 0) || (len != strlen(text)))
        {
            fprintf(stderr, "  bits %08x: expected `%s`, printed `%s` (returned %u)\n",
                    (unsigned) expected_floats[i].bits, expected_floats[i].text, text,
                    (unsigned) len);
            CNA_CHECK(0, "a pinned float spelling changed");
        } // if
        CNA_CHECK(check_finite_bits(expected_floats[i].bits, 1), "pinned value round trip");
    } // for
} // test_named_values

static uint32_t random_state = 0x2545f491u;

static uint32_t next_random(void)
{
    // xorshift32: a fixed, portable sequence.
    random_state ^= random_state << 13;
    random_state ^= random_state >> 17;
    random_state ^= random_state << 5;
    return random_state;
} // next_random

static void test_round_trip_sample(void)
{
    static const uint32_t mantissas[] =
    {
        0x000000u, 0x000001u, 0x000002u, 0x000003u, 0x3fffffu, 0x400000u, 0x400001u,
        0x7ffffdu, 0x7ffffeu, 0x7fffffu
    };
    int failures = 0;
    int checked = 0;
    uint32_t sign;
    uint32_t exponent;
    int i;
    int power;

    // Every exponent, both signs, the mantissas at either end and in the middle.
    for (sign = 0; sign < 2; sign++)
    {
        for (exponent = 0; exponent < 255; exponent++)
        {
            for (i = 0; i < (int) (sizeof (mantissas) / sizeof (mantissas[0])); i++)
            {
                const uint32_t bits = (sign << 31) | (exponent << 23) | mantissas[i];
                failures += !check_finite_bits(bits, 1);
                checked++;
            } // for
        } // for
    } // for

    // The floats nearest each power of ten and their neighbours, where the decimal exponent
    // changes.
    for (power = -45; power <= 38; power++)
    {
        char text[32];
        uint32_t bits;
        snprintf(text, sizeof (text), "1e%d", power);
        bits = bits_from_float(strtof(text, NULL));
        if (bits > 1)
            failures += !check_finite_bits(bits - 1, 1);
        failures += !check_finite_bits(bits, 1);
        if (bits < 0x7f7fffffu)
            failures += !check_finite_bits(bits + 1, 1);
        checked += 3;
    } // for

    // A fixed pseudorandom sample of the remaining finite patterns.
    for (i = 0; i < 200000; i++)
    {
        const uint32_t bits = next_random();
        if ((bits & 0x7f800000u) == 0x7f800000u)
            continue;  // not finite
        failures += !check_finite_bits(bits, 1);
        checked++;
    } // for

    printf("  %d finite patterns checked, %d failed; spelling digest %08x\n",
           checked, failures, (unsigned) digest);
    CNA_CHECK(failures == 0, "every sampled finite float prints as itself");
} // test_round_trip_sample

static void test_exhaustive(void)
{
    const char *enabled = getenv("MOJOSHADER_CNA_PRINTFLOAT_EXHAUSTIVE");
    const char *shard = getenv("MOJOSHADER_CNA_PRINTFLOAT_SHARD");
    unsigned long long pattern;
    unsigned long long failures = 0;
    unsigned long long checked = 0;
    unsigned int index = 0;
    unsigned int stride = 1;

    if ((enabled == NULL) || (strcmp(enabled, "1") != 0))
    {
        printf("  [ SKIP ] MOJOSHADER_CNA_PRINTFLOAT_EXHAUSTIVE=1 checks all 2^32 patterns\n");
        return;
    } // if
    if ((shard != NULL) && ((sscanf(shard, "%u/%u", &index, &stride) != 2) ||
                            (stride == 0) || (index >= stride)))
    {
        CNA_CHECK(0, "MOJOSHADER_CNA_PRINTFLOAT_SHARD must be i/n with 0 <= i < n");
        return;
    } // if

    for (pattern = index; pattern <= 0xffffffffull; pattern += stride)
    {
        const uint32_t bits = (uint32_t) pattern;
        if ((bits & 0x7f800000u) == 0x7f800000u)
            continue;  // not finite
        if (!check_finite_bits(bits, 0))
        {
            if (++failures > 20)
                break;
        } // if
        checked++;
    } // for
    printf("  shard %u/%u: %llu finite patterns checked, %llu failed\n",
           index, stride, checked, failures);
    CNA_CHECK(failures == 0, "every finite float prints as itself");
} // test_exhaustive

static void test_buffer_bounds(void)
{
    static const uint32_t values[] =
    {
        0x00000000u, 0x80000000u, 0x3f800000u, 0x2edbe6ffu, 0x3eaaaaabu, 0x4cbebc20u,
        0x808fffffu, 0xff7fffffu, 0x7f800000u, 0xff800000u, 0x7fc00000u
    };
    int i;
    for (i = 0; i < (int) (sizeof (values) / sizeof (values[0])); i++)
    {
        const float f = float_from_bits(values[i]);
        char full[64];
        const size_t len = MOJOSHADER_printFloat(full, sizeof (full), f);
        size_t maxlen;

        CNA_CHECK(len == strlen(full), "the return value is the spelling's length");
        CNA_CHECK(MOJOSHADER_printFloat(NULL, 0, f) == len,
                  "maxlen 0 writes nothing and still returns the needed length");

        for (maxlen = 0; maxlen <= len + 3; maxlen++)
        {
            unsigned char area[64];
            char *exact = (char *) malloc(maxlen + 1);  // ASan sees a write past maxlen
            const size_t kept = (maxlen > 0) ? ((len < maxlen) ? len : maxlen - 1) : 0;
            size_t returned;
            size_t j;
            int canaries = 1;

            memset(area, 0xa5, sizeof (area));
            returned = MOJOSHADER_printFloat((char *) area, maxlen, f);
            for (j = maxlen; j < sizeof (area); j++)
                canaries = canaries && (area[j] == 0xa5);
            CNA_CHECK(canaries, "nothing is written at or past maxlen");
            CNA_CHECK(returned == len, "a short buffer still returns the needed length");
            if (maxlen > 0)
            {
                CNA_CHECK(area[kept] == '\0', "a nonempty buffer is always NUL-terminated");
                CNA_CHECK(memcmp(area, full, kept) == 0, "a short buffer keeps the prefix");
            } // if
            else
                CNA_CHECK(area[0] == 0xa5, "maxlen 0 writes nothing");

            if (exact != NULL)
            {
                char *buffer = (maxlen > 0) ? exact : NULL;
                if (maxlen > 0)
                    memset(exact, 0xa5, maxlen);
                CNA_CHECK(MOJOSHADER_printFloat(buffer, maxlen, f) == len,
                          "an exactly sized heap buffer returns the needed length");
                if (maxlen > 0)
                    CNA_CHECK(exact[kept] == '\0', "the heap buffer is NUL-terminated");
                free(exact);
            } // if
        } // for
    } // for
} // test_buffer_bounds

/* Token builders for DEF and a minimal CTAB (needed before a shader may address constants
 * relatively). */
#define DEF4(reg, ...) CNA_OP(CNA_DEF, 5), CNA_DST(CNA_CONST, reg, 0xF), __VA_ARGS__
#define CNA_MOVA 46
#define CNA_RELATIVE 0x2000u
#define CTAB_VS_2_0 0xfffeu | (10u << 16), 0x42415443u /* 'CTAB' */, 28, 28, CNA_VS(2, 0), 0, \
    28, 0, 28, 0x325f7376u /* "vs_2" */, 0x0000305fu /* "_0" */

#define FINITE_ROW0 0x2edbe6ffu, 0x6e013f39u, 0x7f7fffffu, 0xdd800000u
#define FINITE_ROW1 0x3f666666u, 0x374f2049u, 0x00800000u, 0x80000000u
#define FINITE_ROW2 0x5f800000u, 0x5f7fffffu, 0x00000001u, 0x3f000000u

static const char *const finite_rows[] =
{
    "1.0e-10, 1.0e28, 3.4028235e38, -1.1529215e18",
    "0.9, 1.2345678e-5, 1.1754944e-38, -0.0",
    "1.8446744e19, 1.8446743e19, 1.0e-45, 0.5",
};

/* The same rows with the decimal point dropped where nothing follows it, as the D3D
 * disassembly writes them. */
static const char *const finite_rows_d3d[] =
{
    "1e-10, 1e28, 3.4028235e38, -1.1529215e18",
    "0.9, 1.2345678e-5, 1.1754944e-38, -0",
    "1.8446744e19, 1.8446743e19, 1e-45, 0.5",
};

static const uint32_t finite_vs[] =
{
    CNA_VS(2, 0),
    DEF4(0, FINITE_ROW0),
    DEF4(1, FINITE_ROW1),
    DEF4(2, FINITE_ROW2),
    CNA_OP(CNA_DCL_OP, 2), CNA_DCL(0, 0), CNA_DST(CNA_INPUT, 0, 0xF),
    CNA_OP(CNA_MOV, 2), CNA_DST(CNA_RASTOUT, 0, 0xF), CNA_SRC(CNA_INPUT, 0),
    CNA_OP(CNA_ADD, 3), CNA_DST(CNA_ATTROUT, 0, 0xF), CNA_SRC(CNA_CONST, 0), CNA_SRC(CNA_CONST, 1),
    CNA_OP(CNA_ADD, 3), CNA_DST(CNA_ATTROUT, 1, 0xF), CNA_SRC(CNA_CONST, 2), CNA_SRC(CNA_CONST, 0),
    CNA_END
};

static const uint32_t finite_ps[] =
{
    CNA_PS(2, 0),
    DEF4(0, FINITE_ROW0),
    DEF4(1, FINITE_ROW1),
    DEF4(2, FINITE_ROW2),
    CNA_OP(CNA_ADD, 3), CNA_DST(CNA_TEMP, 0, 0xF), CNA_SRC(CNA_CONST, 0), CNA_SRC(CNA_CONST, 1),
    CNA_OP(CNA_ADD, 3), CNA_DST(CNA_TEMP, 0, 0xF), CNA_SRC(CNA_TEMP, 0), CNA_SRC(CNA_CONST, 2),
    CNA_OP(CNA_MOV, 2), CNA_DST(CNA_COLOROUT, 0, 0xF), CNA_SRC(CNA_TEMP, 0),
    CNA_END
};

/* c0..c2 addressed relatively, so the profiles that can also emit them as one constant array. */
static const uint32_t finite_relative_vs[] =
{
    CNA_VS(2, 0),
    CTAB_VS_2_0,
    DEF4(0, FINITE_ROW0),
    DEF4(1, FINITE_ROW1),
    DEF4(2, FINITE_ROW2),
    CNA_OP(CNA_DCL_OP, 2), CNA_DCL(0, 0), CNA_DST(CNA_INPUT, 0, 0xF),
    CNA_OP(CNA_MOVA, 2), CNA_DST(CNA_ADDR, 0, 0x1), CNA_SRC_SWZ(CNA_INPUT, 0, 0x00),
    CNA_OP(CNA_MOV, 3), CNA_DST(CNA_RASTOUT, 0, 0xF), CNA_SRC(CNA_CONST, 0) | CNA_RELATIVE,
        CNA_SRC_SWZ(CNA_ADDR, 0, 0x00),
    CNA_END
};

typedef struct TextProfile
{
    const char *name;
    const char *vector_open;  // what wraps one DEF's four values
    const char *vector_close;
    int constant_arrays;      // whether a relatively addressed DEF range becomes a literal array
} TextProfile;

static const TextProfile text_profiles[] =
{
    { MOJOSHADER_PROFILE_GLSL, "vec4(", ")", 0 },
    { MOJOSHADER_PROFILE_GLSL120, "vec4(", ")", 0 },
    { MOJOSHADER_PROFILE_GLSLES, "vec4(", ")", 0 },
    { MOJOSHADER_PROFILE_GLSLES3, "vec4(", ")", 0 },
    { MOJOSHADER_PROFILE_HLSL, "float4(", ")", 1 },
    { MOJOSHADER_PROFILE_METAL, "float4(", ")", 1 },
    { MOJOSHADER_PROFILE_ARB1, "{ ", " }", 1 },
    { MOJOSHADER_PROFILE_D3D, "def c", "", 0 },
};

#define TEXT_PROFILE_COUNT ((int) (sizeof (text_profiles) / sizeof (text_profiles[0])))

/* Translates `tokens` with `profile`, expecting no errors and each needle at least
 * `min_count` times. */
static void expect_translation(const char *profile, const char *what, const uint32_t *tokens,
                               const size_t bytes, const char *const *needles,
                               const int needle_count, const int min_count)
{
    char label[128];
    const MOJOSHADER_parseData *pd = cna_parse(profile, tokens, bytes);
    int i;
    snprintf(label, sizeof (label), "%s %s", profile, what);
    CNA_CHECK(cna_report_errors(label, pd) == 0, "the shader translates");
    for (i = 0; i < needle_count; i++)
    {
        if (cna_output_count(pd, needles[i]) < min_count)
        {
            fprintf(stderr, "  %s: expected `%s` at least %d time(s) in:\n%s\n", label,
                    needles[i], min_count, (pd && pd->output) ? pd->output : "(no output)");
            CNA_CHECK(0, "the constant is spelled exactly");
        } // if
    } // for
    MOJOSHADER_freeParseData(pd);
} // expect_translation

static void finite_needles(const TextProfile *profile, char needles[3][96])
{
    const int d3d = (strcmp(profile->name, MOJOSHADER_PROFILE_D3D) == 0);
    int row;
    for (row = 0; row < 3; row++)
    {
        if (d3d)
            snprintf(needles[row], 96, "def c%d, %s", row, finite_rows_d3d[row]);
        else
        {
            snprintf(needles[row], 96, "%s%s%s", profile->vector_open, finite_rows[row],
                     profile->vector_close);
        } // else
    } // for
} // finite_needles

static void test_profiles_finite(void)
{
    int i;
    for (i = 0; i < TEXT_PROFILE_COUNT; i++)
    {
        const TextProfile *profile = &text_profiles[i];
        char storage[3][96];
        const char *needles[3];
        int row;
        finite_needles(profile, storage);
        for (row = 0; row < 3; row++)
            needles[row] = storage[row];
        expect_translation(profile->name, "vs_2_0 DEF", finite_vs, sizeof (finite_vs),
                           needles, 3, 1);
        expect_translation(profile->name, "ps_2_0 DEF", finite_ps, sizeof (finite_ps),
                           needles, 3, 1);
        // DEF line plus array element where the profile writes the array out.
        expect_translation(profile->name, "vs_2_0 relative DEF", finite_relative_vs,
                           sizeof (finite_relative_vs), needles, 3,
                           profile->constant_arrays ? 2 : 1);
    } // for
} // test_profiles_finite

/* The three shapes a DEF row can be written in: a vertex shader macro, a pixel shader const,
 * and a relatively addressed constant array. */
#define DEF_VS(ROW0, ROW1) \
{ \
    CNA_VS(2, 0), DEF4(0, ROW0), DEF4(1, ROW1), \
    CNA_OP(CNA_DCL_OP, 2), CNA_DCL(0, 0), CNA_DST(CNA_INPUT, 0, 0xF), \
    CNA_OP(CNA_MOV, 2), CNA_DST(CNA_RASTOUT, 0, 0xF), CNA_SRC(CNA_INPUT, 0), \
    CNA_OP(CNA_ADD, 3), CNA_DST(CNA_ATTROUT, 0, 0xF), CNA_SRC(CNA_CONST, 0), \
        CNA_SRC(CNA_CONST, 1), \
    CNA_END \
}
#define DEF_PS(ROW0, ROW1) \
{ \
    CNA_PS(2, 0), DEF4(0, ROW0), DEF4(1, ROW1), \
    CNA_OP(CNA_ADD, 3), CNA_DST(CNA_TEMP, 0, 0xF), CNA_SRC(CNA_CONST, 0), CNA_SRC(CNA_CONST, 1), \
    CNA_OP(CNA_MOV, 2), CNA_DST(CNA_COLOROUT, 0, 0xF), CNA_SRC(CNA_TEMP, 0), \
    CNA_END \
}
#define DEF_RELATIVE_VS(ROW0, ROW1) \
{ \
    CNA_VS(2, 0), CTAB_VS_2_0, DEF4(0, ROW0), DEF4(1, ROW1), \
    CNA_OP(CNA_DCL_OP, 2), CNA_DCL(0, 0), CNA_DST(CNA_INPUT, 0, 0xF), \
    CNA_OP(CNA_MOVA, 2), CNA_DST(CNA_ADDR, 0, 0x1), CNA_SRC_SWZ(CNA_INPUT, 0, 0x00), \
    CNA_OP(CNA_MOV, 3), CNA_DST(CNA_RASTOUT, 0, 0xF), CNA_SRC(CNA_CONST, 0) | CNA_RELATIVE, \
        CNA_SRC_SWZ(CNA_ADDR, 0, 0x00), \
    CNA_END \
}

// Both infinities among finite values; then NaNs: quiet, negative with a payload, signalling.
#define INF_ROW0 0x7f800000u, 0xff800000u, 0x3f800000u, 0x3f000000u
#define INF_ROW1 0xff800000u, 0x00000000u, 0x7f800000u, 0x80000000u
#define NAN_ROW0 0x7fc00000u, 0xffc00001u, 0x7f800001u, 0x3f800000u
#define NAN_ROW1 0x3f000000u, 0x7fc00000u, 0x00000000u, 0x7fc00000u

static const uint32_t inf_vs[] = DEF_VS(INF_ROW0, INF_ROW1);
static const uint32_t inf_ps[] = DEF_PS(INF_ROW0, INF_ROW1);
static const uint32_t inf_relative_vs[] = DEF_RELATIVE_VS(INF_ROW0, INF_ROW1);
static const uint32_t nan_vs[] = DEF_VS(NAN_ROW0, NAN_ROW1);
static const uint32_t nan_ps[] = DEF_PS(NAN_ROW0, NAN_ROW1);
static const uint32_t nan_relative_vs[] = DEF_RELATIVE_VS(NAN_ROW0, NAN_ROW1);

/* How `profile` must write one constant: its literal when finite, else the language's own
 * spelling. Returns 0 where the profile has none and must refuse the shader. */
static int expected_spelling(const char *profile, const uint32_t bits, char *text,
                             const size_t size)
{
    if ((bits & 0x7f800000u) != 0x7f800000u)
    {
        MOJOSHADER_printFloat(text, size, float_from_bits(bits));
        return 1;
    } // if
    if (strcmp(profile, MOJOSHADER_PROFILE_HLSL) == 0)
        snprintf(text, size, "asfloat(0x%08xu)", (unsigned) bits);  // Shader Model 4
    else if (strcmp(profile, MOJOSHADER_PROFILE_METAL) == 0)
        snprintf(text, size, "as_type<float>(0x%08xu)", (unsigned) bits);
    else if ((strcmp(profile, MOJOSHADER_PROFILE_GLSLES3) == 0) && ((bits & 0x007fffffu) == 0))
        snprintf(text, size, "%s1.0e39", (bits >> 31) ? "-" : "");  // ESSL 3.00 section 4.1.4
    else
        return 0;
    return 1;
} // expected_spelling

/* A profile with no spelling must refuse the shader and say why, instead of writing text no
 * compiler takes. */
static void expect_refusal(const char *profile, const char *what, const uint32_t *tokens,
                           const size_t bytes, const char *reason)
{
    const MOJOSHADER_parseData *pd = cna_parse(profile, tokens, bytes);
    int explained = 0;
    int i;
    for (i = 0; (pd != NULL) && (i < pd->error_count); i++)
    {
        if ((pd->errors[i].error != NULL) && (strstr(pd->errors[i].error, reason) != NULL))
            explained = 1;
    } // for
    if (!explained)
    {
        fprintf(stderr, "  %s %s: expected the error `%s`, got %d error(s) and:\n%s\n",
                profile, what, reason, pd ? pd->error_count : -1,
                (pd && pd->output) ? pd->output : "(no output)");
    } // if
    CNA_CHECK(explained, "a profile without a spelling refuses the constant, saying why");
    MOJOSHADER_freeParseData(pd);
} // expect_refusal

static void check_nonfinite_rows(const uint32_t rows[2][4], const char *reason,
                                 const uint32_t *vs, const size_t vs_bytes,
                                 const uint32_t *ps, const size_t ps_bytes,
                                 const uint32_t *relative_vs, const size_t relative_vs_bytes)
{
    int i;
    for (i = 0; i < TEXT_PROFILE_COUNT; i++)
    {
        const TextProfile *profile = &text_profiles[i];
        char storage[2][256];
        const char *needles[2];
        int spellable = 1;
        int row;
        for (row = 0; row < 2; row++)
        {
            char values[4][48];
            int column;
            for (column = 0; column < 4; column++)
            {
                spellable &= expected_spelling(profile->name, rows[row][column], values[column],
                                               sizeof (values[column]));
            } // for
            snprintf(storage[row], sizeof (storage[row]), "%s%s, %s, %s, %s%s",
                     profile->vector_open, values[0], values[1], values[2], values[3],
                     profile->vector_close);
            needles[row] = storage[row];
        } // for

        if (!spellable)
        {
            expect_refusal(profile->name, "vs_2_0 DEF", vs, vs_bytes, reason);
            expect_refusal(profile->name, "ps_2_0 DEF", ps, ps_bytes, reason);
            expect_refusal(profile->name, "vs_2_0 relative DEF", relative_vs,
                           relative_vs_bytes, reason);
        } // if
        else
        {
            expect_translation(profile->name, "vs_2_0 DEF", vs, vs_bytes, needles, 2, 1);
            expect_translation(profile->name, "ps_2_0 DEF", ps, ps_bytes, needles, 2, 1);
            expect_translation(profile->name, "vs_2_0 relative DEF", relative_vs,
                               relative_vs_bytes, needles, 2, profile->constant_arrays ? 2 : 1);
        } // else
    } // for
} // check_nonfinite_rows

static void test_profiles_infinity(void)
{
    static const uint32_t rows[2][4] = { { INF_ROW0 }, { INF_ROW1 } };
    check_nonfinite_rows(rows, "Float constant +inf (0x7f800000) cannot be written",
                         inf_vs, sizeof (inf_vs), inf_ps, sizeof (inf_ps),
                         inf_relative_vs, sizeof (inf_relative_vs));
} // test_profiles_infinity

static void test_profiles_nan(void)
{
    static const uint32_t rows[2][4] = { { NAN_ROW0 }, { NAN_ROW1 } };
    check_nonfinite_rows(rows, "Float constant NaN (0x7fc00000) cannot be written",
                         nan_vs, sizeof (nan_vs), nan_ps, sizeof (nan_ps),
                         nan_relative_vs, sizeof (nan_relative_vs));
} // test_profiles_nan

/* SPIR-V writes constants as binary words, never through floatstr(), and must keep taking
 * every one of them. */
static void test_spirv_nonfinite(void)
{
    static const char *const profiles[] = { MOJOSHADER_PROFILE_SPIRV, MOJOSHADER_PROFILE_GLSPIRV };
    const uint32_t *const shaders[] = { inf_ps, nan_ps, inf_vs, nan_vs };
    const size_t sizes[] = { sizeof (inf_ps), sizeof (nan_ps), sizeof (inf_vs), sizeof (nan_vs) };
    int i;
    int j;
    for (i = 0; i < 2; i++)
    {
        for (j = 0; j < 4; j++)
        {
            const MOJOSHADER_parseData *pd = cna_parse(profiles[i], shaders[j], sizes[j]);
            char name[64];
            snprintf(name, sizeof (name), "printfloat_nonfinite_%d_%s", j, profiles[i]);
            CNA_CHECK(cna_report_errors(name, pd) == 0, "SPIR-V takes non-finite constants");
            CNA_CHECK(cna_spirv_validate(pd, name) != 0, "the SPIR-V module validates");
            MOJOSHADER_freeParseData(pd);
        } // for
    } // for
} // test_spirv_nonfinite

static const char *find_comma_locale(void)
{
    static const char *const candidates[] =
    {
        "de_DE.UTF-8", "de_DE.utf8", "de_DE", "cs_CZ.UTF-8", "cs_CZ.utf8", "fr_FR.UTF-8",
        "fr_FR.utf8", "ru_RU.UTF-8", "ru_RU.utf8", "German_Germany.1252"
    };
    int i;
    for (i = 0; i < (int) (sizeof (candidates) / sizeof (candidates[0])); i++)
    {
        const char *set = setlocale(LC_NUMERIC, candidates[i]);
        if ((set != NULL) && (strcmp(localeconv()->decimal_point, ",") == 0))
            return candidates[i];
    } // for
    setlocale(LC_NUMERIC, "C");
    return NULL;
} // find_comma_locale

static void test_locale_independence(void)
{
    char c_spellings[EXPECTED_FLOAT_COUNT][64];
    char *c_output = NULL;
    const char *locale_name;
    const MOJOSHADER_parseData *pd;
    int i;

    setlocale(LC_NUMERIC, "C");
    for (i = 0; i < EXPECTED_FLOAT_COUNT; i++)
    {
        MOJOSHADER_printFloat(c_spellings[i], sizeof (c_spellings[i]),
                              float_from_bits(expected_floats[i].bits));
    } // for
    pd = cna_parse(MOJOSHADER_PROFILE_GLSL, finite_ps, sizeof (finite_ps));
    if ((pd != NULL) && (pd->output != NULL))
    {
        c_output = (char *) malloc(strlen(pd->output) + 1);
        if (c_output != NULL)
            strcpy(c_output, pd->output);
    } // if
    MOJOSHADER_freeParseData(pd);

    locale_name = find_comma_locale();
    if (locale_name == NULL)
    {
        printf("  [ SKIP ] no comma-decimal locale is installed; locale independence unchecked\n");
        free(c_output);
        return;
    } // if
    printf("  LC_NUMERIC=%s (decimal point ',')\n", locale_name);

    for (i = 0; i < EXPECTED_FLOAT_COUNT; i++)
    {
        char text[64];
        MOJOSHADER_printFloat(text, sizeof (text), float_from_bits(expected_floats[i].bits));
        if (strcmp(text, c_spellings[i]) != 0)
        {
            fprintf(stderr, "  bits %08x: `%s` under %s, `%s` under C\n",
                    (unsigned) expected_floats[i].bits, text, locale_name, c_spellings[i]);
            CNA_CHECK(0, "LC_NUMERIC does not change a spelling");
        } // if
    } // for

    pd = cna_parse(MOJOSHADER_PROFILE_GLSL, finite_ps, sizeof (finite_ps));
    CNA_CHECK(cna_report_errors("glsl under a comma locale", pd) == 0, "the shader translates");
    CNA_CHECK((c_output != NULL) && (pd != NULL) && (pd->output != NULL) &&
              (strcmp(pd->output, c_output) == 0),
              "LC_NUMERIC does not change a translation");
    MOJOSHADER_freeParseData(pd);
    free(c_output);
    setlocale(LC_NUMERIC, "C");
} // test_locale_independence

int main(void)
{
    setlocale(LC_ALL, "C");
    cna_test_run("named_values", test_named_values);
    cna_test_run("round_trip_sample", test_round_trip_sample);
    cna_test_run("exhaustive", test_exhaustive);
    cna_test_run("buffer_bounds", test_buffer_bounds);
    cna_test_run("profiles_finite", test_profiles_finite);
    cna_test_run("profiles_infinity", test_profiles_infinity);
    cna_test_run("profiles_nan", test_profiles_nan);
    cna_test_run("spirv_nonfinite", test_spirv_nonfinite);
    cna_test_run("locale_independence", test_locale_independence);
    return cna_test_finish();
} // main
