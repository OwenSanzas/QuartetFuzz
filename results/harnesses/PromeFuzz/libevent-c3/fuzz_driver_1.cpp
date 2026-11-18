#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <cstdint>
#include <cstddef>

#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

#include <event2/event.h>
#include <event2/buffer.h>
#include <event2/bufferevent.h>
#include <event2/http.h>
#include <event2/http_struct.h>
#include <event2/keyvalq_struct.h>
#include <event2/util.h>
#include <event2/listener.h>

#include <ws.h>

struct GenCbArg {
    int options;
};

static struct evws_connection *g_evws = nullptr;

static void dummy_ws_msg_cb(struct evws_connection *evws, int type,
                             const unsigned char *data, size_t len, void *arg) {
    (void)evws;
    (void)type;
    (void)data;
    (void)len;
    (void)arg;
}

static void generic_cb(struct evhttp_request *req, void *arg) {
    GenCbArg *a = static_cast<GenCbArg *>(arg);

    struct evws_connection *evws =
        evws_new_session(req, dummy_ws_msg_cb, nullptr, a->options);

    if (evws) {

        g_evws = evws;
    }

}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size) {
    if (Size == 0)
        return 0;

    g_evws = nullptr;

    struct event_base *base = event_base_new();
    if (!base)
        return 0;

    struct evhttp *http = evhttp_new(base);
    if (!http) {
        event_base_free(base);
        return 0;
    }

    struct evhttp_bound_socket *handle =
        evhttp_bind_socket_with_handle(http, "127.0.0.1", 0);
    if (!handle) {
        evhttp_free(http);
        event_base_free(base);
        return 0;
    }

    evutil_socket_t listen_fd = evhttp_bound_socket_get_fd(handle);
    struct sockaddr_in bound_addr;
    socklen_t bound_len = sizeof(bound_addr);
    memset(&bound_addr, 0, sizeof(bound_addr));
    if (getsockname(listen_fd, (struct sockaddr *)&bound_addr, &bound_len) != 0) {
        evhttp_free(http);
        event_base_free(base);
        return 0;
    }
    uint16_t port = ntohs(bound_addr.sin_port);

    GenCbArg cbarg;
    cbarg.options = (Size > 1) ? (Data[1] % 4) : 0;
    evhttp_set_gencb(http, generic_cb, &cbarg);

    size_t header_data_offset = (Size > 2) ? 2 : 0;
    std::string input(reinterpret_cast<const char *>(Data + header_data_offset),
                       (Size > header_data_offset) ? (Size - header_data_offset) : 0);

    size_t half = input.size() / 2;
    std::string part1 = input.substr(0, half);
    std::string part2 = (half < input.size()) ? input.substr(half) : std::string();

    if (part1.empty()) part1 = "websocket";
    if (part2.empty()) part2 = "dGhlIHNhbXBsZSBub25jZQ==";

    for (auto &c : part1) {
        if (c == '\r' || c == '\n') c = ' ';
    }
    for (auto &c : part2) {
        if (c == '\r' || c == '\n') c = ' ';
    }

    uint8_t choice = Data[0] % 4;
    std::ostringstream headers;

    switch (choice) {
        case 0:
            headers << "Upgrade: websocket\r\n";
            headers << "Connection: Upgrade\r\n";
            headers << "Sec-WebSocket-Key: " << part2 << "\r\n";
            break;
        case 1:
            headers << "Upgrade: " << part1 << "\r\n";
            headers << "Connection: Upgrade\r\n";
            headers << "Sec-WebSocket-Key: " << part2 << "\r\n";
            break;
        case 2:
            headers << "Upgrade: websocket\r\n";
            headers << "Connection: " << part1 << "\r\n";
            headers << "Sec-WebSocket-Key: " << part2 << "\r\n";
            break;
        case 3:
        default:
            if (!part1.empty())
                headers << "Upgrade: " << part1 << "\r\n";
            if (!part2.empty())
                headers << "Sec-WebSocket-Key: " << part2 << "\r\n";
            break;
    }

    std::string request_line = "GET /ws HTTP/1.1\r\n";
    std::string host_header = "Host: 127.0.0.1\r\n";
    std::string http_request =
        request_line + host_header + headers.str() + "\r\n";

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    server_addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    struct bufferevent *client_bev =
        bufferevent_socket_new(base, -1, BEV_OPT_CLOSE_ON_FREE);
    if (client_bev) {
        bufferevent_enable(client_bev, EV_READ | EV_WRITE);
        bufferevent_socket_connect(client_bev,
                                   (struct sockaddr *)&server_addr,
                                   sizeof(server_addr));
        bufferevent_write(client_bev, http_request.data(),
                           http_request.size());
    }

    for (int i = 0; i < 300; i++) {
        event_base_loop(base, EVLOOP_NONBLOCK);
    }

    if (g_evws) {
        evws_connection_free(g_evws);
        g_evws = nullptr;
    }

    if (client_bev) {
        bufferevent_free(client_bev);
    }

    evhttp_free(http);
    event_base_free(base);

    return 0;
}