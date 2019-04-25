#include "common.h"
#include "hs.h"


HttpInfo_t g_httpInfo;

typedef struct MatchContext
{
    HttpInfo_t *httpInfo;
    char *data;
    uint32_t dataLen;
    int anchorId;
    int isHttp;
    uint16_t splitOffset;
} MatchContext_t;

enum PatternType
{
    PTN_ANCHOR,
    PTN_NEWLINE,
    PTN_HTTPEND
};

typedef struct AnchorPattern
{
    const char *regex;
    int type;
    int anchorId;
} AnchorPattern_t;

const char *HTTPSPLIT_PATTERNS[] = {
    "^GET ",
    "^POST ",
    "^Head ",
    "^HTTP/1\\.0",
    "^HTTP/1\\.1",
    "^HTTP/0\\.",
    "^PUT",
    "^Delete",
    "^trace",
    "^Options",
    "^Connect",
    "^Patch"};

// clang-format off
#define HTTP_USERAGENT          "User-Agent"
#define HTTP_CONTENTTYPE        "Content-Type"
#define HTTP_HOST               "Host"
#define HTTP_XONLINEHOST        "X-Online-Host"
#define HTTP_HTTPEND            "\\r\\n\\r\\n"
#define HTTP_REFERER            "Referer"
#define HTTP_SERVER             "Server"
#define HTTP_COOKIE             "Cookie"
#define HTTP_CONTENTLENGTH      "Content-Length"
#define HTTP_CONNECTION         "Connection"
#define HTTP_XREQWIDTH          "X-Requested-With"
#define HTTP_TRANSENCODE        "Transfer-Encoding"
#define HTTP_CONTENTENCODING    "Content-Encoding"

#define HTTP_NEWLINE    "\\n"


const AnchorPattern_t HTTPANCHOR_PATTERNS[] = {
    {HTTP_USERAGENT,     PTN_ANCHOR,    1},
    {HTTP_CONTENTTYPE,   PTN_ANCHOR,    2},
    {HTTP_HOST,          PTN_ANCHOR,    4},
    {HTTP_REFERER,       PTN_ANCHOR,    7},
    {HTTP_SERVER,        PTN_ANCHOR,    8},
    {HTTP_COOKIE,        PTN_ANCHOR,    9},
    {HTTP_CONTENTLENGTH, PTN_ANCHOR,    10},
    {HTTP_CONNECTION,    PTN_ANCHOR,    11},
    {HTTP_NEWLINE,       PTN_NEWLINE,   -1},
    {HTTP_HTTPEND,       PTN_HTTPEND,   -1}
};
// clang-format on

hs_database_t *g_splitDb = NULL;
hs_database_t *g_anchorDb = NULL;
hs_scratch_t *g_scratch = NULL;

static hs_database_t *compileHTTPSplitDb(void)
{
    hs_database_t *db = NULL;
    uint32_t i, n;
    const char *exps[16];
    uint32_t flags[16];
    uint32_t ids[16];
    int ret;
    hs_compile_error_t *hsErr;

    n = sizeof(HTTPSPLIT_PATTERNS) / sizeof(HTTPSPLIT_PATTERNS[0]);

    for (i = 0; i < n; i++)
    {
        exps[i] = HTTPSPLIT_PATTERNS[i];
        flags[i] = HS_FLAG_CASELESS | HS_FLAG_DOTALL;
        ids[i] = i;
    }

    ret = hs_compile_multi(exps, flags, ids, n, HS_MODE_BLOCK, NULL, &db, &hsErr);
    if (ret != HS_SUCCESS)
    {
        if (hsErr->expression < 0)
            printf("compile httpSplitDb failed: %s\n", hsErr->message);
        else
            printf("compile httpSplitDb failed: pattern %s with error %s\n",
                   exps[hsErr->expression], hsErr->message);
        hs_free_compile_error(hsErr);
        return NULL;
    }

    return db;
}

static hs_database_t *compileHTTPAnchorDb(void)
{
    hs_database_t *db = NULL;
    uint32_t i, n;
    const char *exps[16];
    uint32_t flags[16];
    uint32_t ids[16];
    int ret;
    hs_compile_error_t *hsErr;

    n = sizeof(HTTPANCHOR_PATTERNS) / sizeof(HTTPANCHOR_PATTERNS[0]);

    for (i = 0; i < n; i++)
    {
        exps[i] = HTTPANCHOR_PATTERNS[i].regex;
#ifdef DEBUG
        printf("compile anchor: %s\n", exps[i]);
#endif
        flags[i] = HS_FLAG_CASELESS | HS_FLAG_DOTALL;
        ids[i] = i;
    }

    ret = hs_compile_multi(exps, flags, ids, n, HS_MODE_BLOCK, NULL, &db, &hsErr);
    if (ret != HS_SUCCESS)
    {
        if (hsErr->expression < 0)
            printf("compile httpAnchorDb failed: %s\n", hsErr->message);
        else
            printf("compile httpAnchorDb failed: pattern %s with error %s\n",
                   exps[hsErr->expression], hsErr->message);
        hs_free_compile_error(hsErr);
        return NULL;
    }

    return db;
}

int httpSplitCallback(unsigned int id, unsigned long long from,
                      unsigned long long to, unsigned int flags, void *ctx)
{
    MatchContext_t *p = (MatchContext_t *)ctx;

#ifdef DEBUG
    printf("splitCallback: match %s\n", HTTPSPLIT_PATTERNS[id]);
#endif

    p->isHttp = 1;
    p->splitOffset = to;
    return 1;
}

int httpAnchorCallback(unsigned int id, unsigned long long from,
                       unsigned long long to, unsigned int flags, void *ctx)
{
    MatchContext_t *p = (MatchContext_t *)ctx;
    const AnchorPattern_t *ptn = &HTTPANCHOR_PATTERNS[id];

#ifdef DEBUG
    printf("anchorCallback: match %s\n", ptn->regex);
#endif

    if(likely(ptn->type == PTN_ANCHOR)) {
        while (to < (p->dataLen - 1))
        {
            if (*(p->data + to) != 0x20)
                break;
            to++;
        }
        if (to < p->dataLen - 1)
        {
            p->anchorId = ptn->anchorId;
            p->httpInfo->StrLen[p->anchorId] = 0;
            p->httpInfo->String[p->anchorId] = (char *)p->data + to;
        }
        else
            return 1;
    } else if(ptn->type == PTN_NEWLINE) {
        if (p->anchorId == 0)
        {
            p->httpInfo->StrLen[p->anchorId] =
                (to > p->splitOffset + 10) ? (to - p->splitOffset - 10) : 0;
            if ((p->httpInfo->StrLen[p->anchorId] > 0) &&
                (' ' == *(p->httpInfo->String[p->anchorId] +
                          p->httpInfo->StrLen[p->anchorId] - 1)))
                p->httpInfo->StrLen[p->anchorId]--;
        }
        else if (p->anchorId != -1)
        {
            p->httpInfo->StrLen[p->anchorId] =
                p->data + to - 1 - p->httpInfo->String[p->anchorId];
            if ((p->httpInfo->StrLen[p->anchorId] > 0) &&
                ('\r' == *(p->httpInfo->String[p->anchorId] +
                           p->httpInfo->StrLen[p->anchorId] - 1)))
                p->httpInfo->StrLen[p->anchorId]--;
        }
        p->anchorId = -1;
    } else if(ptn->type == PTN_HTTPEND) {
        p->httpInfo->String[HTTP_ANCHOR_HTTPEND] = p->data + to;
        p->httpInfo->StrLen[HTTP_ANCHOR_HTTPEND] = p->dataLen - to;
        p->anchorId = -1;
        return 1;
    }

    return 0;
}

int parseHttp(char *data, uint32_t dataLen, HttpInfo_t *httpInfo)
{
    int ret;
    MatchContext_t ctx;
    uint32_t len;

    ctx.data = data;
    ctx.dataLen = dataLen;
    ctx.httpInfo = httpInfo;
    ctx.anchorId = 0;

#define SPLIT_MATCH_MAX 10

    len = (dataLen > SPLIT_MATCH_MAX) ? SPLIT_MATCH_MAX : dataLen;

    ret = hs_scan(g_splitDb, data, len, 0, g_scratch, httpSplitCallback, &ctx);
    if (ret != HS_SUCCESS && ret != HS_SCAN_TERMINATED)
        return ret;

    if (ctx.isHttp && ctx.splitOffset < (dataLen - 1))
    {
        httpInfo->IsHttpPacket = 1;
        httpInfo->String[0] = data + ctx.splitOffset;
    }
    else
    {
        httpInfo->IsHttpPacket = 0;
        return 0;
    }

    ctx.anchorId = 0;

    ret = hs_scan(g_anchorDb, data, dataLen, 0, g_scratch, httpAnchorCallback, &ctx);
    if (ret != HS_SUCCESS && ret != HS_SCAN_TERMINATED)
        return ret;

    return ret;
}

int main(int argc, char **argv)
{
    int ret;
    char *filename;
    FILE *file;
    long file_length;
    char *data;
    uint64_t start, stop, total = 0;
    uint32_t i, N = 0;

    if (argc != 3)
    {
        printf("usage: %s <file> <loop count>\n", argv[0]);
        return -1;
    }

    filename = argv[1];
    file = fopen(filename, "r");
    if (file == NULL)
    {
        perror("fopen");
        goto fail;
    }

    N = atoi(argv[2]);
    if (N == 0 || N > INT_MAX)
    {
        printf("loop count %u is invalid\n", N);
        goto fail;
    }

    fseek(file, 0, SEEK_END);
    file_length = ftell(file);
    if (file_length == -1)
    {
        perror("ftell");
        goto fail;
    }
    fseek(file, 0, SEEK_SET);

    data = malloc(file_length);
    if (fread(data, 1, file_length, file) != (size_t)file_length)
    {
        fprintf(stderr, "couldn't read entire file\n");
        free(data);
        goto fail;
    }

    g_splitDb = compileHTTPSplitDb();
    if (g_splitDb == NULL)
        goto fail;
    g_anchorDb = compileHTTPAnchorDb();
    if (g_anchorDb == NULL)
        goto fail;
    ret = hs_alloc_scratch(g_splitDb, &g_scratch);
    if (ret != HS_SUCCESS)
        goto fail;
    ret = hs_alloc_scratch(g_anchorDb, &g_scratch);
    if (ret != HS_SUCCESS)
        goto fail;

    for (i = 0; i < N; i++)
    {
        start = rte_rdtsc();
        ResetHttpInfoAnchors(&g_httpInfo);
        parseHttp(data, file_length, &g_httpInfo);
#ifdef DEBUG
        PrintHttpInfoAnchors(&g_httpInfo);
#endif
        stop = rte_rdtsc();
        total += stop - start;
        usleep(1000);
    }
    

    printf(FMT64 " cycles\n", total);

    free(data);

    return 0;

fail:
    fclose(file);
    return EXIT_FAILURE;
}