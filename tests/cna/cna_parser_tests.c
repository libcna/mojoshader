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

int main(void)
{
    cna_test_run("ctab_struct_members_beyond_64k", test_ctab_struct_members_beyond_64k);
    return cna_test_finish();
} // main
