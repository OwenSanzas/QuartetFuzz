#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include <fuzzer/FuzzedDataProvider.h>
#include <nghttp2/nghttp2.h>

namespace {

int on_frame_recv_callback(nghttp2_session *, const nghttp2_frame *, void *) {
  return 0;
}

int on_begin_headers_callback(nghttp2_session *, const nghttp2_frame *,
                               void *) {
  return 0;
}

int on_header_callback2(nghttp2_session *, const nghttp2_frame *,
                         nghttp2_rcbuf *name, nghttp2_rcbuf *value, uint8_t,
                         void *) {

  nghttp2_vec nv = nghttp2_rcbuf_get_buf(name);
  nghttp2_vec vv = nghttp2_rcbuf_get_buf(value);
  volatile uint8_t sink = 0;
  for (size_t i = 0; i < nv.len; i++) {
    sink ^= nv.base[i];
  }
  for (size_t i = 0; i < vv.len; i++) {
    sink ^= vv.base[i];
  }
  (void)sink;
  return 0;
}

int on_invalid_frame_recv_callback(nghttp2_session *, const nghttp2_frame *,
                                    int, void *) {
  return 0;
}

int on_data_chunk_recv_callback(nghttp2_session *session, uint8_t,
                                 int32_t stream_id, const uint8_t *data,
                                 size_t len, void *) {
  volatile uint8_t sink = 0;
  for (size_t i = 0; i < len; i++) {
    sink ^= data[i];
  }
  (void)sink;

  nghttp2_session_consume(session, stream_id, len);
  return 0;
}

int on_stream_close_callback(nghttp2_session *, int32_t, uint32_t, void *) {
  return 0;
}

int before_frame_send_callback(nghttp2_session *, const nghttp2_frame *,
                                void *) {
  return 0;
}

int on_frame_send_callback(nghttp2_session *, const nghttp2_frame *, void *) {
  return 0;
}

void send_pending(nghttp2_session *session) {
  for (;;) {
    const uint8_t *data;
    nghttp2_ssize n = nghttp2_session_mem_send2(session, &data);
    if (n <= 0) {
      return;
    }
  }
}

}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  FuzzedDataProvider fdp(data, size);

  uint8_t ctrl = fdp.ConsumeIntegral<uint8_t>();
  bool is_server = (ctrl & 0x1) != 0;
  bool no_recv_client_magic = (ctrl & 0x2) != 0;
  bool no_auto_window_update = (ctrl & 0x4) != 0;
  bool use_upgrade = (ctrl & 0x8) != 0;
  bool head_request = (ctrl & 0x10) != 0;
  bool limit_outbound_queue = (ctrl & 0x20) != 0;

  uint32_t max_concurrent_streams = fdp.ConsumeIntegral<uint32_t>();
  uint32_t initial_window_size = fdp.ConsumeIntegral<uint32_t>();
  uint32_t max_frame_size = fdp.ConsumeIntegral<uint32_t>();
  uint32_t queue_size_ctrl = fdp.ConsumeIntegral<uint32_t>();

  uint8_t num_chunks_hint = fdp.ConsumeIntegral<uint8_t>();
  std::vector<uint8_t> payload = fdp.ConsumeRemainingBytes<uint8_t>();

  nghttp2_option *option = nullptr;
  nghttp2_option_new(&option);
  if (option != nullptr) {
    nghttp2_option_set_no_auto_window_update(option,
                                              no_auto_window_update ? 1 : 0);
    nghttp2_option_set_no_recv_client_magic(option,
                                             no_recv_client_magic ? 1 : 0);
    if (limit_outbound_queue) {

      nghttp2_option_set_max_outbound_queue_size(
          option, 1 + (queue_size_ctrl % 64));
    }
  }

  nghttp2_session_callbacks *callbacks;
  nghttp2_session_callbacks_new(&callbacks);
  nghttp2_session_callbacks_set_on_frame_recv_callback(callbacks,
                                                        on_frame_recv_callback);
  nghttp2_session_callbacks_set_on_begin_headers_callback(
      callbacks, on_begin_headers_callback);
  nghttp2_session_callbacks_set_on_header_callback2(callbacks,
                                                     on_header_callback2);
  nghttp2_session_callbacks_set_on_invalid_frame_recv_callback(
      callbacks, on_invalid_frame_recv_callback);
  nghttp2_session_callbacks_set_on_data_chunk_recv_callback(
      callbacks, on_data_chunk_recv_callback);
  nghttp2_session_callbacks_set_on_stream_close_callback(
      callbacks, on_stream_close_callback);
  nghttp2_session_callbacks_set_before_frame_send_callback(
      callbacks, before_frame_send_callback);
  nghttp2_session_callbacks_set_on_frame_send_callback(callbacks,
                                                        on_frame_send_callback);

  nghttp2_session *session = nullptr;
  int rv;
  if (is_server) {
    rv = nghttp2_session_server_new2(&session, callbacks, nullptr, option);
  } else {
    rv = nghttp2_session_client_new2(&session, callbacks, nullptr, option);
  }

  nghttp2_session_callbacks_del(callbacks);
  if (option != nullptr) {
    nghttp2_option_del(option);
  }

  if (rv != 0 || session == nullptr) {
    return 0;
  }

  nghttp2_settings_entry iv[3];
  iv[0].settings_id = NGHTTP2_SETTINGS_MAX_CONCURRENT_STREAMS;
  iv[0].value = max_concurrent_streams;
  iv[1].settings_id = NGHTTP2_SETTINGS_INITIAL_WINDOW_SIZE;
  iv[1].value = initial_window_size;
  iv[2].settings_id = NGHTTP2_SETTINGS_MAX_FRAME_SIZE;

  iv[2].value = 16384 + (max_frame_size % (16777215 - 16384 + 1));

  nghttp2_submit_settings(session, NGHTTP2_FLAG_NONE, iv, 3);
  send_pending(session);

  if (use_upgrade && payload.size() >= 6) {
    size_t upgrade_len = payload.size() < 64 ? payload.size() : 64;
    nghttp2_session_upgrade2(session, payload.data(), upgrade_len,
                              head_request ? 1 : 0, nullptr);
  }

  size_t total = payload.size();
  size_t offset = 0;
  int num_chunks = (num_chunks_hint % 4) + 1;
  size_t base_chunk =
      num_chunks > 0 ? (total / static_cast<size_t>(num_chunks)) : total;
  if (base_chunk == 0) {
    base_chunk = total;
  }

  while (offset < total) {
    size_t remaining = total - offset;
    size_t this_chunk = base_chunk < remaining ? base_chunk : remaining;
    if (this_chunk == 0) {
      this_chunk = remaining;
    }
    nghttp2_ssize nread = nghttp2_session_mem_recv2(
        session, payload.data() + offset, this_chunk);
    if (nread < 0) {
      if (nghttp2_is_fatal(static_cast<int>(nread))) {
        break;
      }

      break;
    }
    if (nread == 0) {
      break;
    }
    offset += static_cast<size_t>(nread);
    send_pending(session);
  }

  nghttp2_session_mem_recv2(session, nullptr, 0);

  send_pending(session);

  nghttp2_session_want_read(session);
  nghttp2_session_want_write(session);

  nghttp2_session_del(session);

  return 0;
}
