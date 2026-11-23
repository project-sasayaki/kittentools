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

#include "../log/log.h"

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

typedef struct { const char *s; const char *r; } Rule;

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

/* ---- vp40/fp40 assembly rebuilt for arbvp1/arbfp1 ---- */

/* From the .cg source and entry point named in cgc's header. */

#define NVSC_CG_COMPILED_PROGRAM 4106
#define NVSC_CG_SOURCE 4112
#define NVSC_CG_PROFILE_ARBVP1 6150
#define NVSC_CG_PROFILE_ARBFP1 7000

typedef void *(__cdecl *NvscCgCreateContextFn)(void);
typedef void *(__cdecl *NvscCgCreateProgramFn)(void *, int, const char *, int, const char *, const char **);
typedef const char *(__cdecl *NvscCgGetProgramStringFn)(void *, int);
typedef void (__cdecl *NvscCgDestroyProgramFn)(void *);
typedef const char *(__cdecl *NvscCgGetLastListingFn)(void *);

static INIT_ONCE s_cgOnce = INIT_ONCE_STATIC_INIT;
static CRITICAL_SECTION s_cgLock;
static void *s_cgContext;
static NvscCgCreateProgramFn s_cgCreateProgram;
static NvscCgGetProgramStringFn s_cgGetProgramString;
static NvscCgDestroyProgramFn s_cgDestroyProgram;
static NvscCgGetLastListingFn s_cgGetLastListing;

typedef struct NvscCacheEntry
{
    struct NvscCacheEntry *next;
    char *key;
    char *program;
} NvscCacheEntry;

static NvscCacheEntry *s_cache;

static BOOL CALLBACK nvsc_loadCg(PINIT_ONCE once, PVOID parameter, PVOID *context)
{
    (void)once;
    (void)parameter;
    (void)context;
    InitializeCriticalSection(&s_cgLock);

    HMODULE cg = GetModuleHandleA("cg.dll");
    if (!cg)
        cg = LoadLibraryA("cg.dll");
    if (!cg)
    {
        log_error(plfNvShader, "nvShader: cg.dll is not installed; NVIDIA-only shaders cannot be converted");
        return TRUE;
    }
    NvscCgCreateContextFn createContext = (NvscCgCreateContextFn)(void *)GetProcAddress(cg, "cgCreateContext");
    s_cgCreateProgram = (NvscCgCreateProgramFn)(void *)GetProcAddress(cg, "cgCreateProgram");
    s_cgGetProgramString = (NvscCgGetProgramStringFn)(void *)GetProcAddress(cg, "cgGetProgramString");
    s_cgDestroyProgram = (NvscCgDestroyProgramFn)(void *)GetProcAddress(cg, "cgDestroyProgram");
    s_cgGetLastListing = (NvscCgGetLastListingFn)(void *)GetProcAddress(cg, "cgGetLastListing");
    if (createContext && s_cgCreateProgram && s_cgGetProgramString && s_cgDestroyProgram)
        s_cgContext = createContext();
    if (!s_cgContext)
        log_error(plfNvShader, "nvShader: cg.dll has no usable compiler entry points");
    return TRUE;
}

/* Copies the rest of the comment line that starts with `prefix`, trimmed. */
static bool nvsc_headerValue(const char *src, size_t len, const char *prefix, char *out, size_t outSize)
{
    size_t pl = strlen(prefix);
    const char *end = src + len;
    for (const char *line = src; line < end;)
    {
        const char *eol = memchr(line, '\n', (size_t)(end - line));
        if (!eol)
            eol = end;
        if ((size_t)(eol - line) > pl && !memcmp(line, prefix, pl))
        {
            const char *v = line + pl, *ve = eol;
            while (v < ve && (*v == ' ' || *v == '\t'))
                v++;
            while (ve > v && (ve[-1] == '\r' || ve[-1] == ' ' || ve[-1] == '\t'))
                ve--;
            if (ve == v || (size_t)(ve - v) >= outSize)
                return false;
            memcpy(out, v, (size_t)(ve - v));
            out[ve - v] = '\0';
            return true;
        }
        line = eol + 1;
    }
    return false;
}

static bool nvsc_isNvAssembly(const char *src, size_t len, bool *vertex)
{
    if (len < 10)
        return false;
    if (!memcmp(src, "!!ARBvp1.0", 10))
        *vertex = true;
    else if (!memcmp(src, "!!ARBfp1.0", 10))
        *vertex = false;
    else
        return false;

    const char *option = *vertex ? "OPTION NV_vertex_program" : "OPTION NV_fragment_program";
    size_t ol = strlen(option);
    /* The option follows the header line. */
    size_t scan = len < 256 ? len : 256;
    for (size_t i = 10; i + ol <= scan; i++)
        if (!memcmp(src + i, option, ol))
            return true;
    return false;
}

static char *nvsc_readSource(const char *file, char *found, size_t foundSize)
{
    static const char *const dirs[] = {"", "data/shader/", "data/graphic/"};
    const char *base = file;
    for (const char *p = file; *p; p++)
        if (*p == '/' || *p == '\\')
            base = p + 1;

    for (size_t i = 0; i < sizeof(dirs) / sizeof(dirs[0]); i++)
    {
        char path[MAX_PATH];
        snprintf(path, sizeof(path), "%s%s", dirs[i], i == 0 ? file : base);
        size_t size = 0;
        char *text = readWholeFile(path, &size);
        if (text)
        {
            snprintf(found, foundSize, "%s", path);
            return text;
        }
    }
    return NULL;
}

char *nvShader_recompileNvAssembly(const char *src, size_t len, bool envConstants)
{
    bool vertex;
    if (!src || !nvsc_isNvAssembly(src, len, &vertex))
        return NULL;

    char file[MAX_PATH], entry[128];
    if (!nvsc_headerValue(src, len, "# source file:", file, sizeof(file)) ||
        !nvsc_headerValue(src, len, "#program", entry, sizeof(entry)))
    {
        log_warn(plfNvShader, "nvShader: NVIDIA-only %s program has no cgc source header; left as is",
                 vertex ? "vertex" : "fragment");
        return NULL;
    }

    InitOnceExecuteOnce(&s_cgOnce, nvsc_loadCg, NULL, NULL);
    if (!s_cgContext)
        return NULL;

    char key[MAX_PATH + 160];
    snprintf(key, sizeof(key), "%c|%d|%s|%s", vertex ? 'v' : 'f', envConstants ? 1 : 0, file, entry);

    EnterCriticalSection(&s_cgLock);
    for (NvscCacheEntry *e = s_cache; e; e = e->next)
    {
        if (!strcmp(e->key, key))
        {
            char *copy = e->program ? strdup(e->program) : NULL;
            LeaveCriticalSection(&s_cgLock);
            return copy;
        }
    }

    char *result = NULL;
    char path[MAX_PATH];
    char *source = nvsc_readSource(file, path, sizeof(path));
    if (!source)
    {
        log_warn(plfNvShader, "nvShader: source %s of an NVIDIA-only %s program not found", file,
                 vertex ? "vertex" : "fragment");
    }
    else
    {
        int profile = vertex ? NVSC_CG_PROFILE_ARBVP1 : NVSC_CG_PROFILE_ARBFP1;
        void *program = s_cgCreateProgram(s_cgContext, NVSC_CG_SOURCE, source, profile, entry, NULL);
        const char *compiled = program ? s_cgGetProgramString(program, NVSC_CG_COMPILED_PROGRAM) : NULL;
        if (!compiled || !compiled[0])
        {
            const char *listing = s_cgGetLastListing ? s_cgGetLastListing(s_cgContext) : NULL;
            log_error(plfNvShader, "nvShader: %s(%s) does not compile for %s: %s", path, entry,
                      vertex ? "arbvp1" : "arbfp1", listing ? listing : "(no listing)");
        }
        else
        {
            /* Undo the Initial D semantic patch if Cg.dll carries it. */
            static const Rule kVertexInputs[] = {
                {"vertex.attrib[0]", "vertex.position"},
                {"vertex.attrib[2]", "vertex.normal"},
                {"vertex.attrib[3]", "vertex.color"},
                {"vertex.attrib[4]", "vertex.color.secondary"},
                {"vertex.attrib[5]", "vertex.fogcoord"},
                {"vertex.attrib[8]", "vertex.texcoord[0]"},
                {"vertex.attrib[9]", "vertex.texcoord[1]"},
                {"vertex.attrib[10]", "vertex.texcoord[2]"},
                {"vertex.attrib[11]", "vertex.texcoord[3]"},
                {"vertex.attrib[12]", "vertex.texcoord[4]"},
                {"vertex.attrib[13]", "vertex.texcoord[5]"},
                {"vertex.attrib[14]", "vertex.texcoord[6]"},
                {"vertex.attrib[15]", "vertex.texcoord[7]"},
            };
            /* Constants go where glHooks sends the uploads. */
            static const Rule kToLocal[] = {
                {"program .local", "program.local"},
                {"program.env", "program.local"},
            };
            static const Rule kToEnv[] = {
                {"program .local", "program.env"},
                {"program.local", "program.env"},
            };
            size_t clen = strlen(compiled);
            char *step = vertex ? applyRules(compiled, clen, kVertexInputs,
                                             sizeof(kVertexInputs) / sizeof(kVertexInputs[0]))
                                : strdup(compiled);
            if (step)
            {
                result = envConstants ? applyRules(step, strlen(step), kToEnv, 2)
                                      : applyRules(step, strlen(step), kToLocal, 2);
                free(step);
            }
            if (result)
                log_info(plfNvShader, "nvShader: %s(%s) rebuilt from NVIDIA-only assembly as %s", path, entry,
                         vertex ? "arbvp1" : "arbfp1");
        }
        if (program)
            s_cgDestroyProgram(program);
        free(source);
    }

    /* Failures too, so each is logged once. */
    NvscCacheEntry *e = malloc(sizeof(*e));
    if (e)
    {
        e->key = strdup(key);
        e->program = result ? strdup(result) : NULL;
        e->next = s_cache;
        s_cache = e;
    }
    LeaveCriticalSection(&s_cgLock);
    return result;
}