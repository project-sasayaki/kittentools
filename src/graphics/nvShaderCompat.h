#pragma once

#include <stdbool.h>
#include <stddef.h>

/* vp40/fp40 program rebuilt for arbvp1/arbfp1 from its Cg source (malloc'd), or NULL.
 * envConstants: constants in program.env instead of program.local. */
char *nvShader_recompileNvAssembly(const char *src, size_t len, bool envConstants);
