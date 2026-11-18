#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <cstdint>
#include <cstddef>
#include <nghttp2.h>
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <vector>

static int on_header_callback(nghttp2_session *session,
                               const nghttp2_frame *frame,
                               const uint8_t *name, size_t namelen,
                               const uint8_t *value, size_t valuelen,
                               uint8_t flags, void *user_data) {
  (void)session;
  (void)frame;
  (void)name;
  (void)namelen;
  (void)value;
  (void)valuelen;
  (void)flags;
  (void)user_data;
  return 0;
}

static int on_begin_headers_callback(nghttp2_session *session,
                                      const nghttp2_frame *frame,
                                      void *user_data) {
  (void)session;
  (void)frame;
  (void)user_data;
  return 0;
}

static int on_frame_recv_callback(nghttp2_session *session,
                                   const nghttp2_frame *frame,
                                   void *user_data) {
  (void)session;
  (void)frame;
  (void)user_data;
  return 0;
}

static int on_data_chunk_recv_callback(nghttp2_session *session,
                                        uint8_t flags, int32_t stream_id,
                                        const uint8_t *data, size_t len,
                                        void *user_data) {
  (void)session;
  (void)flags;
  (void)stream_id;
  (void)data;
  (void)len;
  (void)user_data;
  return 0;
}

static int on_stream_close_callback(nghttp2_session *session,
                                     int32_t stream_id,
                                     uint32_t error_code,
                                     void *user_data) {
  (void)session;
  (void)stream_id;
  (void)error_code;
  (void)user_data;
  return 0;
}

static int on_invalid_frame_recv_callback(nghttp2_session *session,
                                           const nghttp2_frame *frame,
                                           int lib_error_code,
                                           void *user_data) {
  (void)session;
  (void)frame;
  (void)lib_error_code;
  (void)user_data;
  return 0;
}

static nghttp2_ssize send_callback2(nghttp2_session *session,
                                     const uint8_t *data, size_t length,
                                     int flags, void *user_data) {
  (void)session;
  (void)data;
  (void)flags;
  (void)user_data;
  return (nghttp2_ssize)length;
}

static int on_frame_send_callback(nghttp2_session *session,
                                   const nghttp2_frame *frame,
                                   void *user_data) {
  (void)session;
  (void)frame;
  (void)user_data;
  return 0;
}

static int on_frame_not_send_callback(nghttp2_session *session,
                                       const nghttp2_frame *frame,
                                       int lib_error_code, void *user_data) {
  (void)session;
  (void)frame;
  (void)lib_error_code;
  (void)user_data;
  return 0;
}

static int error_callback2(nghttp2_session *session, int lib_error_code,
                            const char *msg, size_t len, void *user_data) {
  (void)session;
  (void)lib_error_code;
  (void)msg;
  (void)len;
  (void)user_data;
  return 0;
}

static int before_frame_send_callback(nghttp2_session *session,
                                       const nghttp2_frame *frame,
                                       void *user_data) {
  (void)session;
  (void)frame;
  (void)user_data;
  return 0;
}

static int on_begin_frame_callback(nghttp2_session *session,
                                    const nghttp2_frame_hd *hd,
                                    void *user_data) {
  (void)session;
  (void)hd;
  (void)user_data;
  return 0;
}

static void setup_callbacks(nghttp2_session_callbacks *callbacks) {
  nghttp2_session_callbacks_set_on_header_callback(callbacks,
                                                    on_header_callback);
  nghttp2_session_callbacks_set_on_begin_headers_callback(
      callbacks, on_begin_headers_callback);
  nghttp2_session_callbacks_set_on_frame_recv_callback(
      callbacks, on_frame_recv_callback);
  nghttp2_session_callbacks_set_on_data_chunk_recv_callback(
      callbacks, on_data_chunk_recv_callback);
  nghttp2_session_callbacks_set_on_stream_close_callback(
      callbacks, on_stream_close_callback);
  nghttp2_session_callbacks_set_on_invalid_frame_recv_callback(
      callbacks, on_invalid_frame_recv_callback);
  nghttp2_session_callbacks_set_send_callback2(callbacks, send_callback2);
  nghttp2_session_callbacks_set_on_frame_send_callback(
      callbacks, on_frame_send_callback);
  nghttp2_session_callbacks_set_on_frame_not_send_callback(
      callbacks, on_frame_not_send_callback);
  nghttp2_session_callbacks_set_error_callback2(callbacks, error_callback2);
  nghttp2_session_callbacks_set_before_frame_send_callback(
      callbacks, before_frame_send_callback);
  nghttp2_session_callbacks_set_on_begin_frame_callback(
      callbacks, on_begin_frame_callback);
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size) {
  if (Size == 0) {
    return 0;
  }

  nghttp2_session_callbacks *callbacks;
  if (nghttp2_session_callbacks_new(&callbacks) != 0) {
    return 0;
  }

  setup_callbacks(callbacks);

  bool is_server = (Data[0] & 0x1) != 0;

  const uint8_t *payload = Data + 1;
  size_t payload_len = Size - 1;

  nghttp2_session *session = nullptr;
  int rv;

  if (is_server) {
    rv = nghttp2_session_server_new(&session, callbacks, nullptr);
  } else {
    rv = nghttp2_session_client_new(&session, callbacks, nullptr);
  }

  nghttp2_session_callbacks_del(callbacks);

  if (rv != 0 || session == nullptr) {
    return 0;
  }

  size_t offset = 0;
  while (offset < payload_len) {
    size_t chunk_size = payload_len - offset;

    if (chunk_size > 64) {
      chunk_size = 64;
    }

    nghttp2_ssize rv2 = nghttp2_session_mem_recv2(
        session, payload + offset, chunk_size);

    if (rv2 < 0) {

      break;
    }

    if (rv2 == 0) {

      break;
    }

    offset += (size_t)rv2;
  }

  nghttp2_session_send(session);

  nghttp2_session_del(session);

  return 0;
}