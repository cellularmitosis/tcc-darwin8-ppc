/* v0.2.21-g3 demo: minimal HTTP server in pure C, built with tcc.
 *
 * Listens on localhost:8080 (or whatever port is passed as argv[1]),
 * serves a tiny HTTP/1.1 response, exits after 3 requests. Exercises:
 *   - BSD sockets (socket, bind, listen, accept, recv, send, close)
 *   - struct sockaddr_in, htons, htonl
 *   - setsockopt
 *   - errno-bearing syscalls via libSystem
 *
 * This was the first interactive network program built with
 * tcc-darwin8-ppc. It proved that the BSD-socket-via-libSystem path
 * works end-to-end (no extra codegen surprises beyond what bzip2 and
 * cJSON exposed).
 *
 * Run:
 *     tcc -B<tccdir> -o /tmp/httpd demos/v0.2.21-httpd.c
 *     /tmp/httpd 8080 &
 *     curl http://127.0.0.1:8080/   # responds with "hello from..."
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

int main(int argc, char **argv) {
    int port = (argc > 1) ? atoi(argv[1]) : 8080;
    int srv = socket(AF_INET, SOCK_STREAM, 0);
    if (srv < 0) { perror("socket"); return 1; }
    int yes = 1;
    setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    if (bind(srv, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind"); return 1;
    }
    if (listen(srv, 5) < 0) { perror("listen"); return 1; }

    printf("listening on 127.0.0.1:%d\n", port);
    fflush(stdout);

    int i;
    for (i = 0; i < 3; i++) {
        struct sockaddr_in client;
        socklen_t cl = sizeof(client);
        int c = accept(srv, (struct sockaddr *)&client, &cl);
        if (c < 0) { perror("accept"); break; }

        char buf[1024];
        ssize_t n = recv(c, buf, sizeof(buf) - 1, 0);
        if (n < 0) { perror("recv"); close(c); continue; }
        buf[n] = '\0';
        printf("request %d (%zd bytes received)\n", i + 1, n);

        const char *resp =
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: text/plain\r\n"
            "Content-Length: 26\r\n"
            "\r\n"
            "hello from tcc-built httpd";
        send(c, resp, strlen(resp), 0);
        close(c);
    }
    close(srv);
    printf("served 3 requests, exiting\n");
    return 0;
}
