#include "common.h"
#include "http_parser.h"


HttpInfo_t g_httpInfo;

int on_headers_complete(http_parser *_)
{
  (void)_;
  return 1;
}

int on_url(http_parser *_, const char *at, size_t length)
{
  (void)_;
#ifdef DEBUG
  printf("Url: %.*s\n", (int)length, at);
#endif
  g_httpInfo.String[0] = at;
  g_httpInfo.StrLen[0] = length;
  return 0;
}

int on_header_field(http_parser *parser, const char *at, size_t length)
{
#ifdef DEBUG
  printf("Header field: %.*s\n", (int)length, at);
#endif
  // if(parser->anchor_id != -1) {
  //   g_httpInfo.String[parser->anchor_id] = at;
  // }

  return 0;
}

int on_header_value(http_parser *parser, const char *at, size_t length)
{
#ifdef DEBUG
  printf("Header value: %.*s\n", (int)length, at);
#endif
  if (parser->anchor_id != -1)
  {
    g_httpInfo.String[parser->anchor_id] = at;
    g_httpInfo.StrLen[parser->anchor_id] = length;
  }

  return 0;
}

int main(int argc, char *argv[])
{
  http_parser_settings settings;
  http_parser parser;
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

  memset(&settings, 0, sizeof(settings));
  settings.on_url = on_url;
  //settings.on_header_field = on_header_field;
  settings.on_header_value = on_header_value;
  settings.on_headers_complete = on_headers_complete;

  for (i = 0; i < N; i++)
  {
    start = rte_rdtsc();
    ResetHttpInfoAnchors(&g_httpInfo);
    http_parser_init(&parser, HTTP_REQUEST);
    http_parser_execute(&parser, &settings, data, file_length);
#ifdef DEBUG
    PrintHttpInfoAnchors(&g_httpInfo);
#endif
    stop = rte_rdtsc();
    total += stop - start;
    usleep(1000); 
  }

  printf(FMT64 " cycles\n", total);

  free(data);

  // if (nparsed != (size_t)file_length) {
  //   fprintf(stderr,
  //           "Error: %s (%s)\n",
  //           http_errno_description(HTTP_PARSER_ERRNO(&parser)),
  //           http_errno_name(HTTP_PARSER_ERRNO(&parser)));
  //   goto fail;
  // }

  return EXIT_SUCCESS;

fail:
  fclose(file);
  return EXIT_FAILURE;
}
