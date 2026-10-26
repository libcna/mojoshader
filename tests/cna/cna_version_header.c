/**
 * Includes mojoshader.h the way the standalone build's own consumers do: with the generated
 * mojoshader_version.h, not MOJOSHADER_NO_VERSION_INCLUDE.
 *
 * This file is part of the libcna fork of MojoShader; see tests/cna/README.md.
 */

#include "mojoshader.h"

const char *cna_version_header_changeset(void)
{
    return MOJOSHADER_CHANGESET;
} // cna_version_header_changeset
