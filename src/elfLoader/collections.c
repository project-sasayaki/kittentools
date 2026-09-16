#include "collections.h"
#include <stdlib.h>
#include <string.h>

static inline uint32_t fnv1a(const void *data, size_t len)
{
    const unsigned char *p = (const unsigned char *)data;
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < len; ++i)
    {
        h ^= p[i];
        h *= 16777619u;
    }
    return h;
}

void ptrMap_Init(PtrMap *m)
{
    m->entries  = NULL;
    m->capacity = 0;
    m->count    = 0;
}

void ptrMap_Destroy(PtrMap *m)
{
    free(m->entries);
    m->entries  = NULL;
    m->capacity = 0;
    m->count    = 0;
}

void ptrMap_Clear(PtrMap *m)
{
    if (m->entries)
        memset(m->entries, 0, m->capacity * sizeof(PtrMapEntry));
    m->count = 0;
}

static void ptrMap_Rehash(PtrMap *m, size_t newCap)
{
    PtrMapEntry *old     = m->entries;
    size_t       oldCap  = m->capacity;

    PtrMapEntry *fresh = (PtrMapEntry *)calloc(newCap, sizeof(PtrMapEntry));
    if (!fresh)
        return; /* allocation failure: keep old table */

    for (size_t i = 0; i < oldCap; ++i)
    {
        if (old[i].key != NULL)
        {
            size_t mask  = newCap - 1;
            size_t slot  = fnv1a(&old[i].key, sizeof(void *)) & mask;
            while (fresh[slot].key != NULL)
                slot = (slot + 1) & mask;
            fresh[slot] = old[i];
        }
    }

    free(old);
    m->entries  = fresh;
    m->capacity = newCap;
}

void *ptrMap_Set(PtrMap *m, void *key, void *value)
{
    if (key == NULL)
        return NULL; /* NULL key reserved for "empty slot" */

    if (m->capacity == 0)
        ptrMap_Rehash(m, 16);
    else if ((m->count + 1) * 4 > m->capacity * 3) /* load > 0.75 */
        ptrMap_Rehash(m, m->capacity * 2);

    size_t mask = m->capacity - 1;
    size_t slot = fnv1a(&key, sizeof(void *)) & mask;

    while (m->entries[slot].key != NULL)
    {
        if (m->entries[slot].key == key)
        {
            void *oldVal = m->entries[slot].value;
            m->entries[slot].value = value;
            return oldVal;
        }
        slot = (slot + 1) & mask;
    }

    m->entries[slot].key   = key;
    m->entries[slot].value = value;
    m->count++;
    return NULL;
}

void *ptrMap_Get(const PtrMap *m, void *key)
{
    if (key == NULL || m->capacity == 0)
        return NULL;

    size_t mask = m->capacity - 1;
    size_t slot = fnv1a(&key, sizeof(void *)) & mask;

    while (m->entries[slot].key != NULL)
    {
        if (m->entries[slot].key == key)
            return m->entries[slot].value;
        slot = (slot + 1) & mask;
    }
    return NULL;
}

int ptrMap_Remove(PtrMap *m, void *key)
{
    if (key == NULL || m->capacity == 0)
        return 0;

    size_t mask = m->capacity - 1;
    size_t slot = fnv1a(&key, sizeof(void *)) & mask;

    while (m->entries[slot].key != NULL)
    {
        if (m->entries[slot].key == key)
        {
            /* Tombstone-free removal via backward shift */
            m->entries[slot].key   = NULL;
            m->entries[slot].value = NULL;
            m->count--;

            size_t next = (slot + 1) & mask;
            while (m->entries[next].key != NULL)
            {
                void *nk = m->entries[next].key;
                void *nv = m->entries[next].value;
                m->entries[next].key   = NULL;
                m->entries[next].value = NULL;

                size_t ns = fnv1a(&nk, sizeof(void *)) & mask;
                while (m->entries[ns].key != NULL)
                    ns = (ns + 1) & mask;
                m->entries[ns].key   = nk;
                m->entries[ns].value = nv;

                next = (next + 1) & mask;
            }
            return 1;
        }
        slot = (slot + 1) & mask;
    }
    return 0;
}

void ptrMap_ForEach(const PtrMap *m, PtrMapCallback cb, void *userdata)
{
    if (!cb || m->capacity == 0)
        return;
    for (size_t i = 0; i < m->capacity; ++i)
    {
        if (m->entries[i].key != NULL)
            cb(m->entries[i].key, m->entries[i].value, userdata);
    }
}

void strMap_Init(StrMap *m)
{
    m->entries  = NULL;
    m->capacity = 0;
    m->count    = 0;
}

static void strMap_Rehash(StrMap *m, size_t newCap)
{
    StrMapEntry *old    = m->entries;
    size_t       oldCap = m->capacity;

    StrMapEntry *fresh = (StrMapEntry *)calloc(newCap, sizeof(StrMapEntry));
    if (!fresh)
        return;

    for (size_t i = 0; i < oldCap; ++i)
    {
        if (old[i].key != NULL)
        {
            size_t mask = newCap - 1;
            size_t slot = fnv1a(old[i].key, old[i].keyLen) & mask;
            while (fresh[slot].key != NULL)
                slot = (slot + 1) & mask;
            fresh[slot] = old[i];
        }
    }

    free(old);
    m->entries  = fresh;
    m->capacity = newCap;
}

void *strMap_Set(StrMap *m, const char *key, void *value)
{
    if (!key)
        return NULL;
    return strMap_SetWithLen(m, key, strlen(key), value);
}

void *strMap_SetWithLen(StrMap *m, const char *key, size_t keyLen, void *value)
{
    if (!key)
        return NULL;

    if (m->capacity == 0)
        strMap_Rehash(m, 16);
    else if ((m->count + 1) * 4 > m->capacity * 3)
        strMap_Rehash(m, m->capacity * 2);

    size_t mask = m->capacity - 1;
    size_t slot = fnv1a(key, keyLen) & mask;

    while (m->entries[slot].key != NULL)
    {
        if (m->entries[slot].keyLen == keyLen &&
            memcmp(m->entries[slot].key, key, keyLen) == 0)
        {
            void *old = m->entries[slot].value;
            m->entries[slot].value = value;
            return old;
        }
        slot = (slot + 1) & mask;
    }

    char *dup = _strdup(key);
    if (!dup)
        return NULL;
    m->entries[slot].key    = dup;
    m->entries[slot].keyLen = keyLen;
    m->entries[slot].value  = value;
    m->count++;
    return NULL;
}

void *strMap_Get(const StrMap *m, const char *key)
{
    if (!key || m->capacity == 0)
        return NULL;
    return strMap_GetWithLen(m, key, strlen(key));
}

void *strMap_GetWithLen(const StrMap *m, const char *key, size_t keyLen)
{
    if (!key || m->capacity == 0)
        return NULL;

    size_t mask = m->capacity - 1;
    size_t slot = fnv1a(key, keyLen) & mask;

    while (m->entries[slot].key != NULL)
    {
        if (m->entries[slot].keyLen == keyLen &&
            memcmp(m->entries[slot].key, key, keyLen) == 0)
            return m->entries[slot].value;
        slot = (slot + 1) & mask;
    }
    return NULL;
}

int strMap_Has(const StrMap *m, const char *key)
{
    if (!key || m->capacity == 0)
        return 0;
    size_t keyLen = strlen(key);
    size_t mask = m->capacity - 1;
    size_t slot = fnv1a(key, keyLen) & mask;
    while (m->entries[slot].key != NULL)
    {
        if (m->entries[slot].keyLen == keyLen &&
            memcmp(m->entries[slot].key, key, keyLen) == 0)
            return 1;
        slot = (slot + 1) & mask;
    }
    return 0;
}

int strMap_Remove(StrMap *m, const char *key)
{
    if (!key || m->capacity == 0)
        return 0;

    size_t keyLen = strlen(key);
    size_t mask = m->capacity - 1;
    size_t slot = fnv1a(key, keyLen) & mask;

    while (m->entries[slot].key != NULL)
    {
        if (m->entries[slot].keyLen == keyLen &&
            memcmp(m->entries[slot].key, key, keyLen) == 0)
        {
            free(m->entries[slot].key);
            m->entries[slot].key    = NULL;
            m->entries[slot].keyLen = 0;
            m->entries[slot].value  = NULL;
            m->count--;

            size_t next = (slot + 1) & mask;
            while (m->entries[next].key != NULL)
            {
                char  *nk = m->entries[next].key;
                size_t nkl = m->entries[next].keyLen;
                void  *nv = m->entries[next].value;
                m->entries[next].key    = NULL;
                m->entries[next].keyLen = 0;
                m->entries[next].value  = NULL;

                size_t ns = fnv1a(nk, nkl) & mask;
                while (m->entries[ns].key != NULL)
                    ns = (ns + 1) & mask;
                m->entries[ns].key    = nk;
                m->entries[ns].keyLen = nkl;
                m->entries[ns].value  = nv;

                next = (next + 1) & mask;
            }
            return 1;
        }
        slot = (slot + 1) & mask;
    }
    return 0;
}

void intMap_Init(IntMap *m)
{
    m->entries  = NULL;
    m->capacity = 0;
    m->count    = 0;
}

void intMap_Clear(IntMap *m)
{
    if (m->entries)
    {
        for (size_t i = 0; i < m->capacity; ++i)
            m->entries[i].occupied = 0;
    }
    m->count = 0;
}

static void intMap_Rehash(IntMap *m, size_t newCap)
{
    IntMapEntry *old    = m->entries;
    size_t       oldCap = m->capacity;

    IntMapEntry *fresh = (IntMapEntry *)calloc(newCap, sizeof(IntMapEntry));
    if (!fresh)
        return;

    for (size_t i = 0; i < oldCap; ++i)
    {
        if (old[i].occupied)
        {
            size_t mask = newCap - 1;
            /* hash the int32_t bytes */
            uint32_t h = fnv1a(&old[i].key, sizeof(int32_t));
            size_t slot = h & mask;
            while (fresh[slot].occupied)
                slot = (slot + 1) & mask;
            fresh[slot] = old[i];
        }
    }

    free(old);
    m->entries  = fresh;
    m->capacity = newCap;
}

void *intMap_Set(IntMap *m, int32_t key, void *value)
{
    if (m->capacity == 0)
        intMap_Rehash(m, 16);
    else if ((m->count + 1) * 4 > m->capacity * 3)
        intMap_Rehash(m, m->capacity * 2);

    size_t mask = m->capacity - 1;
    uint32_t h = fnv1a(&key, sizeof(int32_t));
    size_t slot = h & mask;

    while (m->entries[slot].occupied)
    {
        if (m->entries[slot].key == key)
        {
            void *old = m->entries[slot].value;
            m->entries[slot].value = value;
            return old;
        }
        slot = (slot + 1) & mask;
    }

    m->entries[slot].key      = key;
    m->entries[slot].value    = value;
    m->entries[slot].occupied = 1;
    m->count++;
    return NULL;
}

void *intMap_Get(const IntMap *m, int32_t key)
{
    if (m->capacity == 0)
        return NULL;
    size_t mask = m->capacity - 1;
    uint32_t h = fnv1a(&key, sizeof(int32_t));
    size_t slot = h & mask;
    while (m->entries[slot].occupied)
    {
        if (m->entries[slot].key == key)
            return m->entries[slot].value;
        slot = (slot + 1) & mask;
    }
    return NULL;
}

int intMap_Remove(IntMap *m, int32_t key)
{
    if (m->capacity == 0)
        return 0;
    size_t mask = m->capacity - 1;
    uint32_t h = fnv1a(&key, sizeof(int32_t));
    size_t slot = h & mask;
    while (m->entries[slot].occupied)
    {
        if (m->entries[slot].key == key)
        {
            m->entries[slot].occupied = 0;
            m->entries[slot].value    = NULL;
            m->count--;

            size_t next = (slot + 1) & mask;
            while (m->entries[next].occupied)
            {
                int32_t nk = m->entries[next].key;
                void   *nv = m->entries[next].value;
                m->entries[next].occupied = 0;
                m->entries[next].value    = NULL;

                uint32_t nh = fnv1a(&nk, sizeof(int32_t));
                size_t ns = nh & mask;
                while (m->entries[ns].occupied)
                    ns = (ns + 1) & mask;
                m->entries[ns].key      = nk;
                m->entries[ns].value    = nv;
                m->entries[ns].occupied = 1;

                next = (next + 1) & mask;
            }
            return 1;
        }
        slot = (slot + 1) & mask;
    }
    return 0;
}

void intMap_ForEach(const IntMap *m, IntMapCallback cb, void *userdata)
{
    if (!cb || m->capacity == 0)
        return;
    for (size_t i = 0; i < m->capacity; ++i)
    {
        if (m->entries[i].occupied)
            cb(m->entries[i].key, m->entries[i].value, userdata);
    }
}

void ptrList_Init(PtrList *l)
{
    l->items    = NULL;
    l->count    = 0;
    l->capacity = 0;
}

void ptrList_Destroy(PtrList *l)
{
    free(l->items);
    l->items    = NULL;
    l->count    = 0;
    l->capacity = 0;
}

void ptrList_Append(PtrList *l, void *item)
{
    if (l->count >= l->capacity)
    {
        size_t newCap = (l->capacity == 0) ? 8 : l->capacity * 2;
        void **ni = (void **)realloc(l->items, newCap * sizeof(void *));
        if (!ni)
            return;
        l->items    = ni;
        l->capacity = newCap;
    }
    l->items[l->count++] = item;
}

ptrdiff_t ptrList_Find(const PtrList *l, void *item)
{
    for (size_t i = 0; i < l->count; ++i)
    {
        if (l->items[i] == item)
            return (ptrdiff_t)i;
    }
    return -1;
}

void strList_Init(StrList *l)
{
    l->items    = NULL;
    l->count    = 0;
    l->capacity = 0;
}

void strList_Append(StrList *l, const char *s)
{
    if (!s)
        return;
    if (l->count >= l->capacity)
    {
        size_t newCap = (l->capacity == 0) ? 8 : l->capacity * 2;
        char **ni = (char **)realloc(l->items, newCap * sizeof(char *));
        if (!ni)
            return;
        l->items    = ni;
        l->capacity = newCap;
    }
    l->items[l->count++] = _strdup(s);
}

void strList_Clear(StrList *l)
{
    for (size_t i = 0; i < l->count; ++i)
        free(l->items[i]);
    l->count = 0;
}
