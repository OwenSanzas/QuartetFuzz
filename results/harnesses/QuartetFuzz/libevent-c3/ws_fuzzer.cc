#include <signal.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <netinet/in.h>
#include <sys/socket.h>

#include <string>
#include <vector>

#include <fuzzer/FuzzedDataProvider.h>

extern "C" {
#include "event2/bufferevent.h"
#include "event2/buffer.h"
#include "event2/event.h"
#include "event2/http.h"
#include "event2/util.h"
#include "event2/ws.h"
}

struct FuzzCtx {
	struct evws_connection *evws;
	int options;
	uint16_t close_reason;
	bool do_close;
};

static void
ws_msg_cb(struct evws_connection *evws, int type, const unsigned char *data,
	size_t len, void *arg)
{
	FuzzCtx *ctx = static_cast<FuzzCtx *>(arg);

	if (type == WS_TEXT_FRAME) {

		std::string s(reinterpret_cast<const char *>(data), len);
		evws_send_text(evws, s.c_str());
	} else {
		evws_send_binary(evws, reinterpret_cast<const char *>(data), len);
	}

	if (ctx->do_close) {
		evws_close(evws, ctx->close_reason);
	}
}

static void
ws_close_cb(struct evws_connection *evws, void *arg)
{
	FuzzCtx *ctx = static_cast<FuzzCtx *>(arg);

	ctx->evws = NULL;
}

static void
on_ws_request_cb(struct evhttp_request *req, void *arg)
{
	FuzzCtx *ctx = static_cast<FuzzCtx *>(arg);
	struct evws_connection *evws =
		evws_new_session(req, ws_msg_cb, ctx, ctx->options);
	if (!evws)
		return;

	ctx->evws = evws;
	evws_connection_set_closecb(evws, ws_close_cb, ctx);

	struct bufferevent *bev = evws_connection_get_bufferevent(evws);
	if (bev)
		bufferevent_getfd(bev);
}

static void
client_read_cb(struct bufferevent *bev, void *arg)
{
	struct evbuffer *in = bufferevent_get_input(bev);
	evbuffer_drain(in, evbuffer_get_length(in));
}

static void
client_event_cb(struct bufferevent *bev, short what, void *arg)
{
	(void)bev;
	(void)what;
	(void)arg;
}

static void
pump(struct event_base *base, int iters)
{
	for (int i = 0; i < iters; i++) {
		event_base_loop(base, EVLOOP_NONBLOCK);
	}
}

static const char *kUpgradeValues[] = {
	"websocket", "WebSocket", "WEBSOCKET", "chunked", "h2c", "",
};

static const char *kConnectionValues[] = {
	"Upgrade", "upgrade", "keep-alive, Upgrade", "Upgrade, keep-alive",
	"close", "keep-alive",
};

extern "C" int
LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	if (size < 8)
		return 0;

	signal(SIGPIPE, SIG_IGN);

	FuzzedDataProvider fdp(data, size);

	uint8_t ctl = fdp.ConsumeIntegral<uint8_t>();

	FuzzCtx ctx;
	ctx.evws = NULL;
	ctx.options = (ctl & 0x01) ? BEV_OPT_THREADSAFE : 0;
	bool missing_key = (ctl & 0x08) != 0;
	bool add_protocol = (ctl & 0x10) != 0;
	ctx.do_close = (ctl & 0x20) != 0;
	ctx.close_reason = fdp.ConsumeIntegral<uint16_t>();

	const char *upgrade_val = kUpgradeValues[fdp.ConsumeIntegralInRange<size_t>(
		0, sizeof(kUpgradeValues) / sizeof(kUpgradeValues[0]) - 1)];
	const char *connection_val =
		kConnectionValues[fdp.ConsumeIntegralInRange<size_t>(
			0, sizeof(kConnectionValues) / sizeof(kConnectionValues[0]) - 1)];

	std::string ws_key = fdp.ConsumeRandomLengthString(32);
	if (ws_key.empty())
		ws_key = "x3JJHMbDL1EzLkh9GBhXDw==";

	std::vector<uint8_t> frame_bytes = fdp.ConsumeRemainingBytes<uint8_t>();

	struct event_base *base = event_base_new();
	if (!base)
		return 0;

	struct evhttp *http = evhttp_new(base);
	if (!http) {
		event_base_free(base);
		return 0;
	}

	evhttp_set_cb(http, "/ws", on_ws_request_cb, &ctx);

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
	if (getsockname(listen_fd, (struct sockaddr *)&bound_addr, &bound_len) !=
		0) {
		evhttp_free(http);
		event_base_free(base);
		return 0;
	}

	evutil_socket_t client_fd = socket(AF_INET, SOCK_STREAM, 0);
	if (client_fd < 0) {
		evhttp_free(http);
		event_base_free(base);
		return 0;
	}
	evutil_make_socket_nonblocking(client_fd);
	connect(client_fd, (struct sockaddr *)&bound_addr, bound_len);

	struct bufferevent *client_bev =
		bufferevent_socket_new(base, client_fd, BEV_OPT_CLOSE_ON_FREE);
	if (!client_bev) {
		evutil_closesocket(client_fd);
		evhttp_free(http);
		event_base_free(base);
		return 0;
	}
	bufferevent_setcb(client_bev, client_read_cb, NULL, client_event_cb, NULL);
	bufferevent_enable(client_bev, EV_READ | EV_WRITE);

	struct evbuffer *out = bufferevent_get_output(client_bev);
	evbuffer_add_printf(out, "GET /ws HTTP/1.1\r\n");
	evbuffer_add_printf(out, "Host: 127.0.0.1\r\n");
	evbuffer_add_printf(out, "Upgrade: %s\r\n", upgrade_val);
	evbuffer_add_printf(out, "Connection: %s\r\n", connection_val);
	if (!missing_key)
		evbuffer_add_printf(out, "Sec-WebSocket-Key: %s\r\n", ws_key.c_str());
	if (add_protocol)
		evbuffer_add_printf(out, "Sec-WebSocket-Protocol: chat\r\n");
	evbuffer_add_printf(out, "\r\n");

	pump(base, 30);

	if (!frame_bytes.empty()) {
		evbuffer_add(out, frame_bytes.data(), frame_bytes.size());
		pump(base, 30);
	}

	if (ctx.evws) {
		evws_close(ctx.evws, ctx.close_reason);
		pump(base, 10);
	}

	bufferevent_free(client_bev);

	evhttp_free(http);
	event_base_free(base);

	return 0;
}
