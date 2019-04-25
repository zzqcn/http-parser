#ifndef __PARSER_COMMON_H__
#define __PARSER_COMMON_H__

#include <unistd.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>

#ifdef __cplusplus
extern "C" {
#endif

/** C extension macro for environments lacking C11 features. */
#if !defined(__STDC_VERSION__) || __STDC_VERSION__ < 201112L
#define RTE_STD_C11 __extension__
#else
#define RTE_STD_C11
#endif

#ifdef __APPLE__
#define FMT64 "%llu"
#else
#define FMT64 "%lu"
#endif

#ifndef likely
#define likely(x)	__builtin_expect(!!(x), 1)
#endif /* likely */

#ifndef unlikely
#define unlikely(x)	__builtin_expect(!!(x), 0)
#endif /* unlikely */


static inline uint64_t rte_rdtsc(void)
{
    union {
        uint64_t tsc_64;
        RTE_STD_C11
            struct {
                uint32_t lo_32;
                uint32_t hi_32;
            };
    } tsc;

    asm volatile("rdtsc" :
            "=a" (tsc.lo_32),
            "=d" (tsc.hi_32));
    return tsc.tsc_64;
}

/** URI */
#define HTTP_ANCHOR_URI             0
/** User-Agent */
#define HTTP_ANCHOR_USERAGENT       1
/** Content-Type */
#define HTTP_ANCHOR_CONTENTTYPE     2
/** Host */
#define HTTP_ANCHOR_HOST            4
/** X-Online-Host */
#define HTTP_ANCHOR_XONLINEHOST     5
/** \r\n\r\n */
#define HTTP_ANCHOR_HTTPEND         6
/** Referer */
#define HTTP_ANCHOR_REFERER         7
/** Server */
#define HTTP_ANCHOR_SERVER          8
/** Cookie */
#define HTTP_ANCHOR_COOKIE          9
/** Content-Length */
#define HTTP_ANCHOR_CONTENTLENGTH   10
/** Connection */
#define HTTP_ANCHOR_CONNECTION      11
/** X-Requested-With */
#define HTTP_ANCHOR_XREQWITH        12
/** Transfer-Encoding */
#define HTTP_ANCHOR_TRANSENCODE     13
/** Content-Encoding */
#define HTTP_ANCHOR_CONTENTENCODING 14
#define HTTP_ANCHOR_MAXCNT          16


typedef struct HttpInfo
{
    uint32_t IsHttpPacket : 1;
    uint32_t IsHttpsPacket : 1;
    uint32_t HttpSplitId : 6;
    uint32_t HttpType : 3;
    uint32_t IsUri : 1;
    uint32_t IsParse : 1;
    uint32_t IsCompleteHead : 1;
    uint32_t Reserved : 18;
    int StrLen[HTTP_ANCHOR_MAXCNT];
    const char* String[HTTP_ANCHOR_MAXCNT];
} HttpInfo_t;

static inline void ResetHttpInfoAnchors(HttpInfo_t* p)
{
    int i;
    for(i=0; i<HTTP_ANCHOR_MAXCNT; i++)
        p->StrLen[i] = 0;
}

static inline void PrintHttpInfoAnchors(HttpInfo_t* p)
{
    int i;
    for(i=0; i<HTTP_ANCHOR_MAXCNT; i++) {
        if(p->StrLen[i] != 0)
            printf("  %u: %.*s\n", i, p->StrLen[i], p->String[i]);
    }
}



#ifdef __cplusplus
}
#endif

#endif