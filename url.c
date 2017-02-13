#include "url.h"
#include <string.h>

// assumes for of <service>://<node>/<path>
int split_url(slice url, slice* node, slice* service, slice* path) {
    uint8_t const* colon = memchr(url.ptr, ':', url.len);
    if (!colon) {
        return url_no_colon;
    }
    uint8_t const* node_start = colon + 3;
    if (node_start >= url.ptr + url.len) {
        return url_no_node;
    }
    uint8_t const* path_start = memchr(node_start, '/', url.len - (node_start - url.ptr));
    if (!path_start) {
        path_start = url.ptr + url.len;
    }

    (*service).ptr = url.ptr;
    (*service).len = colon - (*service).ptr;
    (*node).ptr = node_start;
    (*node).len = path_start - node_start;
    (*path).ptr = path_start;
    (*path).len = url.len - ((*path).ptr - url.ptr);

    return 0;
}
