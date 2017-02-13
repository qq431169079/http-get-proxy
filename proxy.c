#include "proxy.h"
#include "http.h"
#include "url.h"
#include "tcp.h"
#include "slice.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdint.h>
#include <assert.h>
#include <errno.h>
#include <netdb.h>

#define BUFLEN          1024
#define TRANSFER_BUFLEN 1048576
#define HTTP_VERSION    0

void print_http_request(http_request const* req) {
    tprintf("http_request {\n");
    tprintf("    method: [%.*s]\n", (int)(*req).method.len, (*req).method.ptr);
    tprintf("    url: [%.*s]\n", (int)(*req).url.len, (*req).url.ptr);
    tprintf("    node: [%.*s]\n", (int)(*req).node.len, (*req).node.ptr);
    tprintf("    service: [%.*s]\n", (int)(*req).service.len, (*req).service.ptr);
    tprintf("    path: [%.*s]\n", (int)(*req).path.len, (*req).path.ptr);
    tprintf("    version: HTTP/1.%d\n", (*req).version);
    for (int i = 0; i < (*req).headerbuf.cap; ++i) {
        tprintf("    header: {[%.*s]: [%.*s]}\n",
            (int)(*req).headerbuf.ptr[i].name.len,
            (*req).headerbuf.ptr[i].name.ptr,
            (int)(*req).headerbuf.ptr[i].value.len,
            (*req).headerbuf.ptr[i].value.ptr);
    }
    tprintf("}\n");
}

void print_http_response(http_response const* res) {
    tprintf("http_response {\n");
    tprintf("    version: [%.*s]\n", (int)(*res).version.slice.len,
                                    (*res).version.slice.ptr);
    tprintf("    status: %d, phrase: [%.*s]\n",
        (*res).status.code, (int)(*res).status.phrase.len,
        (*res).status.phrase.ptr);
    for (int i = 0; i < (*res).headerbuf.cap; ++i) {
        tprintf("    header: {[%.*s]: [%.*s]}\n",
            (int)(*res).headerbuf.ptr[i].name.len,
            (*res).headerbuf.ptr[i].name.ptr,
            (int)(*res).headerbuf.ptr[i].value.len,
            (*res).headerbuf.ptr[i].value.ptr);
    }
    tprintf("}\n");
}

void set_close(mutslice value) {
    memset(value.ptr, ' ', value.len);
    char const* close = "close";
    size_t closelen = strlen(close);
    memcpy(value.ptr + value.len - closelen, close, closelen);
}

void change_keep_alive_to_close(http_headerbuf headerbuf) {
    for (size_t i = 0; i < headerbuf.cap; ++i) {
        http_header* h = &headerbuf.ptr[i];
        if (strncmp((*h).name.ptr, "Connection", (*h).name.len) == 0) {
            set_close((mutslice){(uint8_t*)(*h).value.ptr, (*h).value.len});
        }
    }
}

static
int write_all(int fd, slice bytes) {
    size_t total = 0;
    while (total < bytes.len) {
        ssize_t n = write(fd, &bytes.ptr[total], bytes.len - total);
        if (n == -1) {
            perror("write");
            if (errno != EINTR) {
                return -1;
            }
            continue;
        }
        total += n;
    }
    return 0;
}

static
int send_invalid_url(int client, slice url) {
    uint8_t response[1024];
    sprintf(response,
        "HTTP/1.%d 400 Bad Request\r\n"
        "\r\n"
        "<html>"
            "<body>"
                "400 Bad Request Reason: Invalid URL: %.*s"
            "</body>"
        "</html>",
        HTTP_VERSION, (int)(url.len), url.ptr);
    slice s = {response, strlen(response)};
    int err = write_all(client, s);
    if (err != 0) {
        perror("write_all");
        return -1;
    }
    return 0;
}

static
int send_invalid_method(int client, slice method) {
    uint8_t response[1024];
    sprintf(response,
        "HTTP/1.%d 400 Bad Request\r\n"
        "\r\n"
        "<html>"
            "<body>"
                "400 Bad Request Reason: Invalid Method: %.*s"
            "</body>"
        "</html>",
        HTTP_VERSION, (int)(method.len), method.ptr);
    slice s = {response, strlen(response)};
    int err = write_all(client, s);
    if (err != 0) {
        perror("write_all");
        return -1;
    }
    return 0;
}

static
int send_invalid_version(int client, slice version) {
    uint8_t response[1024];
    sprintf(response,
        "HTTP/1.%d 400 Bad Request\r\n"
        "\r\n"
        "<html>"
            "<body>"
                "400 Bad Request Reason: Invalid Version: %.*s"
            "</body>"
        "</html>",
        HTTP_VERSION, (int)(version.len), version.ptr);
    slice s = {response, strlen(response)};
    int err = write_all(client, s);
    if (err != 0) {
        perror("write_all");
        return -1;
    }
    return 0;
}

static
int send_unsupported_version(int client, uint8_t version) {
    uint8_t response[1024];
    sprintf(response,
        "HTTP/1.%d 501 Not Implemented\r\n"
        "\r\n"
        "<html>"
            "<body>"
                "501 Not Implemented HTTP Version: HTTP/1.%d"
            "</body>"
        "</html>",
        HTTP_VERSION, version);
    slice s = {response, strlen(response)};
    int err = write_all(client, s);
    if (err != 0) {
        perror("write_all");
        return -1;
    }
    return 0;
}

static
int send_not_found(int client, slice node, slice service, slice reason) {
    uint8_t response[1024];
    sprintf(response,
        "HTTP/1.%d 404 Not Found\r\n"
        "\r\n"
        "<html>"
            "<body>"
                "404 Not Found: %.*s: %.*s://%.*s"
            "</body>"
        "</html>",
        HTTP_VERSION,
        (int)(reason.len), reason.ptr,
        (int)(service.len), service.ptr,
        (int)(node.len), node.ptr);
    slice s = {response, strlen(response)};
    int err = write_all(client, s);
    if (err != 0) {
        perror("write_all");
        return -1;
    }
    return 0;

}

uint8_t transfer_buf[TRANSFER_BUFLEN];
static
int transfer_body(int src, int dst) {
    while (1) {
        ssize_t n = read(src, transfer_buf, TRANSFER_BUFLEN);
        if (n == -1) {
            perror("read");
            if (errno != EINTR) {
                return -1;
            }
            continue;
        }
        if (n == 0) {
            tprintf("transfer_body: read=0, returning\n");
            break;
        }

        int err = write_all(dst, (slice){transfer_buf, n});
        if (err != 0) {
            perror("write_all(dst, (slice){buf, n})");
            return -1;
        }
    }

    return 0;
}

#define HEADERBUF_CAP 64
void* handle_client(void* ptr) {
    handle_client_args* args = (handle_client_args*)(ptr);
    int client = args->client;
    free(args);

    http_header headers[HEADERBUF_CAP];
    uint8_t buf[BUFLEN] = {0};

    http_request req;
    http_request_init(&req, headers, 64);
    int err = http_read_request(client, (mutslice){buf, BUFLEN}, &req);
    switch (err) {
    case 0:
        break;
    case http_partial:
        tprintf("only partial request received, then eof\n");
        goto done;
    case http_err_method:
        tprintf("invalid method: [%.*s]\n", (int)(req.method.len), req.method.ptr);
        send_invalid_method(client, req.method);
        goto done;
    case http_err_url:
        tprintf("invalid url: [%.*s]\n", (int)(req.url.len), req.url.ptr);
        send_invalid_url(client, req.url);
        goto done;
    case http_err_version:
        tprintf("invalid version: [%.*s]\n", (int)(req.version_slice.len), req.version_slice.ptr);
        send_invalid_version(client, req.version_slice);
        goto done;
    }

    if (req.version > HTTP_VERSION) {
        tprintf("expected HTTP version %d or lower, got %d\n", HTTP_VERSION, req.version);
        send_unsupported_version(client, req.version);
        goto done;
    }

    // valid http request received
    print_http_request(&req);
    change_keep_alive_to_close(req.headerbuf);

    // check if requested url in cache?

    // if not in cache, try connect to host
    char* node_cstring = strndup(req.node.ptr, req.node.len);
    char* service_cstring = strndup(req.service.ptr, req.service.len);
    int host = dial_tcp(node_cstring, service_cstring);
    if (host < 0) {
        char const* reason = NULL;
        if (host != EAI_SYSTEM) {
            reason = gai_strerror(host);
        } else {
            reason = strerror(host);
        }
        tprintf("unable to connect to %.*s://%.*s: %s\n",
            (int)(req.service.len), req.service.ptr,
            (int)(req.node.len), req.node.ptr,
            reason);
        send_not_found(client, req.node, req.service, (slice){reason, strlen(reason)});
        free(node_cstring);
        free(service_cstring);
        goto done;
    }
    free(node_cstring);
    free(service_cstring);

/*
    tprintf("forwarding request [%.*s] to %.*s://%.*s\n",
        (int)(req.buf.len), req.buf.ptr,
        (int)(req.service.len), req.service.ptr,
        (int)(req.node.len), req.node.ptr);
*/
    err = write_all(host, req.buf);
    if (err != 0) {
        perror("write_all(host, req.buf)");
        close(host);
        goto done;
    }

    http_response res;
    http_response_init(&res, (http_headerbuf){headers, HEADERBUF_CAP});
    ssize_t totalread = http_read_response(host, (mutslice){buf, BUFLEN}, &res);
    if (totalread < 0) {
        tprintf("error reading response: %d, panicking!\n", (int)totalread);
        exit(1);
    }
    change_keep_alive_to_close(res.headerbuf);
    print_http_response(&res);

    err = write_all(client, (slice){buf, totalread});
    if (err != 0) {
        perror("write_all(client, res.buf)");
        close(host);
        goto done;
    }

    //int dst[2] = {client, file}
    err = transfer_body(host, client);
    if (err != 0) {
        perror("transfer_body(host, client)");
        close(host);
        goto done;
    }

    close(host);

done:
    tprintf("closing connection %d\n", client);
    close(client);
    pthread_exit(0);
}
