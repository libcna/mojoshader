/**
 * Bytecode parser regressions (profile-independent).
 *
 * This file is part of the libcna fork of MojoShader; see tests/cna/README.md.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cna_test.h"

typedef struct Words
{
    uint32_t *at;
    size_t count;
    size_t capacity;
} Words;

static void push_word(Words *w, uint32_t value)
{
    if (w->count == w->capacity)
    {
        w->capacity = w->capacity ? (w->capacity * 2) : 1024;
        w->at = (uint32_t *) realloc(w->at, w->capacity * sizeof (uint32_t));
    } // if
    w->at[w->count++] = value;
} // push_word

/* A NUL-terminated string at the end of `w`, padded to a word; returns its byte offset. */
static uint32_t push_string(Words *w, const char *text)
{
    const uint32_t offset = (uint32_t) (w->count * 4);
    const size_t len = strlen(text) + 1;
    size_t i;
    for (i = 0; i < len; i += 4)
    {
        uint32_t word = 0;
        size_t j;
        for (j = 0; (j < 4) && (i + j < len); j++)
            word |= ((uint32_t) (unsigned char) text[i + j]) << (8 * j);
        push_word(w, word);
    } // for
    return offset;
} // push_string

/* D3DXSHADER_TYPEINFO: six WORDs, then the DWORD offset of the member table. */
static void push_typeinfo(Words *w, uint16_t klass, uint16_t type, uint16_t rows, uint16_t columns,
                          uint16_t elements, uint16_t members, uint32_t memberinfo)
{
    push_word(w, (uint32_t) klass | ((uint32_t) type << 16));
    push_word(w, (uint32_t) rows | ((uint32_t) columns << 16));
    push_word(w, (uint32_t) elements | ((uint32_t) members << 16));
    push_word(w, memberinfo);
} // push_typeinfo

/*
 * A2v2 A-17: a vs_2_0 shader whose constant table describes one struct constant `S` with one
 * float4 member `m`, and puts S's member table 64 KiB + 16 bytes into the table -- where a
 * reader of only the low 16 bits of StructMemberInfo looks at byte 16 instead.
 */
static void test_ctab_struct_members_beyond_64k(void)
{
    const uint32_t member_table = 0x10010;
    Words ctab;
    Words shader;
    uint32_t creator, target, sname, mname, stype, mtype, i;
    const MOJOSHADER_parseData *pd;

    memset(&ctab, '\0', sizeof (ctab));
    /* Header: Size, Creator, Version, Constants, ConstantInfo, Flags, Target (patched below). */
    for (i = 0; i < 7; i++)
        push_word(&ctab, 0);
    /* One D3DXSHADER_CONSTANTINFO: Name, RegisterSet|RegisterIndex, RegisterCount|Reserved,
     * TypeInfo, DefaultValue (patched below). */
    for (i = 0; i < 5; i++)
        push_word(&ctab, 0);
    creator = push_string(&ctab, "cna tests");
    target = push_string(&ctab, "vs_2_0");
    sname = push_string(&ctab, "S");
    mname = push_string(&ctab, "m");
    stype = (uint32_t) (ctab.count * 4);
    push_typeinfo(&ctab, MOJOSHADER_SYMCLASS_STRUCT, MOJOSHADER_SYMTYPE_VOID, 1, 4, 1, 1, member_table);
    mtype = (uint32_t) (ctab.count * 4);
    push_typeinfo(&ctab, MOJOSHADER_SYMCLASS_VECTOR, MOJOSHADER_SYMTYPE_FLOAT, 1, 4, 1, 0, 0);
    while (ctab.count * 4 < member_table)
        push_word(&ctab, 0);
    push_word(&ctab, mname);          /* D3DXSHADER_STRUCTMEMBERINFO: Name, TypeInfo */
    push_word(&ctab, mtype);

    ctab.at[0] = 28;
    ctab.at[1] = creator;
    ctab.at[2] = CNA_VS(2, 0);
    ctab.at[3] = 1;
    ctab.at[4] = 28;
    ctab.at[5] = 0;
    ctab.at[6] = target;
    ctab.at[7] = sname;
    ctab.at[8] = 2u;                  /* RegisterSet FLOAT4, RegisterIndex 0 */
    ctab.at[9] = 1u;                  /* RegisterCount 1 */
    ctab.at[10] = stype;
    ctab.at[11] = 0;

    memset(&shader, '\0', sizeof (shader));
    push_word(&shader, CNA_VS(2, 0));
    push_word(&shader, 0x0000FFFEu | ((uint32_t) (ctab.count + 1) << 16));
    push_word(&shader, 0x42415443u);  /* 'CTAB' */
    for (i = 0; i < ctab.count; i++)
        push_word(&shader, ctab.at[i]);
    push_word(&shader, CNA_OP(CNA_DCL_OP, 2));
    push_word(&shader, CNA_DCL(0, 0));
    push_word(&shader, CNA_DST(CNA_INPUT, 0, 15));
    push_word(&shader, CNA_OP(CNA_ADD, 3));
    push_word(&shader, CNA_DST(CNA_RASTOUT, 0, 15));
    push_word(&shader, CNA_SRC(CNA_INPUT, 0));
    push_word(&shader, CNA_SRC(CNA_CONST, 0));
    push_word(&shader, CNA_END);

    pd = cna_parse("glsl", shader.at, shader.count * 4);
    CNA_CHECK(cna_report_errors("ctab_struct_members_beyond_64k", pd) == 0, "the shader parses");
    CNA_CHECK(pd->symbol_count == 1, "one constant");
    if (pd->symbol_count == 1)
    {
        const MOJOSHADER_symbol *sym = &pd->symbols[0];
        CNA_CHECK(strcmp(sym->name, "S") == 0, "the constant's name");
        CNA_CHECK(sym->info.parameter_class == MOJOSHADER_SYMCLASS_STRUCT, "a struct");
        CNA_CHECK(sym->info.member_count == 1, "with one member");
        if (sym->info.member_count == 1)
        {
            CNA_CHECK(strcmp(sym->info.members[0].name, "m") == 0, "the member's name");
            CNA_CHECK(sym->info.members[0].info.columns == 4, "a float4 member");
        } // if
    } // if
    MOJOSHADER_freeParseData(pd);
    free(ctab.at);
    free(shader.at);
} // test_ctab_struct_members_beyond_64k

/* Runs a preshader token stream through MOJOSHADER_parsePreshader from an exact-size allocation,
 * so AddressSanitizer reports any read even one byte past the declared blocks. */
static const MOJOSHADER_preshader *parse_preshader_exact(const CnaWords *w)
{
    unsigned char *copy = (unsigned char *) cna_exact_copy(w->at, w->count * 4);
    const MOJOSHADER_preshader *pre =
        MOJOSHADER_parsePreshader(copy, (unsigned int) (w->count * 4), NULL, NULL, NULL);
    free(copy);
    return pre;
} // parse_preshader_exact

/* A CTAB comment block the preshader section scanner records as "seen". */
static void push_ctab_block(CnaWords *w, const char *name)
{
    CnaWords c;
    memset(&c, '\0', sizeof (c));
    cna_push_ctab(&c, CNA_PRESHADER_VERSION, name);
    cna_words_push(w, CNA_COMMENT(c.count));
    cna_words_append(w, &c);
    cna_words_free(&c);
} // push_ctab_block

/* A-05: a valid standalone preshader still parses after the section-length checks are tightened. */
static void test_preshader_valid_still_parses(void)
{
    CnaWords w;
    const MOJOSHADER_preshader *pre;
    memset(&w, '\0', sizeof (w));
    cna_push_preshader(&w, "Scale", NULL);
    pre = parse_preshader_exact(&w);
    CNA_CHECK(pre != NULL, "the valid preshader parses");
    if (pre != NULL)
    {
        CNA_CHECK(pre->literal_count == 4, "its four literals");
        CNA_CHECK(pre->instruction_count == 1, "its one instruction");
        CNA_CHECK(pre->symbol_count == 1, "its one symbol");
    } // if
    MOJOSHADER_freePreshader(pre);
    cna_words_free(&w);
} // test_preshader_valid_still_parses

/* A-05: parse_preshader read its version token before checking the buffer held one. */
static void test_preshader_empty_buffer(void)
{
    unsigned char *copy = (unsigned char *) cna_exact_copy("", 0);
    const MOJOSHADER_preshader *pre = MOJOSHADER_parsePreshader(copy, 0, NULL, NULL, NULL);
    CNA_CHECK(pre == NULL, "an empty preshader buffer is refused, not read");
    MOJOSHADER_freePreshader(pre);
    free(copy);
} // test_preshader_empty_buffer

/* A valid two-word CLIT block that declares no literals (identifier and a zero count). */
static void push_empty_clit(CnaWords *w)
{
    cna_words_push(w, CNA_COMMENT(2));
    cna_words_push(w, CNA_CLIT_ID);
    cna_words_push(w, 0);
} // push_empty_clit

/* A valid two-word FXLC block that declares no instructions (identifier and a zero count). */
static void push_empty_fxlc(CnaWords *w)
{
    cna_words_push(w, CNA_COMMENT(2));
    cna_words_push(w, CNA_FXLC_ID);
    cna_words_push(w, 0);
} // push_empty_fxlc

/* A-05 / R2-08: a one-word CLIT block has no literal-count word, so reading its second word
 * takes the word that follows the block. The other required blocks are present and the CLIT
 * block is followed by one (so the section scanner accepts the stream and the literal read is
 * actually reached): without the fix the scanner's next comment token is read as a literal count
 * of hundreds of thousands and the literals are memcpy'd from past the one-word block; with the
 * fix the short block is refused. The earlier version of this case omitted the CTAB block, so
 * parse_preshader rejected it for "No CTAB" before ever reading the CLIT count -- it passed with
 * the fix removed. */
static void test_preshader_short_clit_block(void)
{
    CnaWords w, ctab;
    const MOJOSHADER_preshader *pre;
    memset(&w, '\0', sizeof (w));
    cna_words_push(&w, CNA_PRESHADER_VERSION);
    cna_words_push(&w, CNA_COMMENT(1));   /* CLIT: identifier only, not last in the stream */
    cna_words_push(&w, CNA_CLIT_ID);
    memset(&ctab, '\0', sizeof (ctab));
    cna_push_ctab(&ctab, CNA_PRESHADER_VERSION, "Scale");
    cna_words_push(&w, CNA_COMMENT(ctab.count));
    cna_words_append(&w, &ctab); cna_words_free(&ctab);
    push_empty_fxlc(&w);
    pre = parse_preshader_exact(&w);
    CNA_CHECK(pre == NULL, "a one-word CLIT block is refused");
    MOJOSHADER_freePreshader(pre);
    cna_words_free(&w);
} // test_preshader_short_clit_block

/* A-05 / R2-08: the same for a one-word FXLC block -- the required CLIT and CTAB blocks are
 * present and valid, so the instruction read is reached, and the FXLC block is the stream's
 * last, so reading its absent second word runs past the buffer: without the fix that is a
 * heap-buffer-overflow under AddressSanitizer; with the fix the short block is refused. The
 * earlier version of this case omitted the CTAB block, so parse_preshader rejected it for "No
 * CTAB" before reaching the FXLC read -- it passed with the fix removed. */
static void test_preshader_short_fxlc_block(void)
{
    CnaWords w, ctab;
    const MOJOSHADER_preshader *pre;
    memset(&w, '\0', sizeof (w));
    cna_words_push(&w, CNA_PRESHADER_VERSION);
    push_empty_clit(&w);
    memset(&ctab, '\0', sizeof (ctab));
    cna_push_ctab(&ctab, CNA_PRESHADER_VERSION, "Scale");
    cna_words_push(&w, CNA_COMMENT(ctab.count));
    cna_words_append(&w, &ctab); cna_words_free(&ctab);
    cna_words_push(&w, CNA_COMMENT(1));   /* FXLC: identifier only, last in the stream */
    cna_words_push(&w, CNA_FXLC_ID);
    pre = parse_preshader_exact(&w);
    CNA_CHECK(pre == NULL, "a one-word FXLC block is refused");
    MOJOSHADER_freePreshader(pre);
    cna_words_free(&w);
} // test_preshader_short_fxlc_block

/* A-05: a comment block whose declared length swallowed its own header token underflowed the
 * remaining-token counter to four billion and the scan walked off the buffer. */
static void test_preshader_comment_block_exceeds_stream(void)
{
    CnaWords w;
    const MOJOSHADER_preshader *pre;
    memset(&w, '\0', sizeof (w));
    cna_words_push(&w, CNA_PRESHADER_VERSION);
    cna_words_push(&w, CNA_COMMENT(2));   /* claims two words, but only one follows */
    cna_words_push(&w, CNA_CLIT_ID);
    pre = parse_preshader_exact(&w);
    CNA_CHECK(pre == NULL, "an over-long comment block is refused");
    MOJOSHADER_freePreshader(pre);
    cna_words_free(&w);
} // test_preshader_comment_block_exceeds_stream

/* A-05: the FXLC instruction header was bounded once, not before each instruction, so variable
 * operands could leave the next instruction's header past the block end. */
static void test_preshader_fxlc_instruction_truncated(void)
{
    CnaWords w;
    const MOJOSHADER_preshader *pre;
    memset(&w, '\0', sizeof (w));
    cna_words_push(&w, CNA_PRESHADER_VERSION);
    cna_words_push(&w, CNA_COMMENT(2));
    cna_words_push(&w, CNA_CLIT_ID); cna_words_push(&w, 0);
    push_ctab_block(&w, "Scale");
    cna_words_push(&w, CNA_COMMENT(7));   /* FXLC claims two instructions, holds one */
    cna_words_push(&w, CNA_FXLC_ID);
    cna_words_push(&w, 2);                /* instruction count */
    cna_words_push(&w, 0x10000004u);      /* MOV, four elements */
    cna_words_push(&w, 0);                /* one operand */
    cna_words_push(&w, 0);                /* operand: no arrays */
    cna_words_push(&w, 4);                /* operand: OUTPUT */
    cna_words_push(&w, 0);                /* operand: index */
    pre = parse_preshader_exact(&w);
    CNA_CHECK(pre == NULL, "a truncated FXLC instruction stream is refused");
    MOJOSHADER_freePreshader(pre);
    cna_words_free(&w);
} // test_preshader_fxlc_instruction_truncated

/* A-05: a shader's CTAB comment was handed commenttoks * 4 bytes although its data begins after
 * the CTAB identifier, so a truncated CTAB could be read four bytes past the comment. */
static void test_shader_ctab_span_truncated(void)
{
    CnaWords w;
    unsigned char *copy;
    const MOJOSHADER_parseData *pd;
    memset(&w, '\0', sizeof (w));
    cna_words_push(&w, CNA_VS(2, 0));
    cna_words_push(&w, CNA_COMMENT(8));   /* comment payload is eight words */
    cna_words_push(&w, CNA_CTAB_ID);
    cna_words_push(&w, 28);               /* Size */
    cna_words_push(&w, 0);                /* Creator */
    cna_words_push(&w, CNA_VS(2, 0));     /* Version == the shader's version token */
    cna_words_push(&w, 0);                /* Constants */
    cna_words_push(&w, 0);                /* ConstantInfo */
    cna_words_push(&w, 0);                /* Flags */
    cna_words_push(&w, 28);               /* Target string, in the spurious trailing word */
    copy = (unsigned char *) cna_exact_copy(w.at, w.count * 4);
    pd = MOJOSHADER_parse("glsl", "main", copy, (unsigned int) (w.count * 4),
                          NULL, 0, NULL, 0, NULL, NULL, NULL);
    CNA_CHECK(pd != NULL, "a parse result comes back");
    CNA_CHECK((pd == NULL) || (pd->error_count > 0), "the truncated CTAB is rejected");
    MOJOSHADER_freeParseData(pd);
    free(copy);
    cna_words_free(&w);
} // test_shader_ctab_span_truncated

/* A-06: parse_preshader() set instruction_count before the instruction array was allocated, so a
 * failed allocation left the destructor walking a NULL array; its register allocation was zeroed
 * unchecked. Fail each allocation of an otherwise valid preshader parse in turn: every run must
 * return cleanly and free everything, whichever allocation failed. */
static void test_preshader_allocation_failures(void)
{
    CnaWords w;
    unsigned char *copy;
    size_t bytes;
    int fail_at;
    int got_full_run = 0;

    memset(&w, '\0', sizeof (w));
    cna_push_preshader(&w, "Scale", NULL);
    copy = (unsigned char *) cna_exact_copy(w.at, w.count * 4);
    bytes = w.count * 4;

    for (fail_at = 1; fail_at <= 64; fail_at++)
    {
        const MOJOSHADER_preshader *pre;
        cna_alloc_reset(fail_at);
        pre = MOJOSHADER_parsePreshader(copy, (unsigned int) bytes,
                                        cna_alloc_malloc, cna_alloc_free, NULL);
        MOJOSHADER_freePreshader(pre);
        CNA_CHECK(cna_alloc_live() == 0, "no allocation outlives a preshader parse");
        if (!cna_alloc_hit())
        {
            got_full_run = 1;   /* fewer allocations than fail_at: every point has been covered */
            break;
        } // if
    } // for
    CNA_CHECK(got_full_run, "the injection sweep reached a complete run");

    free(copy);
    cna_words_free(&w);
} // test_preshader_allocation_failures

/* A-06: the same, reaching parse_preshader() through a full vs_2_0 shader that carries one. */
static void test_shader_with_preshader_allocation_failures(void)
{
    CnaWords w;
    unsigned char *copy;
    size_t bytes;
    int fail_at;
    int got_full_run = 0;

    memset(&w, '\0', sizeof (w));
    cna_push_vs_with_preshader(&w);
    copy = (unsigned char *) cna_exact_copy(w.at, w.count * 4);
    bytes = w.count * 4;

    for (fail_at = 1; fail_at <= 128; fail_at++)
    {
        const MOJOSHADER_parseData *pd;
        cna_alloc_reset(fail_at);
        pd = MOJOSHADER_parse("glsl", "main", copy, (unsigned int) bytes,
                              NULL, 0, NULL, 0, cna_alloc_malloc, cna_alloc_free, NULL);
        CNA_CHECK(pd != NULL, "a parse result always comes back");
        MOJOSHADER_freeParseData(pd);
        CNA_CHECK(cna_alloc_live() == 0, "no allocation outlives a shader parse");
        if (!cna_alloc_hit())
        {
            got_full_run = 1;
            break;
        } // if
    } // for
    CNA_CHECK(got_full_run, "the injection sweep reached a complete run");

    free(copy);
    cna_words_free(&w);
} // test_shader_with_preshader_allocation_failures

/* vs_3_0: FOG in o1.x declared before TEXCOORD0 in o1.yzw, each lane written alone. */
static const uint32_t fog_first_vs3[] = {
    CNA_VS(3, 0),
    CNA_OP(CNA_DCL_OP, 2), CNA_DCL(0, 0), CNA_DST(CNA_INPUT, 0, 15),
    CNA_OP(CNA_DCL_OP, 2), CNA_DCL(0, 0), CNA_DST(CNA_OUTPUT, 0, 15),
    CNA_OP(CNA_DCL_OP, 2), CNA_DCL(11, 0), CNA_DST(CNA_OUTPUT, 1, 0x1),
    CNA_OP(CNA_DCL_OP, 2), CNA_DCL(5, 0), CNA_DST(CNA_OUTPUT, 1, 0xE),
    CNA_OP(CNA_MOV, 2), CNA_DST(CNA_OUTPUT, 0, 15), CNA_SRC(CNA_INPUT, 0),
    CNA_OP(CNA_MOV, 2), CNA_DST(CNA_OUTPUT, 1, 0xE), CNA_SRC(CNA_INPUT, 0),
    CNA_OP(CNA_MOV, 2), CNA_DST(CNA_OUTPUT, 1, 0x1), CNA_SRC_SWZ(CNA_INPUT, 0, 0xFF),
    CNA_END
};

/* The same, TEXCOORD0 declared first. */
static const uint32_t fog_last_vs3[] = {
    CNA_VS(3, 0),
    CNA_OP(CNA_DCL_OP, 2), CNA_DCL(0, 0), CNA_DST(CNA_INPUT, 0, 15),
    CNA_OP(CNA_DCL_OP, 2), CNA_DCL(0, 0), CNA_DST(CNA_OUTPUT, 0, 15),
    CNA_OP(CNA_DCL_OP, 2), CNA_DCL(5, 0), CNA_DST(CNA_OUTPUT, 1, 0xE),
    CNA_OP(CNA_DCL_OP, 2), CNA_DCL(11, 0), CNA_DST(CNA_OUTPUT, 1, 0x1),
    CNA_OP(CNA_MOV, 2), CNA_DST(CNA_OUTPUT, 0, 15), CNA_SRC(CNA_INPUT, 0),
    CNA_OP(CNA_MOV, 2), CNA_DST(CNA_OUTPUT, 1, 0xE), CNA_SRC(CNA_INPUT, 0),
    CNA_OP(CNA_MOV, 2), CNA_DST(CNA_OUTPUT, 1, 0x1), CNA_SRC_SWZ(CNA_INPUT, 0, 0xFF),
    CNA_END
};

/* vs_3_0: PSIZE (which takes a full mask) and TEXCOORD0 in o1.y, in the given order. */
#define CNA_PSIZE_TEXCOORD_VS3(first_usage, first_mask, second_usage, second_mask) { \
    CNA_VS(3, 0), \
    CNA_OP(CNA_DCL_OP, 2), CNA_DCL(0, 0), CNA_DST(CNA_INPUT, 0, 15), \
    CNA_OP(CNA_DCL_OP, 2), CNA_DCL(0, 0), CNA_DST(CNA_OUTPUT, 0, 15), \
    CNA_OP(CNA_DCL_OP, 2), CNA_DCL(first_usage, 0), CNA_DST(CNA_OUTPUT, 1, first_mask), \
    CNA_OP(CNA_DCL_OP, 2), CNA_DCL(second_usage, 0), CNA_DST(CNA_OUTPUT, 1, second_mask), \
    CNA_OP(CNA_MOV, 2), CNA_DST(CNA_OUTPUT, 0, 15), CNA_SRC(CNA_INPUT, 0), \
    CNA_OP(CNA_MOV, 2), CNA_DST(CNA_OUTPUT, 1, 15), CNA_SRC(CNA_INPUT, 0), \
    CNA_END }
static const uint32_t psize_first_vs3[] = CNA_PSIZE_TEXCOORD_VS3(4, 0xF, 5, 0x2);
static const uint32_t psize_last_vs3[] = CNA_PSIZE_TEXCOORD_VS3(5, 0x2, 4, 0xF);

/*
 * R2-03 (2026-10-26 remediation review): judged by its first semantic, a vs_3_0 output on
 * which FOG was declared first had the next DCL and
 * every write to the register to .x. The disassembly shows the parser's view: both DCLs and
 * both writes keep their masks. PSIZE, which needs a full mask, shares no register, and is
 * refused the same way in either order; so is any packed register by the ARB1 profile, which
 * binds a register to one result and bound a packed one twice.
 */
static void test_packed_fog_dcl_masks(void)
{
    const MOJOSHADER_parseData *pd = cna_parse("d3d", fog_first_vs3, sizeof (fog_first_vs3));
    const MOJOSHADER_parseData *first, *last;
    CNA_CHECK(cna_report_errors("fog first", pd) == 0, "FOG declared first, then TEXCOORD0, parses");
    CNA_CHECK(cna_output_contains(pd, "dcl_fog o1.x\n"), "FOG's DCL keeps its mask");
    CNA_CHECK(cna_output_contains(pd, "dcl_texcoord o1.yzw\n"), "TEXCOORD0's DCL keeps its mask");
    CNA_CHECK(cna_output_contains(pd, "mov o1.yzw, v0\n"), "the TEXCOORD0 write keeps its mask");
    CNA_CHECK(cna_output_contains(pd, "mov o1.x, v0.w\n"), "the FOG write keeps its mask");
    MOJOSHADER_freeParseData(pd);

    first = cna_parse("d3d", psize_first_vs3, sizeof (psize_first_vs3));
    last = cna_parse("d3d", psize_last_vs3, sizeof (psize_last_vs3));
    CNA_CHECK((first->error_count == 1) && (last->error_count == 1), "PSIZE shares no register");
    CNA_CHECK((first->error_count == 1) && (last->error_count == 1) &&
              (strcmp(first->errors[0].error, last->errors[0].error) == 0) &&
              (first->errors[0].error_position == last->errors[0].error_position),
              "... refused the same way in either order");
    MOJOSHADER_freeParseData(first);
    MOJOSHADER_freeParseData(last);

    first = cna_parse("arb1", fog_first_vs3, sizeof (fog_first_vs3));
    last = cna_parse("arb1", fog_last_vs3, sizeof (fog_last_vs3));
    CNA_CHECK((first->error_count == 1) && (last->error_count == 1) &&
              (strcmp(first->errors[0].error, last->errors[0].error) == 0) &&
              (strstr(first->errors[0].error, "packed Shader Model 3 semantics") != NULL),
              "ARB1 refuses a packed register once, the same way in either order");
    MOJOSHADER_freeParseData(first);
    MOJOSHADER_freeParseData(last);
} // test_packed_fog_dcl_masks

static const uint32_t fog_alone_x_vs3[] = CNA_FOG_ALONE_VS3(0x1);
static const uint32_t fog_alone_y_vs3[] = CNA_FOG_ALONE_VS3(0x2);
static const uint32_t fog_alone_z_vs3[] = CNA_FOG_ALONE_VS3(0x4);
static const uint32_t fog_alone_w_vs3[] = CNA_FOG_ALONE_VS3(0x8);

/*
 * R5-02 (2026-10-26 remediation review, round 2): FOG alone in a vs_3_0 output made the
 * register scalar, and the parser narrowed every write to it to .x: `mov o1.z, v0`, with FOG
 * declared in o1.z, became a write of v0.x to o1.x, in every profile. The write keeps its lane
 * (the disassembly shows the parser's view). The ARB1 profiles bind the register to
 * result.fogcoord, whose fog coordinate is x, and refuse FOG declared in another lane rather
 * than write it where it is not read.
 */
static void test_standalone_fog_masks(void)
{
    static const struct { char lane; const uint32_t *tokens; size_t bytes; } vs[] = {
        { 'x', fog_alone_x_vs3, sizeof (fog_alone_x_vs3) },
        { 'y', fog_alone_y_vs3, sizeof (fog_alone_y_vs3) },
        { 'z', fog_alone_z_vs3, sizeof (fog_alone_z_vs3) },
        { 'w', fog_alone_w_vs3, sizeof (fog_alone_w_vs3) },
    };
    static const char *const arb1_profiles[] = { "arb1", "nv2", "nv3", "nv4" };
    size_t i, j;
    for (i = 0; i < sizeof (vs) / sizeof (vs[0]); i++)
    {
        char dcl[32], write[32], arb1_write[32];
        const MOJOSHADER_parseData *pd = cna_parse("d3d", vs[i].tokens, vs[i].bytes);
        snprintf(dcl, sizeof (dcl), "dcl_fog o1.%c\n", vs[i].lane);
        snprintf(write, sizeof (write), "mov o1.%c, v0\n", vs[i].lane);
        CNA_CHECK(cna_report_errors("fog alone", pd) == 0, "FOG alone in one lane parses");
        CNA_CHECK(cna_output_contains(pd, dcl), "FOG's DCL keeps its lane");
        if (!cna_output_contains(pd, write))
            fprintf(stderr, "  expected \"%.*s\" in:\n%s\n", (int) strlen(write) - 1, write,
                    pd->output ? pd->output : "");
        CNA_CHECK(cna_output_contains(pd, write), "the FOG write keeps its lane");
        MOJOSHADER_freeParseData(pd);

        snprintf(arb1_write, sizeof (arb1_write), "MOV o1.%c, v0;", vs[i].lane);
        for (j = 0; j < sizeof (arb1_profiles) / sizeof (arb1_profiles[0]); j++)
        {
            pd = cna_parse(arb1_profiles[j], vs[i].tokens, vs[i].bytes);
            if (vs[i].lane == 'x')
            {
                CNA_CHECK(cna_report_errors(arb1_profiles[j], pd) == 0,
                          "ARB1 translates FOG alone in x");
                CNA_CHECK(cna_output_contains(pd, "OUTPUT o1 = result.fogcoord;") &&
                          cna_output_contains(pd, arb1_write),
                          "... writing result.fogcoord's x");
            } // if
            else
                CNA_CHECK((pd->error_count == 1) &&
                          (strstr(pd->errors[0].error, "FOG in a lane other than x") != NULL),
                          "ARB1 refuses FOG declared in another lane");
            MOJOSHADER_freeParseData(pd);
        } // for
    } // for
} // test_standalone_fog_masks

/* R2-07: the FXLC operand loop checked the three base words of all its operands once, before the
 * loop; an INPUT operand with an array register consumes two extra words, so a later operand's
 * base read ran past the block -- and, with FXLC last in the buffer, past the buffer. */
static void test_preshader_fxlc_operand_past_block(void)
{
    CnaWords w, ctab;
    const MOJOSHADER_preshader *pre;
    memset(&w, '\0', sizeof (w));
    cna_words_push(&w, CNA_PRESHADER_VERSION);
    cna_words_push(&w, CNA_COMMENT(10));              /* CLIT: four literals */
    cna_words_push(&w, CNA_CLIT_ID); cna_words_push(&w, 4);
    { int i; for (i = 0; i < 4; i++) { cna_words_push(&w, 0); cna_words_push(&w, 0x40000000u); } }
    memset(&ctab, '\0', sizeof (ctab));              /* CTAB: Scale as float4 c0 */
    cna_push_ctab(&ctab, CNA_PRESHADER_VERSION, "Scale");
    cna_words_push(&w, CNA_COMMENT(ctab.count));
    cna_words_append(&w, &ctab); cna_words_free(&ctab);
    cna_words_push(&w, CNA_COMMENT(11));              /* FXLC last: 11 words */
    cna_words_push(&w, CNA_FXLC_ID);
    cna_words_push(&w, 1);                            /* one instruction */
    cna_words_push(&w, 0x20500004u);                 /* MUL, four elements */
    cna_words_push(&w, 1);                            /* operand count token -> two operands */
    cna_words_push(&w, 1);                            /* operand 0: one array register */
    cna_words_push(&w, 2);                            /* operand 0: INPUT */
    cna_words_push(&w, 0);                            /* operand 0: item c0 */
    cna_words_push(&w, 0);
    cna_words_push(&w, 0);                            /* the array register's jmp word */
    cna_words_push(&w, 0);
    cna_words_push(&w, 0);                            /* operand 1 has no 3-word room left */
    pre = parse_preshader_exact(&w);
    CNA_CHECK(pre == NULL, "an FXLC operand past the block is refused");
    MOJOSHADER_freePreshader(pre);
    cna_words_free(&w);
} // test_preshader_fxlc_operand_past_block

/* R2-07: the PRSI output-map bound, (output_map_count + 1) * 2, is computed in 32 bits, so a
 * count near UINT32_MAX wraps it to a small product and passes; the preshader must be refused,
 * not accepted to iterate output_map_count times over the map later. */
static void test_preshader_prsi_count_wraps(void)
{
    CnaWords w;
    CnaPreshaderLayout layout;
    const MOJOSHADER_preshader *pre;
    memset(&w, '\0', sizeof (w));
    cna_push_preshader(&w, "Scale", &layout);
    /* output_map_count is the eighth word after PRSI_ID (itself one past the comment token). */
    w.at[layout.prsi + 1 + 7] = 0xFFFFFFFFu;
    pre = parse_preshader_exact(&w);
    CNA_CHECK(pre == NULL, "a wrapping PRSI output-map count is refused");
    MOJOSHADER_freePreshader(pre);
    cna_words_free(&w);
} // test_preshader_prsi_count_wraps

int main(void)
{
    cna_test_run("ctab_struct_members_beyond_64k", test_ctab_struct_members_beyond_64k);
    cna_test_run("preshader_allocation_failures", test_preshader_allocation_failures);
    cna_test_run("shader_with_preshader_allocation_failures",
                 test_shader_with_preshader_allocation_failures);
    cna_test_run("preshader_valid_still_parses", test_preshader_valid_still_parses);
    cna_test_run("preshader_empty_buffer", test_preshader_empty_buffer);
    cna_test_run("preshader_short_clit_block", test_preshader_short_clit_block);
    cna_test_run("preshader_short_fxlc_block", test_preshader_short_fxlc_block);
    cna_test_run("preshader_comment_block_exceeds_stream", test_preshader_comment_block_exceeds_stream);
    cna_test_run("preshader_fxlc_instruction_truncated", test_preshader_fxlc_instruction_truncated);
    cna_test_run("preshader_fxlc_operand_past_block", test_preshader_fxlc_operand_past_block);
    cna_test_run("preshader_prsi_count_wraps", test_preshader_prsi_count_wraps);
    cna_test_run("shader_ctab_span_truncated", test_shader_ctab_span_truncated);
    cna_test_run("packed_fog_dcl_masks", test_packed_fog_dcl_masks);
    cna_test_run("standalone_fog_masks", test_standalone_fog_masks);
    return cna_test_finish();
} // main
