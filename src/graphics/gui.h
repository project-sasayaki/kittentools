#pragma once

#include <GL/gl.h>
#include <windows.h>

extern HWND g_Window;
extern HGLRC g_GLContext;
extern HDC g_WindowDC;

void startGraphics();
void createWindow();
void ensureGraphicsContext(void);
int makeGLCurrent(HWND win, HGLRC ctx);
struct __GLXFBConfigRec; 
int choosePixelFormatFor(HDC hdc, const struct __GLXFBConfigRec *cfg);
void processEvents();