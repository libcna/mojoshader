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
    const char *only = getenv("MOJOSHADER_CNA_TEST_CASE");
    if ((only != NULL) && (*only != '\0') && (strcmp(only, name) != 0))
        return;
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

void *cna_exact_copy(const void *data, size_t bytes)
{
    void *copy = malloc(bytes ? bytes : 1);
    if ((copy != NULL) && (bytes > 0))
        memcpy(copy, data, bytes);
    return copy;
} // cna_exact_copy

static int cna_alloc_fail_at = 0;
static int cna_alloc_attempt_count = 0;
static int cna_alloc_live_count = 0;
static int cna_alloc_was_hit = 0;

void cna_alloc_reset(int fail_at)
{
    cna_alloc_fail_at = fail_at;
    cna_alloc_attempt_count = 0;
    cna_alloc_live_count = 0;
    cna_alloc_was_hit = 0;
} // cna_alloc_reset

int cna_alloc_attempts(void) { return cna_alloc_attempt_count; }
int cna_alloc_live(void) { return cna_alloc_live_count; }
int cna_alloc_hit(void) { return cna_alloc_was_hit; }

void *MOJOSHADERCALL cna_alloc_malloc(int bytes, void *data)
{
    void *ptr;
    (void) data;
    cna_alloc_attempt_count++;
    if (cna_alloc_attempt_count == cna_alloc_fail_at)
    {
        cna_alloc_was_hit = 1;
        return NULL;
    } // if
    if (bytes < 0)
        return NULL;
    ptr = malloc(bytes ? (size_t) bytes : 1);
    if (ptr != NULL)
        cna_alloc_live_count++;
    return ptr;
} // cna_alloc_malloc

void MOJOSHADERCALL cna_alloc_free(void *ptr, void *data)
{
    (void) data;
    if (ptr != NULL)
    {
        cna_alloc_live_count--;
        free(ptr);
    } // if
} // cna_alloc_free

void cna_words_push(CnaWords *w, uint32_t value)
{
    if (w->count == w->capacity)
    {
        w->capacity = w->capacity ? (w->capacity * 2) : 256;
        w->at = (uint32_t *) realloc(w->at, w->capacity * sizeof (uint32_t));
        if (w->at == NULL)
        {
            fprintf(stderr, "out of memory building a test fixture\n");
            exit(2);
        } // if
    } // if
    w->at[w->count++] = value;
} // cna_words_push

void cna_words_append(CnaWords *w, const CnaWords *other)
{
    size_t i;
    for (i = 0; i < other->count; i++)
        cna_words_push(w, other->at[i]);
} // cna_words_append

uint32_t cna_words_string(CnaWords *w, const char *text)
{
    const uint32_t offset = (uint32_t) (w->count * 4);
    const size_t len = strlen(text) + 1;
    size_t i, j;
    for (i = 0; i < len; i += 4)
    {
        uint32_t word = 0;
        for (j = 0; (j < 4) && (i + j < len); j++)
            word |= ((uint32_t) (unsigned char) text[i + j]) << (8 * j);
        cna_words_push(w, word);
    } // for
    return offset;
} // cna_words_string

void cna_words_free(CnaWords *w)
{
    free(w->at);
    w->at = NULL;
    w->count = w->capacity = 0;
} // cna_words_free

void cna_push_ctab(CnaWords *w, uint32_t version, const char *name)
{
    CnaWords ctab;
    uint32_t creator, sname, type, target;
    int i;

    memset(&ctab, '\0', sizeof (ctab));
    for (i = 0; i < 12; i++)  /* header (7 words) and one constant info (5 words) */
        cna_words_push(&ctab, 0);
    creator = cna_words_string(&ctab, "cna tests");
    sname = cna_words_string(&ctab, name);
    type = (uint32_t) (ctab.count * 4);
    cna_words_push(&ctab, (uint32_t) MOJOSHADER_SYMCLASS_VECTOR | ((uint32_t) MOJOSHADER_SYMTYPE_FLOAT << 16));
    cna_words_push(&ctab, 1u | (4u << 16));    /* rows 1, columns 4 */
    cna_words_push(&ctab, 1u);                  /* elements 1, members 0 */
    cna_words_push(&ctab, 0u);                  /* no member table */
    target = cna_words_string(&ctab, "vs_2_0");

    ctab.at[0] = 28;          /* Size */
    ctab.at[1] = creator;
    ctab.at[2] = version;
    ctab.at[3] = 1;           /* Constants */
    ctab.at[4] = 28;          /* ConstantInfo */
    ctab.at[5] = 0;           /* Flags */
    ctab.at[6] = target;
    ctab.at[7] = sname;
    ctab.at[8] = 2u;          /* RegisterSet FLOAT4, RegisterIndex 0 */
    ctab.at[9] = 1u;          /* RegisterCount 1 */
    ctab.at[10] = type;
    ctab.at[11] = 0;          /* no default value */

    cna_words_push(w, CNA_CTAB_ID);
    cna_words_append(w, &ctab);
    cna_words_free(&ctab);
} // cna_push_ctab

void cna_push_preshader(CnaWords *w, const char *input, CnaPreshaderLayout *layout)
{
    const size_t start = w->count;
    CnaWords ctab;
    int i;

    cna_words_push(w, CNA_PRESHADER_VERSION);

    if (layout) layout->clit = w->count - start;
    cna_words_push(w, CNA_COMMENT(10));
    cna_words_push(w, CNA_CLIT_ID);
    cna_words_push(w, 4);                     /* four doubles of 2.0 */
    for (i = 0; i < 4; i++)
    {
        cna_words_push(w, 0x00000000u);
        cna_words_push(w, 0x40000000u);
    } // for

    if (layout) layout->fxlc = w->count - start;
    cna_words_push(w, CNA_COMMENT(13));
    cna_words_push(w, CNA_FXLC_ID);
    cna_words_push(w, 1);                     /* one instruction */
    cna_words_push(w, 0x20500004u);           /* MUL, four elements */
    cna_words_push(w, 2);                     /* two sources, and the destination */
    cna_words_push(w, 0); cna_words_push(w, 2); cna_words_push(w, 0);   /* input c0 */
    cna_words_push(w, 0); cna_words_push(w, 1); cna_words_push(w, 0);   /* literal 0 */
    cna_words_push(w, 0); cna_words_push(w, 4); cna_words_push(w, 0);   /* output c0 */

    if (layout) layout->prsi = w->count - start;
    cna_words_push(w, CNA_COMMENT(12));
    cna_words_push(w, CNA_PRSI_ID);
    cna_words_push(w, 0);                     /* first output register */
    cna_words_push(w, 0);
    cna_words_push(w, 0);
    cna_words_push(w, 1);                     /* output register count */
    cna_words_push(w, 0);
    cna_words_push(w, 0);
    cna_words_push(w, 1);                     /* output map entries */
    cna_words_push(w, 0); cna_words_push(w, 1);   /* register 0, one register */
    cna_words_push(w, 0); cna_words_push(w, 0);

    memset(&ctab, '\0', sizeof (ctab));
    cna_push_ctab(&ctab, CNA_PRESHADER_VERSION, input);
    if (layout) layout->ctab = w->count - start;
    cna_words_push(w, CNA_COMMENT(ctab.count));
    cna_words_append(w, &ctab);
    cna_words_free(&ctab);

    if (layout) layout->end = w->count - start;
    cna_words_push(w, 0x0000FFFFu);
} // cna_push_preshader

void cna_push_vs_with_preshader(CnaWords *w)
{
    CnaWords block;

    cna_words_push(w, CNA_VS(2, 0));

    memset(&block, '\0', sizeof (block));
    cna_push_ctab(&block, CNA_VS(2, 0), "Scale");
    cna_words_push(w, CNA_COMMENT(block.count));
    cna_words_append(w, &block);
    cna_words_free(&block);

    /* The parser hands a PRES block's preshader one token less than the block holds: the end
     * token stays outside it, as fxc lays it out. */
    cna_push_preshader(&block, "Scale", NULL);
    cna_words_push(w, CNA_COMMENT(block.count + 1));
    cna_words_push(w, CNA_PRES_ID);
    cna_words_append(w, &block);
    cna_words_free(&block);

    cna_words_push(w, CNA_OP(CNA_DCL_OP, 2));
    cna_words_push(w, CNA_DCL(0, 0));
    cna_words_push(w, CNA_DST(CNA_INPUT, 0, 15));
    cna_words_push(w, CNA_OP(CNA_ADD, 3));
    cna_words_push(w, CNA_DST(CNA_RASTOUT, 0, 15));
    cna_words_push(w, CNA_SRC(CNA_INPUT, 0));
    cna_words_push(w, CNA_SRC(CNA_CONST, 0));
    cna_words_push(w, CNA_END);
} // cna_push_vs_with_preshader
