#pragma once
#include <stddef.h>
#include <stdint.h>
#include <windows.h>

struct mallinfo
{
    int arena;     
    int ordblks;   
    int smblks;    
    int hblks;    
    int hblkhd;    
    int usmblks;   
    int fsmblks;   
    int uordblks;  
    int fordblks;  
    int keepcost;  
};

void *allocate_executable(size_t size, void *requestedAddr);
void *reserve_address_space(size_t size, void *requestedAddr);

void  *hook_malloc(size_t size);
struct mallinfo hook_mallinfo(void);
void  *hook_calloc(size_t nmemb, size_t size);
void  *hook_realloc(void *ptr, size_t size);
void   hook_free(void *ptr);
void  *hook_memalign(size_t alignment, size_t size);
int    hook_posix_memalign(void **ptr, size_t alignment, size_t size);
char  *hook_strndup(const char *src, size_t size);
char  *hook_strdup(const char *s);
