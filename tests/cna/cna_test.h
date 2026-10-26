/**
 * Shared helpers for the libcna fork's regression tests.
 *
 * This file is part of the libcna fork of MojoShader; see tests/cna/README.md.
 */

#ifndef MOJOSHADER_CNA_TEST_H
#define MOJOSHADER_CNA_TEST_H

#include <stddef.h>
#include <stdint.h>
#include "mojoshader.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Shader Model 1-3 token builders, as the bytecode encodes them. */
#define CNA_DST(type, num, mask) (0x80000000u | (((uint32_t) (type) & 7u) << 28) | \
    (((uint32_t) (type) & 0x18u) << 8) | ((uint32_t) (mask) << 16) | (uint32_t) (num))
#define CNA_SRC(type, num) (CNA_DST(type, num, 0) | 0x00e40000u)
#define CNA_SRC_SWZ(type, num, swizzle) (CNA_DST(type, num, 0) | ((uint32_t) (swizzle) << 16))
#define CNA_OP(opcode, length) ((uint32_t) (opcode) | ((uint32_t) (length) << 24))
#define CNA_DCL(usage, index) (0x80000000u | ((uint32_t) (index) << 16) | (uint32_t) (usage))
#define CNA_VS(major, minor) (0xfffe0000u | ((uint32_t) (major) << 8) | (uint32_t) (minor))
#define CNA_PS(major, minor) (0xffff0000u | ((uint32_t) (major) << 8) | (uint32_t) (minor))
#define CNA_END 0x0000ffffu

/* Register types (D3DSPR_*). */
enum
{
    CNA_TEMP = 0, CNA_INPUT = 1, CNA_CONST = 2, CNA_ADDR = 3, CNA_TEXTURE = 3, CNA_RASTOUT = 4,
    CNA_ATTROUT = 5, CNA_OUTPUT = 6, CNA_TEXCRDOUT = 6, CNA_CONSTINT = 7, CNA_COLOROUT = 8,
    CNA_DEPTHOUT = 9, CNA_SAMPLER = 10, CNA_CONSTBOOL = 14, CNA_LOOP = 15, CNA_MISCTYPE = 17,
    CNA_LABEL = 18
};

/* Opcodes (D3DSIO_*) the tests use. */
enum
{
    CNA_NOP = 0, CNA_MOV = 1, CNA_ADD = 2, CNA_MAD = 4, CNA_MUL = 5, CNA_RCP = 6, CNA_DP4 = 9,
    CNA_CALL = 25, CNA_LOOP_OP = 27, CNA_RET = 28, CNA_ENDLOOP = 29, CNA_LABEL_OP = 30,
    CNA_DCL_OP = 31, CNA_REP = 38, CNA_ENDREP = 39, CNA_IF = 40, CNA_ENDIF = 43,
    CNA_TEXCRD = 64, CNA_TEX = 66, CNA_DEF = 81, CNA_DEFI = 48, CNA_TEXLD = 66
};

/* Counts and reports a failed expectation; the process exits nonzero at cna_test_finish(). */
void cna_test_fail(const char *file, int line, const char *expression, const char *message);
#define CNA_CHECK(condition, message) \
    do { if (!(condition)) cna_test_fail(__FILE__, __LINE__, #condition, (message)); } while (0)

/*
 * Runs one named test case and reports it. When $MOJOSHADER_CNA_TEST_CASE is set, only the case
 * of that name runs, so a sanitizer report can be told apart from the other cases'.
 */
void cna_test_run(const char *name, void (*test)(void));

/* 0 when every check passed, 1 otherwise; prints a summary. */
int cna_test_finish(void);

/* MOJOSHADER_parse() with the test's main function name and no swizzles or sampler maps. */
const MOJOSHADER_parseData *cna_parse(const char *profile, const uint32_t *tokens, size_t bytes);

/* Prints a parse's errors, if any, prefixed with `what`; returns the error count. */
int cna_report_errors(const char *what, const MOJOSHADER_parseData *pd);

/* True when `text` occurs in the parse's (text) output. */
int cna_output_contains(const MOJOSHADER_parseData *pd, const char *text);

/* Number of times `text` occurs in the parse's (text) output. */
int cna_output_count(const MOJOSHADER_parseData *pd, const char *text);

/*
 * Writes a SPIR-V parse's module (without the patch table MojoShader appends) to
 * $MOJOSHADER_CNA_TEST_DIR/<name>.spv and runs $MOJOSHADER_SPIRV_VAL on it. Returns 1 when it
 * validated, 0 when spirv-val rejected it, -1 when no validator is configured (reported as a
 * skip, not a failure).
 */
int cna_spirv_validate(const MOJOSHADER_parseData *pd, const char *name);

/* Number of SPIR-V words of the module proper, excluding the appended patch table. */
size_t cna_spirv_module_words(const MOJOSHADER_parseData *pd);

/*
 * A copy of `bytes` bytes in an allocation of exactly that size, so that AddressSanitizer reports
 * a read even one byte past the end. Release it with free().
 */
void *cna_exact_copy(const void *data, size_t bytes);

/*
 * An allocator for the MOJOSHADER_* entry points that fails one chosen allocation and counts what
 * is still live. cna_alloc_reset(n) makes the n-th allocation from then on fail (1 is the first;
 * 0 fails none).
 */
void cna_alloc_reset(int fail_at);
int cna_alloc_attempts(void);   /* allocations asked for since the reset */
int cna_alloc_live(void);       /* allocations handed out and not yet freed */
int cna_alloc_hit(void);        /* whether the chosen allocation was reached and failed */
void *MOJOSHADERCALL cna_alloc_malloc(int bytes, void *data);
void MOJOSHADERCALL cna_alloc_free(void *ptr, void *data);

/* A growable array of 32-bit words, for laying out bytecode. */
typedef struct CnaWords
{
    uint32_t *at;
    size_t count;
    size_t capacity;
} CnaWords;

void cna_words_push(CnaWords *w, uint32_t value);
void cna_words_append(CnaWords *w, const CnaWords *other);
/* A NUL-terminated string padded to a word; returns its byte offset in `w`. */
uint32_t cna_words_string(CnaWords *w, const char *text);
void cna_words_free(CnaWords *w);

/* Comment-block identifiers (little-endian FOURCCs) and a comment token of `words` words. */
#define CNA_CTAB_ID 0x42415443u
#define CNA_PRES_ID 0x53455250u
#define CNA_PRSI_ID 0x49535250u
#define CNA_CLIT_ID 0x54494C43u
#define CNA_FXLC_ID 0x434C5846u
#define CNA_COMMENT(words) (0x0000FFFEu | ((uint32_t) (words) << 16))
#define CNA_PRESHADER_VERSION 0x46580201u

/*
 * Appends a constant table from its CTAB identifier on (no comment token): a header for `version`
 * and one float4 constant named `name` in register c0.
 */
void cna_push_ctab(CnaWords *w, uint32_t version, const char *name);

/* Word positions inside a preshader cna_push_preshader() laid out, relative to its start. */
typedef struct CnaPreshaderLayout
{
    size_t clit;    /* the CLIT block's comment token */
    size_t fxlc;    /* the FXLC block's comment token */
    size_t prsi;    /* the PRSI block's comment token */
    size_t ctab;    /* the CTAB block's comment token */
    size_t end;     /* the trailing end token */
} CnaPreshaderLayout;

/*
 * Appends a valid preshader as MOJOSHADER_parsePreshader() takes it: the version token, then
 * CLIT (four literals of 2.0), FXLC (one instruction, output c0 = input `input` * literals),
 * PRSI (an output map) and CTAB (`input` as a float4 in c0) blocks, each in its own comment
 * token, then an end token. `layout` may be NULL.
 */
void cna_push_preshader(CnaWords *w, const char *input, CnaPreshaderLayout *layout);

/*
 * Appends a valid vs_2_0 shader that carries a preshader (computing c0 from the float4 "Scale")
 * and a constant table declaring "Scale" in c0: dcl_position v0; add oPos, v0, c0; end.
 */
void cna_push_vs_with_preshader(CnaWords *w);

#ifdef __cplusplus
}
#endif

#endif /* MOJOSHADER_CNA_TEST_H */
