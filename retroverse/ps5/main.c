/* RetroVerse PS5 Alpha: read-only HTTP first-boot test.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#define PORT 8765

typedef struct { char reserved[45]; char message[3075]; } notify_request_t;
int sceKernelSendNotificationRequest(int, notify_request_t *, size_t, int);
static void notify(const char *s) {
    notify_request_t r = {0};
    snprintf(r.message, sizeof(r.message), "%s", s);
    sceKernelSendNotificationRequest(0, &r, sizeof(r), 0);
}

static const char page[] =
"<!doctype html><html lang='en'><head><meta charset='utf-8'>"
"<meta name='viewport' content='width=device-width,initial-scale=1'>"
"<title>RetroVerse PS5 Alpha</title><style>"
"*{box-sizing:border-box}body{margin:0;background:radial-gradient(ellipse at top,#263564,#0c1020 70%);color:#eaf0ff;font:16px system-ui,Arial}"
"main{max-width:950px;margin:0 auto;padding:10vh 24px}h1{font-size:clamp(42px,8vw,84px);margin:0;letter-spacing:-.06em}"
".tag{letter-spacing:.3em;color:#a7b9fc;font-size:12px}.panel{margin-top:28px;border:1px solid #6977a255;border-radius:22px;padding:28px;background:#ffffff0c}"
".status{color:#86efac}.muted{color:#aab5d0;line-height:1.6}footer{margin-top:36px;color:#8290b0;font-size:13px}"
"</style></head><body><main><div class='tag'>PS5 HOMEBREW · FIRST-BOOT TEST</div>"
"<h1>RETRO<span style='color:#ac9fff'>VERSE</span></h1><section class='panel'>"
"<h2 class='status'>● PS5 ELF HTTP service is running</h2>"
"<p class='muted'>Your console loaded the RetroVerse test payload and served this page on your local network.</p>"
"<p>This is the first boot milestone, not the complete game store. The download engine, game library, and CoinOPS layout follow.</p>"
"<p><a style='color:#adcaff' href='/api/health'>View API health</a></p>"
"</section><footer>RetroVerse Alpha 0.1 · Read-only · Local network only</footer></main></body></html>";

static int send_all(int fd, const char *p, size_t length) {
    while (length) {
        ssize_t n = send(fd, p, length, 0);
        if (n <= 0) return -1;
        p += n; length -= (size_t)n;
    }
    return 0;
}
static void reply(int fd, int code, const char *reason, const char *ctype,
                  const char *body) {
    char header[512];
    size_t len = strlen(body);
    int n = snprintf(header, sizeof(header),
        "HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %zu\r\n"
        "Cache-Control: no-store\r\nConnection: close\r\nX-Content-Type-Options: nosniff\r\n\r\n",
        code, reason, ctype, len);
    if (n > 0 && (size_t)n < sizeof(header) &&
        send_all(fd, header, (size_t)n) == 0) send_all(fd, body, len);
}
static void handle(int fd) {
    char req[2049], method[16], path[256], version[16];
    ssize_t n = recv(fd, req, sizeof(req)-1, 0);
    if (n <= 0) return;
    req[n] = '\0';
    if (sscanf(req, "%15s %255s %15s", method, path, version) != 3) {
        reply(fd, 400, "Bad Request", "text/plain", "Malformed request");
        return;
    }
    if (strcmp(method, "GET") != 0) {
        reply(fd, 405, "Method Not Allowed", "text/plain", "GET only");
    } else if (!strcmp(path, "/") || !strcmp(path, "/index.html")) {
        reply(fd, 200, "OK", "text/html; charset=utf-8", page);
    } else if (!strcmp(path, "/api/health")) {
        reply(fd, 200, "OK", "application/json",
            "{\"ok\":true,\"platform\":\"ps5\",\"version\":\"alpha-0.1\",\"mode\":\"read-only\"}");
    } else if (!strncmp(path, "/api/", 5)) {
        reply(fd, 501, "Not Implemented", "application/json",
            "{\"error\":\"Not available in first-boot alpha\"}");
    } else reply(fd, 404, "Not Found", "text/plain", "Not found");
}
int main(void) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) { notify("RetroVerse: socket error"); return 1; }
    int reuse = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(PORT);
    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) != 0 ||
        listen(sock, 8) != 0) {
        notify("RetroVerse: port 8765 unavailable"); close(sock); return 2;
    }
    notify("RetroVerse ELF loaded. Visit PS5-IP:8765 from PC.");
    for (;;) {
        int client = accept(sock, NULL, NULL);
        if (client < 0) { if (errno == EINTR) continue; break; }
        struct timeval timeout = { .tv_sec = 5, .tv_usec = 0 };
        setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
        handle(client);
        close(client);
    }
    close(sock);
    return 0;
}
