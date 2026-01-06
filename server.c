#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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
} request_t;

static const char *BASIC_TOKEN = "Basic YWRtaW46cGFzc3dvcmQ="; /* admin:password */

static void die(const char *msg) {
    perror(msg);
    exit(EXIT_FAILURE);
}

static void send_response(int client, int status, const char *status_text, const char *content_type,
                          const char *body) {
    char resp[2048];
    size_t len = snprintf(resp, sizeof(resp),
                          "HTTP/1.1 %d %s\r\n"
                          "Server: c-mini\r\n"
                          "Content-Type: %s\r\n"
                          "Content-Length: %zu\r\n"
                          "Connection: close\r\n"
                          "\r\n"
                          "%s",
                          status, status_text, content_type, strlen(body), body);
    send(client, resp, len, 0);
}

static const char *get_header(const request_t *req, const char *name) {
    for (size_t i = 0; i < req->header_count; i++) {
        if (strcasecmp(req->headers[i].name, name) == 0) {
            return req->headers[i].value;
        }
    }
    return NULL;
}

static bool parse_request(int client, request_t *out) {
    static char buffer[REQ_BUF];
    ssize_t received = recv(client, buffer, sizeof(buffer) - 1, 0);
    if (received <= 0) {
        return false;
    }
    buffer[received] = '\0';

    char *headers_end = strstr(buffer, "\r\n\r\n");
    if (!headers_end) return false;
    *headers_end = '\0';
    char *body_start = headers_end + 4;

    char *saveptr = NULL;
    char *line = strtok_r(buffer, "\r\n", &saveptr);
    if (!line) return false;

    if (sscanf(line, "%7s %255s", out->method, out->path) != 2) {
        return false;
    }

    out->header_count = 0;
    out->body = NULL;
    out->body_len = 0;

    while ((line = strtok_r(NULL, "\r\n", &saveptr)) && *line) {
        if (out->header_count >= MAX_HEADERS) break;
        char *colon = strchr(line, ':');
        if (!colon) continue;
        *colon = '\0';
        char *name = line;
        char *value = colon + 1;
        while (*value == ' ') value++;
        strncpy(out->headers[out->header_count].name, name,
                sizeof(out->headers[out->header_count].name) - 1);
        out->headers[out->header_count].name[sizeof(out->headers[out->header_count].name) - 1] = '\0';
        strncpy(out->headers[out->header_count].value, value,
                sizeof(out->headers[out->header_count].value) - 1);
        out->headers[out->header_count].value[sizeof(out->headers[out->header_count].value) - 1] = '\0';
        out->header_count++;
    }

    const char *cl_header = get_header(out, "Content-Length");
    size_t content_len = cl_header ? (size_t)atoi(cl_header) : 0;
    size_t available = received - (body_start - buffer);
    if (content_len > 0 && available > 0) {
        out->body_len = content_len < available ? content_len : available;
        body_start[out->body_len] = '\0';
        out->body = body_start;
    }

    return true;
}

static bool is_authorized(const request_t *req) {
    const char *auth = get_header(req, "Authorization");
    if (!auth) return false;
    return strncmp(auth, BASIC_TOKEN, strlen(BASIC_TOKEN)) == 0;
}

static void handle_client(int client) {
    request_t req;
    if (!parse_request(client, &req)) {
        send_response(client, 400, "Bad Request", "text/plain", "bad request\n");
        return;
    }

    if (!is_authorized(&req)) {
        const char *body = "unauthorized\n";
        char resp[512];
        size_t len = snprintf(resp, sizeof(resp),
                              "HTTP/1.1 401 Unauthorized\r\n"
                              "Server: c-mini\r\n"
                              "WWW-Authenticate: Basic realm=\"Simple\"\r\n"
                              "Content-Type: text/plain\r\n"
                              "Content-Length: %zu\r\n"
                              "Connection: close\r\n"
                              "\r\n"
                              "%s",
                              strlen(body), body);
        send(client, resp, len, 0);
        return;
    }

    const char *allowed =
        "{\"message\":\"ok\",\"hint\":\"use GET/POST/PUT/DELETE with auth\"}\n";

    if (strcmp(req.method, "GET") == 0) {
        send_response(client, 200, "OK", "application/json", allowed);
    } else if (strcmp(req.method, "POST") == 0 || strcmp(req.method, "PUT") == 0 ||
               strcmp(req.method, "PATCH") == 0 || strcmp(req.method, "DELETE") == 0) {
        char body[1024];
        snprintf(body, sizeof(body),
                 "{ \"method\": \"%s\", \"path\": \"%s\", \"body_len\": %zu }\n",
                 req.method, req.path, req.body_len);
        send_response(client, 200, "OK", "application/json", body);
    } else {
        send_response(client, 405, "Method Not Allowed", "text/plain", "method not allowed\n");
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

