

CC = gcc
CFLAGS = -Wall -g -O0
LDFLAGS = 


all: perf_test perf_test_hs


perf_test:
	$(CC) $(CFLAGS) -o $@ http_parser.c perf_test.c

perf_test_hs:
	$(CC) $(CFLAGS) -I/usr/local/include/hs -o $@ perf_test_hs.c -L. -lhs

clean:
	rm -rf perf_test perf_test_hs
