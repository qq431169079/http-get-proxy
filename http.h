#ifndef HTTP_H
#define HTTP_H
#include "tprintf.h"
#include "slice.h"
#include <stdint.h>
#include <sys/types.h>

#define http_partial        -1
#define http_err_method     -2
#define http_err_url        -3
#define http_err_version    -4
#define http_err_newline    -5
#define http_err_zerolentok -6
#define http_err_header     -7
#define http_not_newline    -8
#define http_too_many_headers -9
#define http_req_too_large  -10
#define http_read_eof       -11
#define http_res_too_large  -12

typedef struct {
    slice name;
    slice value;
} http_header;

typedef struct {
    http_header* ptr;
    size_t cap;
} http_headerbuf;

typedef struct {
    slice buf;
    slice method;
    slice url;
    slice node;
    slice service;
    slice path;
    slice version_slice;
    uint8_t version;
    http_headerbuf headerbuf;
} http_request;

typedef struct {
    slice buf;
    struct{uint8_t minor; slice slice;} version;
    struct{int code; slice phrase;} status;
    http_headerbuf headerbuf;
} http_response;

void http_request_init(http_request* req, http_header* headers, size_t cap);
void http_response_init(http_response* res, http_headerbuf hdrbuf);

int http_parse_request(uint8_t const* buf, size_t len, http_request* r);
int http_read_request(int fd, mutslice buf, http_request* req);
int http_parse_response(slice buf, http_response* res);
ssize_t http_read_response(int fd, mutslice buf, http_response* res);

#endif
