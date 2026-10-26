/**
 * Build-level checks: the generated version header is one string literal (A3a-01), and the
 * OpenGL glue compiles without MOJOSHADER_FLIP_RENDERTARGET (A2v2 A-01; the object library
 * mojoshader_cna_opengl_noflip in CMakeLists.txt is that check, and this test is not built
 * without it).
 *
 * This file is part of the libcna fork of MojoShader; see tests/cna/README.md.
 */

#include <stdio.h>
#include <string.h>

#include "cna_test.h"

extern const char *cna_version_header_changeset(void);

static void test_changeset_is_one_line(void)
{
    const char *changeset = cna_version_header_changeset();
    CNA_CHECK(changeset != NULL, "the generated header defines MOJOSHADER_CHANGESET");
    CNA_CHECK(strchr(changeset, '\n') == NULL, "MOJOSHADER_CHANGESET is a single line");
    CNA_CHECK((strncmp(changeset, "git-", 4) == 0) || (strcmp(changeset, "???") == 0),
              "MOJOSHADER_CHANGESET names a git revision or is unknown");
    if (strncmp(changeset, "git-", 4) == 0)
        CNA_CHECK(strlen(changeset) == 4 + 40, "MOJOSHADER_CHANGESET names exactly one commit");
} // test_changeset_is_one_line

int main(void)
{
    cna_test_run("changeset_is_one_line", test_changeset_is_one_line);
    return cna_test_finish();
} // main
