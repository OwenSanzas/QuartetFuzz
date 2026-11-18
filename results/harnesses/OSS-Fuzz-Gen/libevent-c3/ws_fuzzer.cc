#include <fuzzer/FuzzedDataProvider.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>

#include <arpa/inet.h>
#include <sys/queue.h>

extern "C" {
#include <event2/event.h>
#include <event2/buffer.h>
#include <event2/bufferevent.h>
#include <event2/http.h>
#include <event2/http_struct.h>
#include <event2/keyvalq_struct.h>
#include <event2/thread.h>
#include <event2/ws.h>
#include "http-internal.h"
}

static void fuzz_ws_on_msg_cb(struct evws_connection *evws, int type,
                               const unsigned char *msg_data, size_t len,
                               void *arg) {
  (void)evws;
  (void)type;
  (void)msg_data;
  (void)len;
  (void)arg;
}

static void fuzz_ws_on_close_cb(struct evws_connection *evws, void *arg) {
  (void)evws;
  (void)arg;
}

static std::string sanitize_header_value(std::string s) {
  for (auto &c : s) {
    if (c == '\r' || c == '\n')
      c = ' ';
  }
  return s;
}

static std::string build_ws_frame(FuzzedDataProvider &stream) {
  uint8_t opcode = stream.ConsumeIntegralInRange<uint8_t>(0, 0x0F);
  bool fin = stream.ConsumeBool();
  bool mask = stream.ConsumeBool();

  int len_choice = stream.ConsumeIntegralInRange<int>(0, 3);
  size_t payload_len = 0;
  bool oversized = false;
  switch (len_choice) {
    case 0:
      payload_len = stream.ConsumeIntegralInRange<size_t>(0, 125);
      break;
    case 1:
      payload_len = stream.ConsumeIntegralInRange<size_t>(126, 2000);
      break;
    case 2:
      payload_len = stream.ConsumeIntegralInRange<size_t>(65536, 70000);
      break;
    default:

      payload_len = 10485761 + stream.ConsumeIntegralInRange<size_t>(0, 100000);
      oversized = true;
      break;
  }

  std::string payload;
  if (!oversized)
    payload = stream.ConsumeBytesAsString(std::min<size_t>(payload_len, 2000));
  if (!oversized && payload.size() < payload_len)
    payload.resize(payload_len, 0);

  std::string frame;
  frame.push_back((char)((fin ? 0x80 : 0) | opcode));
  uint8_t mbit = mask ? 0x80 : 0;
  if (payload_len <= 125) {
    frame.push_back((char)(mbit | (uint8_t)payload_len));
  } else if (payload_len <= 65535) {
    frame.push_back((char)(mbit | 126));
    uint16_t be = htons((uint16_t)payload_len);
    frame.append(reinterpret_cast<char *>(&be), 2);
  } else {
    frame.push_back((char)(mbit | 127));
    uint64_t v = payload_len;
    for (int i = 56; i >= 0; i -= 8)
      frame.push_back((char)((v >> i) & 0xFF));
  }

  if (oversized)
    return frame;

  if (mask) {
    uint8_t mk[4];
    for (auto &b : mk)
      b = stream.ConsumeIntegral<uint8_t>();
    frame.append(reinterpret_cast<char *>(mk), 4);
    for (size_t i = 0; i < payload.size(); i++)
      payload[i] ^= mk[i % 4];
  }
  frame += payload;
  return frame;
}

static std::string build_incomplete_fragment(size_t payload_len) {
  std::string frame;
  frame.push_back((char)0x00);
  frame.push_back((char)127);
  uint64_t v = payload_len;
  for (int i = 56; i >= 0; i -= 8)
    frame.push_back((char)((v >> i) & 0xFF));
  frame.resize(frame.size() + payload_len, 0);
  return frame;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {

  static const int threads_rv = evthread_use_pthreads();
  (void)threads_rv;

  FuzzedDataProvider stream(data, size);

  int options = stream.ConsumeIntegral<int>();

  bool with_upgrade = stream.ConsumeBool();
  bool with_connection = stream.ConsumeBool();
  bool with_key = stream.ConsumeBool();
  bool with_protocol = stream.ConsumeBool();

  std::string upgrade_val;
  if (with_upgrade) {
    if (stream.ConsumeBool()) {
      upgrade_val = "websocket";
    } else {
      size_t len = stream.ConsumeIntegralInRange<size_t>(0, 16);
      upgrade_val = sanitize_header_value(stream.ConsumeBytesAsString(len));
    }
  }

  std::string connection_val;
  if (with_connection) {
    if (stream.ConsumeBool()) {
      connection_val = "Upgrade";
    } else {
      size_t len = stream.ConsumeIntegralInRange<size_t>(0, 16);
      connection_val = sanitize_header_value(stream.ConsumeBytesAsString(len));
    }
  }

  size_t key_len = stream.ConsumeIntegralInRange<size_t>(0, 64);
  std::string ws_key =
      sanitize_header_value(stream.ConsumeBytesAsString(key_len));

  size_t proto_len = stream.ConsumeIntegralInRange<size_t>(0, 32);
  std::string ws_protocol =
      sanitize_header_value(stream.ConsumeBytesAsString(proto_len));

  std::string extra_headers = stream.ConsumeRandomLengthString(256);

  std::string req_str = "GET /ws HTTP/1.1\r\n";
  if (with_upgrade)
    req_str += "Upgrade: " + upgrade_val + "\r\n";
  if (with_connection)
    req_str += "Connection: " + connection_val + "\r\n";
  if (with_key)
    req_str += "Sec-WebSocket-Key: " + ws_key + "\r\n";
  if (with_protocol)
    req_str += "Sec-WebSocket-Protocol: " + ws_protocol + "\r\n";
  req_str += extra_headers;
  req_str += "\r\n";

  struct event_base *base = event_base_new();
  if (!base)
    return 0;

  struct evhttp *http = evhttp_new(base);
  if (!http) {
    event_base_free(base);
    return 0;
  }

  struct evhttp_connection *evcon =
      evhttp_connection_base_new(base, NULL, "127.0.0.1", 0);
  if (!evcon) {
    evhttp_free(http);
    event_base_free(base);
    return 0;
  }

  evcon->http_server = http;
  TAILQ_INSERT_TAIL(&http->connections, evcon, next);
  http->connection_cnt++;

  struct evhttp_request *req = evhttp_request_new(NULL, NULL);
  if (req) {
    req->kind = EVHTTP_REQUEST;
    req->evcon = evcon;

    TAILQ_INSERT_TAIL(&evcon->requests, req, next);

    struct evbuffer *buf = evbuffer_new();
    if (buf) {
      evbuffer_add(buf, req_str.data(), req_str.size());

      enum message_read_status fl_status =
          evhttp_parse_firstline_(req, buf);
      if (fl_status == ALL_DATA_READ) {
        enum message_read_status hdr_status =
            evhttp_parse_headers_(req, buf);
        if (hdr_status == ALL_DATA_READ) {

          struct evws_connection *evws =
              evws_new_session(req, fuzz_ws_on_msg_cb, nullptr, options);

          if (evws) {

            if (stream.ConsumeBool())
              evws_connection_set_closecb(evws, fuzz_ws_on_close_cb, nullptr);

            struct bufferevent *bev = evws_connection_get_bufferevent(evws);
            if (bev) {

              size_t frame_len = stream.ConsumeIntegralInRange<size_t>(0, 512);
              std::string frame_bytes = stream.ConsumeBytesAsString(frame_len);
              if (!frame_bytes.empty()) {
                struct evbuffer *in = bufferevent_get_input(bev);
                size_t split = stream.ConsumeIntegralInRange<size_t>(
                    0, frame_bytes.size());
                evbuffer_add(in, frame_bytes.data(), split);
                bufferevent_trigger(bev, EV_READ, 0);
                evbuffer_add(in, frame_bytes.data() + split,
                              frame_bytes.size() - split);
                bufferevent_trigger(bev, EV_READ, 0);
              }

              if (stream.ConsumeBool()) {
                int n_frames = stream.ConsumeIntegralInRange<int>(1, 4);
                std::string structured;
                for (int i = 0; i < n_frames; i++)
                  structured += build_ws_frame(stream);
                struct evbuffer *in = bufferevent_get_input(bev);
                evbuffer_add(in, structured.data(), structured.size());
                bufferevent_trigger(bev, EV_READ, 0);
              }

              if (stream.ConsumeIntegralInRange<int>(0, 63) == 0) {
                struct evbuffer *in = bufferevent_get_input(bev);

                std::string big1 = build_incomplete_fragment(10000000);
                evbuffer_add(in, big1.data(), big1.size());
                bufferevent_trigger(bev, EV_READ, 0);

                std::string big2 = build_incomplete_fragment(600000);
                evbuffer_add(in, big2.data(), big2.size());
                bufferevent_trigger(bev, EV_READ, 0);
              }

              if (stream.ConsumeBool()) {
                size_t target_len = stream.ConsumeIntegralInRange<size_t>(0, 70000);
                size_t real_n = std::min<size_t>(
                    target_len, std::min<size_t>(stream.remaining_bytes(), 2000));
                std::string payload = stream.ConsumeBytesAsString(real_n);
                if (payload.size() < target_len)
                  payload.resize(target_len, 'A');

                if (stream.ConsumeBool()) {

                  evws_send_text(evws, payload.c_str());
                } else {
                  evws_send_binary(evws, payload.data(), payload.size());
                }
              }

              bool session_freed = false;

              if (stream.ConsumeBool()) {
                evws_close(evws, stream.ConsumeIntegral<uint16_t>());

                if (stream.ConsumeBool()) {

                  bufferevent_trigger(bev, EV_WRITE, 0);
                }

                if (!session_freed && stream.ConsumeBool()) {

                  evws_close(evws, stream.ConsumeIntegral<uint16_t>());
                }

                if (!session_freed && stream.ConsumeBool()) {

                  evbuffer_drain(bufferevent_get_output(bev), -1);
                  bufferevent_trigger(bev, EV_WRITE, 0);
                  session_freed = true;
                }
              }

              if (!session_freed && stream.ConsumeBool()) {
                bool drain_before_eof = stream.ConsumeBool();
                if (drain_before_eof)
                  evbuffer_drain(bufferevent_get_output(bev), -1);
                bufferevent_trigger_event(bev, BEV_EVENT_EOF, 0);
                session_freed = true;
              }
              (void)session_freed;
            }
          }
        }
      }
      evbuffer_free(buf);
    }
  }

  evhttp_free(http);
  event_base_free(base);

  return 0;
}