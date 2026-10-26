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

/* A-05: a one-word CLIT block has no literal-count word to read. */
static void test_preshader_short_clit_block(void)
{
    CnaWords w;
    const MOJOSHADER_preshader *pre;
    memset(&w, '\0', sizeof (w));
    cna_words_push(&w, CNA_PRESHADER_VERSION);
    cna_words_push(&w, CNA_COMMENT(3));
    cna_words_push(&w, CNA_FXLC_ID); cna_words_push(&w, 0); cna_words_push(&w, 0);
    cna_words_push(&w, CNA_COMMENT(1));   /* CLIT: identifier only */
    cna_words_push(&w, CNA_CLIT_ID);
    pre = parse_preshader_exact(&w);
    CNA_CHECK(pre == NULL, "a one-word CLIT block is refused");
    MOJOSHADER_freePreshader(pre);
    cna_words_free(&w);
} // test_preshader_short_clit_block

/* A-05: a one-word FXLC block has no instruction-count word to read. */
static void test_preshader_short_fxlc_block(void)
{
    CnaWords w;
    const MOJOSHADER_preshader *pre;
    memset(&w, '\0', sizeof (w));
    cna_words_push(&w, CNA_PRESHADER_VERSION);
    cna_words_push(&w, CNA_COMMENT(4));
    cna_words_push(&w, CNA_CLIT_ID); cna_words_push(&w, 0); cna_words_push(&w, 0); cna_words_push(&w, 0);
    cna_words_push(&w, CNA_COMMENT(1));   /* FXLC: identifier only */
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

int main(void)
{
    cna_test_run("ctab_struct_members_beyond_64k", test_ctab_struct_members_beyond_64k);
    cna_test_run("preshader_valid_still_parses", test_preshader_valid_still_parses);
    cna_test_run("preshader_empty_buffer", test_preshader_empty_buffer);
    cna_test_run("preshader_short_clit_block", test_preshader_short_clit_block);
    cna_test_run("preshader_short_fxlc_block", test_preshader_short_fxlc_block);
    cna_test_run("preshader_comment_block_exceeds_stream", test_preshader_comment_block_exceeds_stream);
    cna_test_run("preshader_fxlc_instruction_truncated", test_preshader_fxlc_instruction_truncated);
    cna_test_run("shader_ctab_span_truncated", test_shader_ctab_span_truncated);
    return cna_test_finish();
} // main
