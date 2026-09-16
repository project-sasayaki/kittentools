#include "nvShaderCompat.h"

#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <winsock2.h>
#include <windows.h>

#include "../hooks/file.h"
#include "../log/log.h"

static bool endsWith(const char *s, const char *suffix)
{
    size_t ls = strlen(s), lf = strlen(suffix);
    return lf <= ls && !memcmp(s + ls - lf, suffix, lf);
}

static char *readWholeFile(const char *path, size_t *outSize)
{
    int fd = open(path, O_RDONLY | O_BINARY);
    if (fd == -1) return NULL;
    int64_t sz = lseek(fd, 0, SEEK_END);
    if (sz < 0) { close(fd); return NULL; }
    lseek(fd, 0, SEEK_SET);
    char *buf = malloc((size_t)sz + 1);
    if (!buf) { close(fd); return NULL; }
    size_t total = 0;
    while (total < (size_t)sz) {
        ssize_t n = read(fd, buf + total, (size_t)sz - total);
        if (n <= 0) break;
        total += (size_t)n;
    }
    close(fd);
    buf[total] = '\0';
    *outSize = total;
    return buf;
}

static bool writeWholeFile(const char *path, const void *data, size_t size)
{
    HANDLE h = CreateFileA(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD w; BOOL ok = WriteFile(h, data, (DWORD)size, &w, NULL);
    BOOL flushOk = FlushFileBuffers(h);
    BOOL closeOk = CloseHandle(h);
    return ok && (w == (DWORD)size) && flushOk && closeOk;
}

typedef struct { const char *s; const char *r; } Rule;

static const Rule kAttribs[] = {
    {"POSITION;","ATTR0;"},{"BLENDWEIGHT;","ATTR1;"},{"NORMAL;","ATTR2;"},
    {"COLOR0;","ATTR3;"},{"COLOR;","ATTR3;"},{"BLENDINDICES;","ATTR7;"},
    {"TEXCOORD0;","ATTR8;"},{"TEXCOORD1;","ATTR9;"},{"TEXCOORD2;","ATTR10;"},
    {"TEXCOORD3;","ATTR11;"},{"TANGENT;","ATTR14;"},{"BINORMAL;","ATTR15;"},
};

static const Rule kConsts[] = {
    {"const float4x4  mtxWorldViewProj;","const float4x4  mtxWorldViewProj : C0;"},
    {"const uniform float4x4  mtxWorldViewProj;","const uniform float4x4  mtxWorldViewProj : C0;"},
    {"float4x4    mtxWorldViewProj;","float4x4    mtxWorldViewProj : C0;"},
    {"const uniform float4x4  g_mtxWorldViewProj;","const uniform float4x4  g_mtxWorldViewProj : C0;"},
    {"uniform\tfloat4x4\tmtxWorldViewProj;","uniform\tfloat4x4\tmtxWorldViewProj : C0;"},
    {"const uniform float2 f2UV_Offset;","const uniform float2 f2UV_Offset : C0;"},
    {"const float4x4 g_m4WorldViewProj;","const float4x4 g_m4WorldViewProj : C0;"}
};

static char *applyRules(const char *src, size_t srcLen, const Rule *rules, size_t nRules)
{
    size_t cap = srcLen + 1;
    for (size_t i = 0; i < nRules; i++) {
        size_t sl = strlen(rules[i].s);
        if (!sl) continue;
        size_t cnt = 0; const char *p = src;
        while ((p = strstr(p, rules[i].s))) { cnt++; p += sl; }
        long g = (long)strlen(rules[i].r) - (long)sl;
        if (g > 0) cap += (size_t)g * cnt;
    }
    char *out = malloc(cap);
    if (!out) return NULL;
    size_t ol = 0;
    const char *p = src, *end = src + srcLen;
    while (p < end) {
        int m = 0;
        for (size_t i = 0; i < nRules; i++) {
            size_t sl = strlen(rules[i].s);
            if (sl && p + sl <= end && !memcmp(p, rules[i].s, sl)) {
                size_t rl = strlen(rules[i].r);
                memcpy(out + ol, rules[i].r, rl); ol += rl;
                p += sl; m = 1; break;
            }
        }
        if (!m) out[ol++] = *p++;
    }
    out[ol] = '\0';
    return out;
}

static char *applyAttribs(const char *src, size_t srcLen)
{
    char *out = malloc(srcLen * 2 + 16);
    if (!out) return NULL;
    size_t ol = 0;
    const char *p = src, *end = src + srcLen;
    while (p < end) {
        if ((end - p >= 15 &&
             (!strncmp(p, "struct VS_INPUT", 15) || !strncmp(p, "struct\tVS_INPUT", 15)))) {
            const char *be = strchr(p, '}');
            if (!be) be = end; else be++;
            size_t nRules = sizeof(kAttribs) / sizeof(kAttribs[0]);
            const char *q = p;
            while (q < be) {
                int m = 0;
                for (size_t i = 0; i < nRules; i++) {
                    size_t sl = strlen(kAttribs[i].s);
                    if (sl && q + sl <= be && !memcmp(q, kAttribs[i].s, sl)) {
                        size_t rl = strlen(kAttribs[i].r);
                        memcpy(out + ol, kAttribs[i].r, rl); ol += rl;
                        q += sl; m = 1; break;
                    }
                }
                if (!m) out[ol++] = *q++;
            }
            p = be;
        } else out[ol++] = *p++;
    }
    out[ol] = '\0';
    return out;
}


static char *nvShader_fixCgSource(const char *src, size_t srcLen)
{
    size_t nc = sizeof(kConsts) / sizeof(kConsts[0]);
    char *step1 = applyRules(src, srcLen, kConsts, nc);
    if (!step1) step1 = strdup(src);
    size_t len1 = strlen(step1);
    char *step2 = applyAttribs(step1, len1);
    free(step1);
    return step2 ? step2 : strdup(src);
}

char *nvShader_fixArbProgram(const char *src, size_t len)
{
    static const Rule kArb[] = {
        {"OPTION ARB_fragment_program_shadow;","#OPTION ARB_fragment_program_shadow;"},
        {" SHADOW2D;"," 2D;"},
    };
    return applyRules(src, len, kArb, sizeof(kArb)/sizeof(kArb[0]));
}

bool nvShader_isNvShaderFile(const char *pathname)
{
    if (!pathname) return false;
    return endsWith(pathname, ".fx") || endsWith(pathname, ".cg");
}

bool nvShader_convertToCache(const char *originalPath, char *targetPath, size_t targetPathSize)
{
    if (!originalPath || !targetPath || !targetPathSize || !nvShader_isNvShaderFile(originalPath))
        return false;

    char cachePath[512];
    getKittenPath(originalPath, cachePath);

    const char *cands[3]; char r0[512], r1[512]; int nc = 0;
    if (originalPath[0] == '/') {
        snprintf(r0, sizeof(r0), ".%s", originalPath);
        snprintf(r1, sizeof(r1), "%s", originalPath);
        cands[nc++] = r0; cands[nc++] = r1;
    } else cands[nc++] = originalPath;

    size_t srcSize = 0; char *src = NULL;
    for (int i = 0; i < nc && !src; i++) src = readWholeFile(cands[i], &srcSize);
    if (!src) return false;

    log_debug(plfNvShader, "nvShader: read %s (%zu bytes)\n", originalPath, srcSize);
    char *fixed = nvShader_fixCgSource(src, srcSize);
    free(src);
    if (!fixed) return false;

    size_t fs = strlen(fixed);
    if (!writeWholeFile(cachePath, fixed, fs)) { free(fixed); return false; }
    snprintf(targetPath, targetPathSize, "%s", cachePath);
    log_debug(plfNvShader, "nvShader: cached %s -> %s (%zu bytes)\n", originalPath, cachePath, fs);
    free(fixed);
    return true;
}