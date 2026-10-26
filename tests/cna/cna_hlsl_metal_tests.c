/**
 * HLSL and Metal emitter regressions (A2v2 A-12).
 *
 * Every case translates a valid shader with both profiles and checks the structure of the
 * source. Where the shaderc library was found at configure time ($MOJOSHADER_SHADERC), the HLSL
 * is also compiled by glslang's HLSL front end; without it those compilations are reported as
 * skipped. No Metal compiler is assumed, so Metal is checked structurally only.
 *
 * This file is part of the libcna fork of MojoShader; see tests/cna/README.md.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if !defined(_WIN32)
#include <dlfcn.h>
#endif

#include "cna_test.h"

/* D3DDECLUSAGE_* values a DCL token carries. */
enum { CNA_POSITION = 0, CNA_TEXCOORD = 5, CNA_COLOR = 10, CNA_FOG = 11 };


static const char *const cna_profiles[] = { MOJOSHADER_PROFILE_HLSL, MOJOSHADER_PROFILE_METAL };

/* -- helpers ------------------------------------------------------------------------------- */

/* Writes a translation to $MOJOSHADER_CNA_TEST_DIR/<name>.<profile> for inspection. */
static void cna_save_source(const MOJOSHADER_parseData *pd, const char *name, const char *profile)
{
    const char *dir = getenv("MOJOSHADER_CNA_TEST_DIR");
    char path[1024];
    char command[1100];
    FILE *io;

    if ((pd == NULL) || (pd->output == NULL) || (dir == NULL) || (*dir == '\0'))
        return;
    snprintf(command, sizeof (command), "mkdir -p '%s'", dir);
    if (system(command) != 0)
        return;
    snprintf(path, sizeof (path), "%s/%s.%s", dir, name, profile);
    io = fopen(path, "wb");
    if (io == NULL)
        return;
    fwrite(pd->output, 1, (size_t) pd->output_len, io);
    fclose(io);
} // cna_save_source

/* Offset of the first `text` at or after `from`, or -1. */
static long cna_offset(const MOJOSHADER_parseData *pd, const char *text, long from)
{
    const char *at;
    if ((pd == NULL) || (pd->output == NULL) || (from < 0) || (from > (long) strlen(pd->output)))
        return -1;
    at = strstr(pd->output + from, text);
    return (at == NULL) ? -1 : (long) (at - pd->output);
} // cna_offset

/* 1 when every '{' has a later matching '}' and none is left open. */
static int cna_braces_balance(const MOJOSHADER_parseData *pd)
{
    const char *at;
    int depth = 0;
    if ((pd == NULL) || (pd->output == NULL))
        return 0;
    for (at = pd->output; *at != '\0'; at++)
    {
        if (*at == '{')
            depth++;
        else if ((*at == '}') && (--depth < 0))
            return 0;
    } // for
    return depth == 0;
} // cna_braces_balance

/* 1 when no struct in the output declares the same member name twice. */
static int cna_struct_members_unique(const MOJOSHADER_parseData *pd)
{
    const char *at;
    if ((pd == NULL) || (pd->output == NULL))
        return 0;
    for (at = strstr(pd->output, "struct "); at != NULL; at = strstr(at + 1, "struct "))
    {
        char names[64][64];
        int count = 0, i;
        const char *line = strstr(at, "{\n");
        const char *end = strstr(at, "};");
        if ((line == NULL) || (end == NULL) || (line > end))
            continue;
        for (line += 2; (line != NULL) && (line < end); line = strchr(line, '\n'), line = line ? line + 1 : NULL)
        {
            const char *name;
            size_t len = 0;
            while (*line == '\t')
                line++;
            if ((strncmp(line, "//", 2) == 0) || (line >= end))
                continue;
            name = strchr(line, ' ');  /* skip the type */
            if ((name == NULL) || (name > end))
                continue;
            name++;
            while ((name[len] != '\0') && (strchr(" ;:[\n", name[len]) == NULL))
                len++;
            if ((len == 0) || (len >= sizeof (names[0])) || (count >= 64))
                continue;
            memcpy(names[count], name, len);
            names[count][len] = '\0';
            for (i = 0; i < count; i++)
            {
                if (strcmp(names[i], names[count]) == 0)
                    return 0;
            } // for
            count++;
        } // for
    } // for
    return 1;
} // cna_struct_members_unique

/* Offset of the main function's signature in either profile's output. */
static long cna_main_offset(const MOJOSHADER_parseData *pd)
{
    long at = cna_offset(pd, "_Output main(", 0);  /* HLSL */
    if (at < 0)
        at = cna_offset(pd, "void main(", 0);
    if (at < 0)
        at = cna_offset(pd, " main (", 0);  /* Metal */
    return at;
} // cna_main_offset

/* Parses, reports errors and saves the source; NULL-safe for the checks that follow. */
static const MOJOSHADER_parseData *cna_translate(const char *profile, const uint32_t *tokens,
                                                 size_t bytes, const char *name)
{
    const MOJOSHADER_parseData *pd = cna_parse(profile, tokens, bytes);
    char what[128];
    snprintf(what, sizeof (what), "%s %s", name, profile);
    CNA_CHECK(cna_report_errors(what, pd) == 0, "the shader translates without errors");
    CNA_CHECK(cna_struct_members_unique(pd), "no struct declares a member twice");
    cna_save_source(pd, name, profile);
    return pd;
} // cna_translate

/* -- shaderc (glslang's HLSL front end), loaded at run time ------------------------------- */

typedef void *(*cna_shaderc_ptr_void)(void);
typedef void (*cna_shaderc_release)(void *);
typedef void (*cna_shaderc_set_language)(void *, int);
typedef void *(*cna_shaderc_compile)(void *, const char *, size_t, int, const char *,
                                     const char *, void *);
typedef int (*cna_shaderc_status)(void *);
typedef const char *(*cna_shaderc_message)(void *);

/*
 * Compiles a translation as HLSL into SPIR-V with the shaderc library named by
 * $MOJOSHADER_SHADERC. 1: compiled; 0: rejected; -1: no compiler configured (a skip).
 */
static int cna_hlsl_compiles(const MOJOSHADER_parseData *pd, const char *name)
{
#if defined(_WIN32)
    printf("  (shaderc not supported on this host: %s not compiled)\n", name);
    return -1;
#else
    static void *library = NULL;
    const char *path = getenv("MOJOSHADER_SHADERC");
    cna_shaderc_ptr_void compiler_initialize, options_initialize;
    cna_shaderc_release compiler_release, options_release, result_release;
    cna_shaderc_set_language set_source_language;
    cna_shaderc_compile compile_into_spv;
    cna_shaderc_status get_status;
    cna_shaderc_message get_message;
    void *compiler, *options, *result;
    int status;

    if ((path == NULL) || (*path == '\0'))
    {
        printf("  (shaderc not configured: %s not compiled)\n", name);
        return -1;
    } // if
    if ((pd == NULL) || (pd->output == NULL))
        return 0;
    if ((library == NULL) && ((library = dlopen(path, RTLD_NOW | RTLD_LOCAL)) == NULL))
    {
        fprintf(stderr, "cannot load %s: %s\n", path, dlerror());
        return 0;
    } // if

    compiler_initialize = (cna_shaderc_ptr_void) dlsym(library, "shaderc_compiler_initialize");
    compiler_release = (cna_shaderc_release) dlsym(library, "shaderc_compiler_release");
    options_initialize = (cna_shaderc_ptr_void) dlsym(library, "shaderc_compile_options_initialize");
    options_release = (cna_shaderc_release) dlsym(library, "shaderc_compile_options_release");
    set_source_language = (cna_shaderc_set_language) dlsym(library,
        "shaderc_compile_options_set_source_language");
    compile_into_spv = (cna_shaderc_compile) dlsym(library, "shaderc_compile_into_spv");
    get_status = (cna_shaderc_status) dlsym(library, "shaderc_result_get_compilation_status");
    get_message = (cna_shaderc_message) dlsym(library, "shaderc_result_get_error_message");
    result_release = (cna_shaderc_release) dlsym(library, "shaderc_result_release");
    if (!compiler_initialize || !compiler_release || !options_initialize || !options_release ||
        !set_source_language || !compile_into_spv || !get_status || !get_message ||
        !result_release)
    {
        fprintf(stderr, "%s lacks part of the shaderc C API\n", path);
        return 0;
    } // if

    compiler = compiler_initialize();
    options = options_initialize();
    set_source_language(options, 1);  /* shaderc_source_language_hlsl */
    result = compile_into_spv(compiler, pd->output, strlen(pd->output),
                              (pd->shader_type == MOJOSHADER_TYPE_PIXEL) ? 1 : 0,  /* fragment : vertex */
                              name, pd->mainfn, options);
    status = (result != NULL) ? get_status(result) : -1;
    if (status != 0)  /* shaderc_compilation_status_success */
        fprintf(stderr, "glslang rejected the HLSL of %s:\n%s\n", name,
                (result != NULL) ? get_message(result) : "(no result)");
    else
        printf("  (glslang compiled the HLSL of %s)\n", name);
    if (result != NULL)
        result_release(result);
    options_release(options);
    compiler_release(compiler);
    return (status == 0) ? 1 : 0;
#endif
} // cna_hlsl_compiles

static void cna_check_hlsl_compiles(const MOJOSHADER_parseData *pd, const char *profile,
                                    const char *name)
{
    if (strcmp(profile, MOJOSHADER_PROFILE_HLSL) == 0)
        CNA_CHECK(cna_hlsl_compiles(pd, name) != 0, "glslang compiles the generated HLSL");
} // cna_check_hlsl_compiles

/* -- A-12: LOOP ---------------------------------------------------------------------------- */

/* vs_3_0: i0 = (3, 0, 2, 0) runs three iterations with aL = 0, 2, 4. */
static const uint32_t loop_stride_vs3[] = {
    CNA_VS(3, 0),
    CNA_OP(CNA_DCL_OP, 2), CNA_DCL(CNA_POSITION, 0), CNA_DST(CNA_INPUT, 0, 0xF),
    CNA_OP(CNA_DCL_OP, 2), CNA_DCL(CNA_POSITION, 0), CNA_DST(CNA_OUTPUT, 0, 0xF),
    CNA_OP(CNA_DEFI, 5), CNA_DST(CNA_CONSTINT, 0, 0xF), 3, 0, 2, 0,
    CNA_OP(CNA_LOOP_OP, 2), CNA_SRC(CNA_LOOP, 0), CNA_SRC(CNA_CONSTINT, 0),
    CNA_OP(CNA_MOV, 2), CNA_DST(CNA_OUTPUT, 0, 0xF), CNA_SRC(CNA_INPUT, 0),
    CNA_OP(CNA_ENDLOOP, 0),
    CNA_END
};

/*
 * Runs the emitted loop header for i# = (count, initial, step): the header must be
 * "for (int aL = i0.y, aL_count = 0; aL_count < i0.x; aL += i0.z, aL_count++)", evaluated here
 * for the constant the shader defines.
 */
static int cna_emitted_iterations(const MOJOSHADER_parseData *pd, const int count,
                                  const int initial, const int step, int *last_aL)
{
    int aL, aL_count, iterations = 0;
    if (!cna_output_contains(pd, "for (int aL = i0.y, aL_count = 0; aL_count < i0.x; "
                                 "aL += i0.z, aL_count++) {"))
        return -1;
    for (aL = initial, aL_count = 0; aL_count < count; aL += step, aL_count++)
    {
        *last_aL = aL;
        if (++iterations > 1000)
            break;
    } // for
    return iterations;
} // cna_emitted_iterations

static void test_loop_counts_iterations(void)
{
    size_t i;
    for (i = 0; i < sizeof (cna_profiles) / sizeof (cna_profiles[0]); i++)
    {
        const char *profile = cna_profiles[i];
        const MOJOSHADER_parseData *pd = cna_translate(profile, loop_stride_vs3,
            sizeof (loop_stride_vs3), "loop_stride");
        int last_aL = -1;
        CNA_CHECK(pd->output != NULL, "there is source");
        CNA_CHECK(cna_braces_balance(pd), "braces balance");
        CNA_CHECK(cna_offset(pd, "for (int aL", 0) > cna_main_offset(pd), "the loop is in main");
        CNA_CHECK(!cna_output_contains(pd, "aLend"), "the loop no longer ends at aL == x + y");
        CNA_CHECK(cna_emitted_iterations(pd, 3, 0, 2, &last_aL) == 3,
                  "i0 = (3, 0, 2, 0) iterates three times");
        CNA_CHECK(last_aL == 4, "with aL = 0, 2, 4");
        CNA_CHECK(cna_output_contains(pd, "const int4 i0 = int4(3, 0, 2, 0);"), "i0 is (3, 0, 2, 0)");
        cna_check_hlsl_compiles(pd, profile, "loop_stride");
        MOJOSHADER_freeParseData(pd);
    } // for
} // test_loop_counts_iterations

/* With an argument, runs only the case of that name (one case per process, so an abort on an
 * unfixed tree does not hide the others). */
int main(int argc, char **argv)
{
    static const struct { const char *name; void (*test)(void); } cases[] = {
        { "loop_counts_iterations", test_loop_counts_iterations }
    };
    size_t i;
    for (i = 0; i < sizeof (cases) / sizeof (cases[0]); i++)
    {
        if ((argc < 2) || (strcmp(argv[1], cases[i].name) == 0))
            cna_test_run(cases[i].name, cases[i].test);
    } // for
    return cna_test_finish();
} // main
