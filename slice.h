#ifndef SLICE_H
#define SLICE_H
#include "tprintf.h"
#include <stdint.h>
#include <sys/types.h>

typedef struct {
    uint8_t const* ptr;
    size_t len;
} slice;

typedef struct {
    uint8_t* ptr;
    size_t len;
} mutslice;

void print_slice(slice s);

#endif
