/**
 * Shared helpers for the libcna fork's regression tests.
 *
 * This file is part of the libcna fork of MojoShader; see tests/cna/README.md.
 */

#include "cna_test.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The patch table's size is internal; these tests are the library's own. */
#define __MOJOSHADER_INTERNAL__ 1
#include "mojoshader_internal.h"

static int cna_failures = 0;
static int cna_cases = 0;
static int cna_case_failed = 0;

void cna_test_fail(const char *file, int line, const char *expression, const char *message)
{
    fprintf(stderr, "%s:%d: check failed: %s (%s)\n", file, line, expression, message);
    cna_failures++;
    cna_case_failed = 1;
} // cna_test_fail

void cna_test_run(const char *name, void (*test)(void))
{
    cna_cases++;
    cna_case_failed = 0;
    printf("[ RUN  ] %s\n", name);
    fflush(stdout);
    test();
    printf("[ %s ] %s\n", cna_case_failed ? "FAIL" : " OK ", name);
    fflush(stdout);
} // cna_test_run

int cna_test_finish(void)
{
    printf("%d case(s), %d failed check(s)\n", cna_cases, cna_failures);
    return (cna_failures == 0) ? 0 : 1;
} // cna_test_finish

const MOJOSHADER_parseData *cna_parse(const char *profile, const uint32_t *tokens, size_t bytes)
{
    return MOJOSHADER_parse(profile, "main", (const unsigned char *) tokens,
                            (unsigned int) bytes, NULL, 0, NULL, 0, NULL, NULL, NULL);
} // cna_parse

int cna_report_errors(const char *what, const MOJOSHADER_parseData *pd)
{
    int i;
    if (pd == NULL)
    {
        fprintf(stderr, "%s: no parse data\n", what);
        return 1;
    } // if
    for (i = 0; i < pd->error_count; i++)
        fprintf(stderr, "%s: error at %d: %s\n", what, pd->errors[i].error_position,
                pd->errors[i].error ? pd->errors[i].error : "(null)");
    return pd->error_count;
} // cna_report_errors

int cna_output_count(const MOJOSHADER_parseData *pd, const char *text)
{
    int count = 0;
    const char *at;
    if ((pd == NULL) || (pd->output == NULL) || (text == NULL) || (*text == '\0'))
        return 0;
    at = pd->output;
    while ((at = strstr(at, text)) != NULL)
    {
        count++;
        at += strlen(text);
    } // while
    return count;
} // cna_output_count

int cna_output_contains(const MOJOSHADER_parseData *pd, const char *text)
{
    return cna_output_count(pd, text) > 0;
} // cna_output_contains

size_t cna_spirv_module_words(const MOJOSHADER_parseData *pd)
{
    if ((pd == NULL) || (pd->output == NULL) ||
        (pd->output_len < (int) sizeof (SpirvPatchTable)))
        return 0;
    return (size_t) (pd->output_len - (int) sizeof (SpirvPatchTable)) / 4;
} // cna_spirv_module_words

int cna_spirv_validate(const MOJOSHADER_parseData *pd, const char *name)
{
    const char *validator = getenv("MOJOSHADER_SPIRV_VAL");
    const char *dir = getenv("MOJOSHADER_CNA_TEST_DIR");
    char path[1024];
    char command[2300];
    const size_t words = cna_spirv_module_words(pd);
    FILE *io;
    int rc;

    if ((validator == NULL) || (*validator == '\0') || (dir == NULL) || (*dir == '\0'))
    {
        printf("  (spirv-val not configured: %s not validated)\n", name);
        return -1;
    } // if
    if (words == 0)
        return 0;

    snprintf(command, sizeof (command), "mkdir -p '%s'", dir);
    if (system(command) != 0)
        return 0;
    snprintf(path, sizeof (path), "%s/%s.spv", dir, name);
    io = fopen(path, "wb");
    if (io == NULL)
        return 0;
    if (fwrite(pd->output, 4, words, io) != words)
    {
        fclose(io);
        return 0;
    } // if
    fclose(io);

    snprintf(command, sizeof (command), "'%s' '%s'", validator, path);
    rc = system(command);
    if (rc != 0)
        fprintf(stderr, "spirv-val rejected %s (%s)\n", name, path);
    return (rc == 0) ? 1 : 0;
} // cna_spirv_validate
