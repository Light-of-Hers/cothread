#ifndef __CRZ_COTHRD_UTIL_H__
#define __CRZ_COTHRD_UTIL_H__

#include <malloc.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define STR_IMPL(_x) #_x
#define STR(_x) STR_IMPL(_x)

#define log(args...) fprintf(stderr, __FILE__ ":" STR(__LINE__) ":" args)
#define log_hex(_x) log("(" #_x "): %08lx\n", (uint64_t)(_x))

#define DEBUG

#ifdef DEBUG
#define debug(args...) log(args)
#else
#define debug(args...) (void)0
#endif

#define panic(args...)       \
    do {                     \
        log("panic: " args); \
        exit(1);             \
    } while (0)

static inline uint64_t
round_down(uint64_t x, uint64_t f)
{
    return (x / f) * f;
}

static inline uint64_t
round_up(uint64_t x, uint64_t f)
{
    return ((x + f - 1) / f) * f;
}

#define OUT_OF_MEM_MSG "不行啊，人家……人家已经被塞满了……\n"

static inline void*
my_malloc(size_t size)
{
    void* ptr = malloc(size);
    if (!ptr)
        panic(OUT_OF_MEM_MSG);
    return ptr;
}

static inline void*
my_calloc(size_t nmemb, size_t size)
{
    void* ptr = calloc(nmemb, size);
    if (!ptr)
        panic(OUT_OF_MEM_MSG);
    return ptr;
}

static inline void*
my_realloc(void* ptr, size_t size)
{
    ptr = realloc(ptr, size);
    if (!ptr)
        panic(OUT_OF_MEM_MSG);
    return ptr;
}

#undef OUT_OF_MEM_MSG


#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define INF 0x7f7f7f7f

#endif