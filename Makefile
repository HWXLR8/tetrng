CC ?= cc
CFLAGS ?= -O3 -march=native -std=c11 -Wall -Wextra -Wpedantic
OPENMP ?= -fopenmp

.PHONY: all clean

all: tetrng

tetrng: tetrng.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(OPENMP) $< -o $@ $(LDFLAGS) $(OPENMP)

clean:
	$(RM) tetrng
