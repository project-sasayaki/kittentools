#pragma once

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    void *key;
    void *value;
} PtrMapEntry;

typedef struct {
    PtrMapEntry *entries;
    size_t       capacity;   /* always a power of two */
    size_t       count;      /* number of live entries (key != NULL) */
} PtrMap;

typedef struct {
    char  *key;
    size_t keyLen;
    void  *value;
} StrMapEntry;

typedef struct {
    StrMapEntry *entries;
    size_t       capacity;
    size_t       count;
} StrMap;

typedef struct {
    int32_t key;
    void   *value;
    uint8_t occupied;
} IntMapEntry;

typedef struct {
    IntMapEntry *entries;
    size_t       capacity;
    size_t       count;
} IntMap;

typedef struct {
    void **items;
    size_t count;
    size_t capacity;
} PtrList;

typedef struct {
    char **items;
    size_t count;
    size_t capacity;
} StrList;


void  ptrMap_Init(PtrMap *m);
void  ptrMap_Destroy(PtrMap *m);

void *ptrMap_Set(PtrMap *m, void *key, void *value);

void *ptrMap_Get(const PtrMap *m, void *key);

int   ptrMap_Remove(PtrMap *m, void *key);
void  ptrMap_Clear(PtrMap *m);

typedef void (*PtrMapCallback)(void *key, void *value, void *userdata);
void  ptrMap_ForEach(const PtrMap *m, PtrMapCallback cb, void *userdata);

void  strMap_Init(StrMap *m);
void *strMap_Set(StrMap *m, const char *key, void *value);
void *strMap_SetWithLen(StrMap *m, const char *key, size_t keyLen, void *value);
void *strMap_Get(const StrMap *m, const char *key);
void *strMap_GetWithLen(const StrMap *m, const char *key, size_t keyLen);
int   strMap_Has(const StrMap *m, const char *key);
int   strMap_Remove(StrMap *m, const char *key);

void  intMap_Init(IntMap *m);
void *intMap_Set(IntMap *m, int32_t key, void *value);
void *intMap_Get(const IntMap *m, int32_t key);
int   intMap_Remove(IntMap *m, int32_t key);
void  intMap_Clear(IntMap *m);
typedef void (*IntMapCallback)(int32_t key, void *value, void *userdata);
void  intMap_ForEach(const IntMap *m, IntMapCallback cb, void *userdata);

void   ptrList_Init(PtrList *l);
void   ptrList_Destroy(PtrList *l);
void   ptrList_Append(PtrList *l, void *item);

ptrdiff_t ptrList_Find(const PtrList *l, void *item);

void   strList_Init(StrList *l);
void   strList_Append(StrList *l, const char *s);
void   strList_Clear(StrList *l);