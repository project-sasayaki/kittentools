#pragma once


#include <glad/gl.h>

/* -------------------------------------------------------------------------
 * Lifecycle
 * ------------------------------------------------------------------------- */

void tfInit(void);

/* -------------------------------------------------------------------------
 * Accessors
 * ------------------------------------------------------------------------- */

int tfAnisotropy(void);

/* -------------------------------------------------------------------------
 * glTexParameter hook callbacks (called from glHooks.c)
 * ------------------------------------------------------------------------- */

int tfOverrideMaxAniso_i(GLenum target, GLint *param);
int tfOverrideMaxAniso_f(GLenum target, GLfloat *param);
void tfAutoInjectAfterFilter(GLenum target);
void tfOnDeleteTextures(GLsizei n, const GLuint *textures);
