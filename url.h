#ifndef URL_H
#define URL_H
#include "tprintf.h"
#include "slice.h"

#define url_no_colon    -1
#define url_no_node     -2

int split_url(slice url, slice* node, slice* service, slice* path);

#endif
