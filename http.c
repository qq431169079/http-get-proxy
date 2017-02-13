#include "http.h"
#include "url.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>

#define ZERO_LEN_PATH   "/"

static
int is_space(uint8_t b) {
    return (b == ' ' || b == '\t'
        || b == '\r' || b == '\n');
}

// Returns 0 or http_partial
static
int parse_line(uint8_t const* buf, size_t len, size_t* pos) {
    while (*pos < len && buf[*pos] != '\r' && buf[*pos] != '\n') {
        *pos += 1;
    }

    if (*pos < len) {
        return 0;
    } else {
        return http_partial;
    }
}

static
int consume_whitespace(slice buf, size_t* pos) {
    while (*pos < buf.len && is_space(buf.ptr[*pos])) {
        *pos += 1;
    }
    if (*pos < buf.len) {
        return 0;
    }
    return http_partial;
}

static
int parse_token(uint8_t const* buf, size_t len, size_t* pos, slice* token) {
    // consume whitespace
    while (*pos < len && is_space(buf[*pos])) {
        *pos += 1;
    }

    size_t start = *pos;
    while (1) {
        if (*pos >= len) {
            return http_partial;
        }

        uint8_t b = buf[*pos];
        *pos += 1;

        if (is_space(b)) {
            size_t toklen = *pos - start - 1;
            if (toklen == 0) {
                // should never happen because of
                // consuming whitespace at start
                return http_err_zerolentok;
            }
            token->ptr = &buf[start];
            token->len = toklen;
            return 0;
        }
    }
}

static
int parse_newline(uint8_t const* buf, size_t len, size_t* pos) {
    if (*pos >= len) {
        return http_partial;
    }
    if (buf[*pos] == '\r') {
        *pos += 1;
        if (*pos >= len) {
            return http_partial;
        }
        if (buf[*pos] == '\n') {
            *pos += 1;
            return 0;
        }
        return http_err_newline;
    }
    if (buf[*pos] == '\n') {
        *pos += 1;
        return 0;
    }
    return http_not_newline;
}

static
int parse_version(uint8_t const* buf, size_t len, size_t* pos,
                  uint8_t* version, slice* version_slice) {
    version_slice->ptr = &buf[*pos];
    version_slice->len = 0;

    char const* expect = "HTTP/1.";
    int expectlen = strlen(expect);
    for (int i = 0; *pos < len && i < expectlen; ++i) {
        version_slice->len += 1;
        if (buf[*pos] != expect[i]) {
            return http_err_version;
        }
        *pos += 1;
    }
    if (*pos >= len) {
        return http_partial;
    }
    uint8_t b = buf[*pos];
    *pos += 1;
    version_slice->len += 1;
    switch (b) {
    case '0':
    case '1':
        *version = b - '0';
        return 0;
    default:
        return http_err_version;
    }
}

#define NUM_HTTP_METHODS 7
static
int is_valid_method(slice const* b) {
    char const* valid[NUM_HTTP_METHODS] = {"GET", "HEAD", "PUT", "DELETE", "POST", "TRACE", "CONNECT"};
    for (int i = 0; i < NUM_HTTP_METHODS; ++i) {
        if (strncmp((*b).ptr, valid[i], (*b).len) == 0) {
            return 1;
        }
    }
    return 0;
}

static
int parse_header(uint8_t const* buf, size_t len, size_t* pos, http_header* header) {
    int err = parse_token(buf, len, pos, &(*header).name);
    if (err != 0) {
        assert(err == http_partial);
        return http_partial;
    }
    if ((*header).name.ptr[(*header).name.len - 1] != ':') {
        return http_err_header;
    }
    (*header).name.len -= 1;

    err = consume_whitespace((slice){buf, len}, pos);
    if (err != 0) {
        return err;
    }
    (*header).value.ptr = &buf[*pos];

    err = parse_line(buf, len, pos);
    if (err != 0) {
        return http_partial;
    }
    (*header).value.len = &buf[*pos] - (*header).value.ptr;

    err = parse_newline(buf, len, pos);
    if (err != 0) {
        return err;
    }

    return 0;
}

static
int parse_headers(uint8_t const* buf, size_t len, size_t* pos, http_headerbuf* headerbuf) {
    int header = 0;
    while (1) {
        int err = parse_newline(buf, len, pos);
        switch (err) {
        case 0:
            (*headerbuf).cap = header;
            return 0;
        case http_partial:
            return http_partial;
        case http_err_newline:
            return http_err_newline;
        case http_not_newline:
            break;
        }

        if (header >= (*headerbuf).cap) {
            return http_too_many_headers;
        }
        err = parse_header(buf, len, pos, &(*headerbuf).ptr[header]);
        if (err != 0) {
            return err; // partial or err_header
        }
        header += 1;
    }
}

static
int parse_status(slice status, int* code) {
    if (status.len < 3) {
        return http_partial;
    }
    int a = status.ptr[0] - '0';
    int b = status.ptr[1] - '0';
    int c = status.ptr[2] - '0';
    // TODO status error checking
    *code = 100*a + 10*b + c;
    return 0;
}

static
int parse_status_phrase(slice buf, size_t* pos, slice* phrase) {
    int err;

    err = consume_whitespace(buf, pos);
    if (err != 0) {
        return err;
    }
    (*phrase).ptr = &buf.ptr[*pos];

    err = parse_line(buf.ptr, buf.len, pos);
    if (err != 0) {
        return http_partial;
    }
    (*phrase).len = &buf.ptr[*pos] - (*phrase).ptr;
    return 0;
}

int http_parse_request(uint8_t const* buf, size_t len, http_request* r) {
    size_t pos = 0;
    int err;

    err = parse_token(buf, len, &pos, &(*r).method);
    if (err != 0) {
        assert(err == http_partial);
        return http_partial;
    }
    if (!is_valid_method(&(*r).method)) {
        return http_err_method;
    }

    err = parse_token(buf, len, &pos, &(*r).url);
    if (err != 0) {
        assert(err == http_partial);
        return http_partial;
    }
    // validate url?

    err = parse_version(buf, len, &pos, &(*r).version, &(*r).version_slice);
    if (err != 0) {
        // either http_partial or http_err_version
        return err;
    }

    err = parse_newline(buf, len, &pos);
    if (err != 0) {
        return err; // either partial or err_newline or not_newline
    }

    err = parse_headers(buf, len, &pos, &(*r).headerbuf);
    if (err != 0) {
        return err; // either partial or err_header
    }

    (*r).buf.ptr = buf;
    (*r).buf.len = pos;

    err = split_url((*r).url, &(*r).node, &(*r).service, &(*r).path);
    if (err != 0) {
        return http_err_url;
    }
    if ((*r).path.len == 0) {
        (*r).path.ptr = ZERO_LEN_PATH;
        (*r).path.len = strlen(ZERO_LEN_PATH);
    }

    return 0;
}

void http_request_init(http_request* req, http_header* headers, size_t cap) {
    memset(req, 0, sizeof(http_request));
    (*req).headerbuf.ptr = headers;
    (*req).headerbuf.cap = cap;
    for (int i = 0; i < cap; ++i) {
        (*req).headerbuf.ptr[i].name.ptr = NULL;
        (*req).headerbuf.ptr[i].name.len = 0;
        (*req).headerbuf.ptr[i].value.ptr = NULL;
        (*req).headerbuf.ptr[i].value.len = 0;
    }
}

void http_response_init(http_response* res, http_headerbuf hdrbuf) {
    memset(res, 0, sizeof(http_response));
    memset(hdrbuf.ptr, 0, sizeof(http_header)*hdrbuf.cap);
    (*res).headerbuf = hdrbuf;
}

int http_read_request(int fd, mutslice buf, http_request* req) {
    size_t count = 0;
    int eof = 0;
    while (!eof) {
        size_t tail = buf.len - count;
        if (tail == 0) {
            return http_req_too_large;
        }
        ssize_t n = read(fd, &buf.ptr[count], tail);
        switch (n) {
        case -1:
            perror("read");
            continue;
        case 0:
            tprintf("read=0\n");
            eof = 1;
            break;
        default:
            tprintf("read=%zd\n", n);
            count += n;
        }

        int err = http_parse_request(buf.ptr, count, req);
        if (err != 0) {
            if (err == http_partial) {
                continue;
            }
            return err;
        }
        return 0;
    }
    return http_partial; // TODO should return http_partial?
}

int http_parse_response(slice buf, http_response* res) {
    size_t pos = 0;
    int err;

    err = parse_version(buf.ptr, buf.len, &pos,
        &(*res).version.minor, &(*res).version.slice);
    if (err != 0) {
        return err;
    }

    slice status;
    err = parse_token(buf.ptr, buf.len, &pos, &status);
    if (err != 0) {
        return err;
    }
    err = parse_status(status, &(*res).status.code);
    if (err != 0) {
        return err;
    }

    err = parse_status_phrase(buf, &pos, &(*res).status.phrase);
    if (err != 0) {
        return err;
    }

    err = parse_newline(buf.ptr, buf.len, &pos);
    if (err != 0) {
        return err;
    }

    err = parse_headers(buf.ptr, buf.len, &pos, &(*res).headerbuf);
    if (err != 0) {
        return err;
    }

    (*res).buf.ptr = buf.ptr;
    (*res).buf.len = pos;

    return 0;
}

ssize_t http_read_response(int fd, mutslice buf, http_response* res) {
    size_t count = 0;
    int eof = 0;
    while (!eof) {
        size_t tail = buf.len - count;
        if (tail == 0) {
            return http_res_too_large;
        }
        ssize_t n = read(fd, &buf.ptr[count], tail);
        switch (n) {
        case -1:
            perror("read");
            continue;
        case 0:
            tprintf("read=0\n");
            eof = 1;
            break;
        default:
            tprintf("read=%zd\n", n);
            count += n;
        }

        int err = http_parse_response((slice){buf.ptr, count}, res);
        if (err != 0) {
            if (err == http_partial) {
                continue;
            }
            return err;
        }
        return count;
    }
    return http_partial; // TODO should return http_partial?

}
