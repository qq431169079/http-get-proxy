#include "slice.h"
#include <stdio.h>

void print_slice(slice s) {
    tprintf("[%.*s]", (int)(s.len), s.ptr);
}
