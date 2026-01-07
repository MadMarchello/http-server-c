#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <strings.h>
#include <sys/socket.h>
#include <unistd.h>

/* Simple HTTP server written in pure C.
 * - Supports GET, POST, PUT, DELETE (others yield 405).
 * - Basic auth with credentials admin:password.
 * - Echoes JSON with method/path/headers/body length.
 */

#define PORT 8080
#define BACKLOG 10
#define REQ_BUF 8192
#define MAX_HEADERS 64

typedef struct {
    char name[64];
    char value[256];
} header_t;

typedef struct {
    char method[8];
    char path[256];
    header_t headers[MAX_HEADERS];
    size_t header_count;
    char *body;
    size_t body_len;
    char buffer[REQ_BUF];
    size_t buffer_len;
} request_t;

static const char *BASIC_CREDENTIALS = "admin:password";

static void die(const char *msg) {
    perror(msg);
    exit(EXIT_FAILURE);
}

static int send_all(int client, const char *data, size_t len) {
    size_t sent = 0;

    while (sent < len) {
        ssize_t res = send(client, data + sent, len - sent, 0);
        if (res < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        if (res == 0) {
            break;
        }
        sent += (size_t)res;
    }

    return sent == len ? 0 : -1;
}

static void send_response(int client, int status, const char *status_text, const char *content_type,
                          const char *body, const char *extra_headers) {
    char resp[2048];
    const char *headers = extra_headers ? extra_headers : "";
    size_t body_len = body ? strlen(body) : 0;
    size_t len = snprintf(resp, sizeof(resp),
                          "HTTP/1.1 %d %s\r\n"
                          "Server: c-mini\r\n"
                          "%s"
                          "Content-Type: %s\r\n"
                          "Content-Length: %zu\r\n"
                          "Connection: close\r\n"
                          "\r\n"
                          "%s",
                          status, status_text, headers, content_type, body_len, body ? body : "");
    send_all(client, resp, len);
}

static const char *get_header(const request_t *req, const char *name) {
    for (size_t i = 0; i < req->header_count; i++) {
        if (strcasecmp(req->headers[i].name, name) == 0) {
            return req->headers[i].value;
        }
    }
    return NULL;
}

static bool parse_content_length(const char *value, size_t *out_len) {
    char *endptr = NULL;
    unsigned long parsed;

    if (!value) {
        *out_len = 0;
        return true;
    }

    while (*value == ' ' || *value == '\t') {
        value++;
    }

    errno = 0;
    parsed = strtoul(value, &endptr, 10);
    if (errno != 0 || endptr == value) {
        return false;
    }

    while (*endptr == ' ' || *endptr == '\t') {
        endptr++;
    }
    if (*endptr != '\0') {
        return false;
    }

    if (parsed > SIZE_MAX) {
        return false;
    }

    *out_len = (size_t)parsed;
    return true;
}

static bool parse_request(int client, request_t *out) {
    ssize_t received;
    size_t total = 0;
    size_t headers_len = 0;
    char *headers_end = NULL;

    if (!out) {
        return false;
    }

    memset(out, 0, sizeof(*out));

    while (total < sizeof(out->buffer) - 1) {
        received = recv(client, out->buffer + total, sizeof(out->buffer) - 1 - total, 0);
        if (received < 0) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }
        if (received == 0) {
            break;
        }
        total += (size_t)received;
        out->buffer[total] = '\0';

        headers_end = strstr(out->buffer, "\r\n\r\n");
        if (headers_end) {
            headers_len = (size_t)(headers_end - out->buffer) + 4;
            break;
        }
    }

    if (!headers_end || headers_len == 0) {
        return false;
    }

    *headers_end = '\0';
    out->buffer_len = total;

    char *body_start = headers_end + 4;
    size_t available = total > headers_len ? total - headers_len : 0;
    char *saveptr = NULL;
    char *line = strtok_r(out->buffer, "\r\n", &saveptr);

    if (!line || sscanf(line, "%7s %255s", out->method, out->path) != 2) {
        return false;
    }

    while ((line = strtok_r(NULL, "\r\n", &saveptr)) && *line) {
        char *colon;
        char *name;
        char *value;

        if (out->header_count >= MAX_HEADERS) {
            break;
        }

        colon = strchr(line, ':');
        if (!colon) {
            continue;
        }

        *colon = '\0';
        name = line;
        value = colon + 1;

        while (*value == ' ') {
            value++;
        }

        strncpy(out->headers[out->header_count].name, name,
                sizeof(out->headers[out->header_count].name) - 1);
        out->headers[out->header_count].name[sizeof(out->headers[out->header_count].name) - 1] = '\0';

        strncpy(out->headers[out->header_count].value, value,
                sizeof(out->headers[out->header_count].value) - 1);
        out->headers[out->header_count].value[sizeof(out->headers[out->header_count].value) - 1] = '\0';

        out->header_count++;
    }

    {
        const char *cl_header = get_header(out, "Content-Length");
        size_t content_len = 0;

        if (!parse_content_length(cl_header, &content_len)) {
            return false;
        }

        if (headers_len + content_len >= sizeof(out->buffer)) {
            return false;
        }

        while (available < content_len) {
            received = recv(client, out->buffer + total, sizeof(out->buffer) - 1 - total, 0);
            if (received < 0) {
                if (errno == EINTR) {
                    continue;
                }
                return false;
            }
            if (received == 0) {
                return false;
            }

            total += (size_t)received;
            available = total > headers_len ? total - headers_len : 0;
        }

        out->buffer[headers_len + content_len] = '\0';
        out->body = body_start;
        out->body_len = content_len;
        out->buffer_len = total;
    }

    return true;
}

static int base64_value(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    if (c == '=') return -2; /* padding */
    return -1;               /* invalid */
}

static bool base64_decode(const char *in, uint8_t *out, size_t out_cap, size_t *out_len) {
    size_t len = strlen(in);
    size_t out_pos = 0;

    if (len % 4 != 0) {
        return false;
    }

    for (size_t i = 0; i < len; i += 4) {
        int v0 = base64_value(in[i]);
        int v1 = base64_value(in[i + 1]);
        int v2 = base64_value(in[i + 2]);
        int v3 = base64_value(in[i + 3]);

        if (v0 < 0 || v1 < 0) {
            return false;
        }

        uint32_t triple = ((uint32_t)v0 << 18) | ((uint32_t)v1 << 12);

        if (v2 >= 0) {
            triple |= (uint32_t)v2 << 6;
        } else if (v2 == -1) {
            return false;
        }

        if (v3 >= 0) {
            triple |= (uint32_t)v3;
        } else if (v3 == -1) {
            return false;
        }

        if (out_pos + 3 > out_cap) {
            return false;
        }

        out[out_pos++] = (uint8_t)((triple >> 16) & 0xFF);
        if (v2 != -2) {
            out[out_pos++] = (uint8_t)((triple >> 8) & 0xFF);
        }
        if (v3 != -2) {
            out[out_pos++] = (uint8_t)(triple & 0xFF);
        }

        if (v2 == -2) {
            if (v3 != -2 || i + 4 != len) {
                return false;
            }
            break;
        }
        if (v3 == -2 && i + 4 != len) {
            return false;
        }
    }

    *out_len = out_pos;
    return true;
}

/* Decode and verify Basic auth exactly (no prefix/suffix tricks or variant base64). */
static bool is_basic_authorized(const char *auth_header) {
    static const char prefix[] = "Basic ";
    uint8_t decoded[64];
    size_t decoded_len = 0;
    size_t expected_len = strlen(BASIC_CREDENTIALS);

    if (!auth_header) {
        return false;
    }
    if (strncmp(auth_header, prefix, sizeof(prefix) - 1) != 0) {
        return false;
    }

    if (!base64_decode(auth_header + sizeof(prefix) - 1, decoded, sizeof(decoded), &decoded_len)) {
        return false;
    }

    if (decoded_len != expected_len) {
        return false;
    }

    return memcmp(decoded, BASIC_CREDENTIALS, expected_len) == 0;
}

static bool is_authorized(const request_t *req) {
    const char *auth = get_header(req, "Authorization");
    return is_basic_authorized(auth);
}

static void handle_client(int client) {
    request_t req;
    if (!parse_request(client, &req)) {
        send_response(client, 400, "Bad Request", "text/plain", "bad request\n", NULL);
        return;
    }

    if (!is_authorized(&req)) {
        const char *body = "unauthorized\n";
        const char *extra = "WWW-Authenticate: Basic realm=\"Simple\"\r\n";
        send_response(client, 401, "Unauthorized", "text/plain", body, extra);
        return;
    }

    const char *allowed =
        "{\"message\":\"ok\",\"hint\":\"use GET/POST/PUT/DELETE with auth\"}\n";

    if (strcmp(req.method, "GET") == 0) {
        send_response(client, 200, "OK", "application/json", allowed, NULL);
    } else if (strcmp(req.method, "POST") == 0 || strcmp(req.method, "PUT") == 0 ||
               strcmp(req.method, "PATCH") == 0 || strcmp(req.method, "DELETE") == 0) {
        char body[1024];
        snprintf(body, sizeof(body),
                 "{ \"method\": \"%s\", \"path\": \"%s\", \"body_len\": %zu }\n",
                 req.method, req.path, req.body_len);
        send_response(client, 200, "OK", "application/json", body, NULL);
    } else {
        send_response(client, 405, "Method Not Allowed", "text/plain", "method not allowed\n", NULL);
    }
}

int main(void) {
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) die("socket");

    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        die("setsockopt");
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) die("bind");
    if (listen(server_fd, BACKLOG) < 0) die("listen");

    printf("Server running on http://127.0.0.1:%d (auth: admin:password)\n", PORT);

    while (1) {
        int client = accept(server_fd, NULL, NULL);
        if (client < 0) {
            if (errno == EINTR) continue;
            die("accept");
        }
        handle_client(client);
        close(client);
    }

    close(server_fd);
    return 0;
}

