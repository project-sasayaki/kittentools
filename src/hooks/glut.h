#pragma once

void hook_GlutInit(int *argcp, char **argv);
void hook_GlutMainLoop(void);
void hook_GlutSwapBuffers(void);
int hook_GlutEnterGameMode(void);
void hook_GlutSetCursor(int glutCursor);
void hook_GlutInitWindowSize(int width, int height);
void hook_GlutInitWindowPosition(int x, int y);
void hook_GlutDisplayFunc(void (*callback)(void));
void hook_GlutIdleFunc(void (*callback)(void));
void hook_GlutInitDisplayMode(unsigned int mode);
void hook_GlutGameModeString(const char *string);
void hook_GlutPostRedisplay(void);
void hook_GlutKeyboardFunc(void (*callback)(unsigned char, int, int));
void hook_GlutLeaveGameMode(void);
int hook_GlutBitmapWidth(void *fontID, int character);
void hook_GlutBitmapCharacter(void *fontID, int character);
int hook_GlutGet(unsigned int type);
