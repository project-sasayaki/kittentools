#pragma once

#include <stdbool.h>
#include <stddef.h>

bool nvShader_isNvShaderFile(const char *pathname);
bool nvShader_convertToCache(const char *originalPath, char *targetPath, size_t targetPathSize);
char *nvShader_fixArbProgram(const char *src, size_t len);
/* vp40/fp40 program rebuilt from its Cg source (malloc'd), or NULL. */
char *nvShader_recompileNvAssembly(const char *src, size_t len, bool envConstants);
