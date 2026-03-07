CC = gcc
CFLAGS = -std=c99 -Wall -Werror -Wno-unused-function -Wno-unused-variable -pedantic -D_POSIX_C_SOURCE=200809L
TARGETS = bfind

# Use bfind.c if it exists (renamed skeleton), otherwise bfind_skeleton.c
SRC = $(shell if [ -f bfind.c ]; then echo bfind.c; else echo bfind_skeleton.c; fi)

all: $(TARGETS)

bfind: $(SRC) queue.c queue.h
	$(CC) $(CFLAGS) -o $@ $(SRC) queue.c

clean:
	rm -f $(TARGETS)

test: bfind
	python3 test_bfind_basic.py

.PHONY: all clean test
