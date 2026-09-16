#pragma once

#include <stdbool.h>
#include <stddef.h>

bool nvShader_isNvShaderFile(const char *pathname);
bool nvShader_convertToCache(const char *originalPath, char *targetPath, size_t targetPathSize);
char *nvShader_fixArbProgram(const char *src, size_t len);
