#include <fuzzer/FuzzedDataProvider.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include <nghttp2/nghttp2.h>

namespace {

int on_frame_recv_callback(nghttp2_session *session, const nghttp2_frame *frame,
                           void *user_data) {
  return 0;
}

int on_begin_headers_callback(nghttp2_session *session,
                              const nghttp2_frame *frame, void *user_data) {
  return 0;
}

int on_header_callback2(nghttp2_session *session, const nghttp2_frame *frame,
                        nghttp2_rcbuf *name, nghttp2_rcbuf *value,
                        uint8_t flags, void *user_data) {
  return 0;
}

int before_frame_send_callback(nghttp2_session *session,
                               const nghttp2_frame *frame, void *user_data) {
  return 0;
}

int on_frame_send_callback(nghttp2_session *session, const nghttp2_frame *frame,
                           void *user_data) {
  return 0;
}

int on_data_chunk_recv_callback(nghttp2_session *session, uint8_t flags,
                                 int32_t stream_id, const uint8_t *data,
                                 size_t len, void *user_data) {
  return 0;
}

int on_invalid_frame_recv_callback(nghttp2_session *session,
                                    const nghttp2_frame *frame,
                                    int lib_error_code, void *user_data) {
  return 0;
}

int on_stream_close_callback(nghttp2_session *session, int32_t stream_id,
                             uint32_t error_code, void *user_data) {
  return 0;
}

int on_begin_frame_callback(nghttp2_session *session,
                            const nghttp2_frame_hd *hd, void *user_data) {
  return 0;
}

int error_callback2(nghttp2_session *session, int lib_error_code,
                    const char *msg, size_t len, void *user_data) {
  return 0;
}

int unpack_extension_callback(nghttp2_session *session, void **payload,
                              const nghttp2_frame_hd *hd, void *user_data) {
  return 0;
}

nghttp2_ssize pack_extension_callback(nghttp2_session *session, uint8_t *buf,
                                       size_t len, const nghttp2_frame *frame,
                                       void *user_data) {
  auto *payload = static_cast<std::string *>(frame->ext.payload);
  if (payload == nullptr) {
    return 0;
  }
  size_t n = std::min(len, payload->size());
  if (n > 0) {
    memcpy(buf, payload->data(), n);
  }
  return static_cast<nghttp2_ssize>(n);
}

nghttp2_ssize select_padding_callback(nghttp2_session *session,
                                       const nghttp2_frame *frame,
                                       size_t max_payloadlen,
                                       void *user_data) {
  size_t padded = frame->hd.length + 8;
  if (padded > max_payloadlen) {
    padded = max_payloadlen;
  }
  if (padded < frame->hd.length) {
    padded = frame->hd.length;
  }
  return static_cast<nghttp2_ssize>(padded);
}

struct DataSrc {
  std::string body;
  size_t offset = 0;
};

nghttp2_ssize data_read_cb(nghttp2_session *session, int32_t stream_id,
                            uint8_t *buf, size_t length,
                            uint32_t *data_flags, nghttp2_data_source *source,
                            void *user_data) {
  auto *ds = static_cast<DataSrc *>(source->ptr);
  size_t remaining = ds->body.size() - ds->offset;
  size_t n = length < remaining ? length : remaining;
  if (n > 0) {
    memcpy(buf, ds->body.data() + ds->offset, n);
    ds->offset += n;
  }
  if (ds->offset >= ds->body.size()) {
    *data_flags |= NGHTTP2_DATA_FLAG_EOF;
  }
  return static_cast<nghttp2_ssize>(n);
}

void set_callbacks(nghttp2_session_callbacks *callbacks) {
  nghttp2_session_callbacks_set_on_frame_recv_callback(callbacks,
                                                       on_frame_recv_callback);
  nghttp2_session_callbacks_set_on_begin_headers_callback(
    callbacks, on_begin_headers_callback);
  nghttp2_session_callbacks_set_on_header_callback2(callbacks,
                                                    on_header_callback2);
  nghttp2_session_callbacks_set_before_frame_send_callback(
    callbacks, before_frame_send_callback);
  nghttp2_session_callbacks_set_on_frame_send_callback(callbacks,
                                                       on_frame_send_callback);
  nghttp2_session_callbacks_set_on_data_chunk_recv_callback(
    callbacks, on_data_chunk_recv_callback);
  nghttp2_session_callbacks_set_on_invalid_frame_recv_callback(
    callbacks, on_invalid_frame_recv_callback);
  nghttp2_session_callbacks_set_on_stream_close_callback(
    callbacks, on_stream_close_callback);
  nghttp2_session_callbacks_set_on_begin_frame_callback(
    callbacks, on_begin_frame_callback);
  nghttp2_session_callbacks_set_error_callback2(callbacks, error_callback2);
  nghttp2_session_callbacks_set_unpack_extension_callback(
    callbacks, unpack_extension_callback);
  nghttp2_session_callbacks_set_pack_extension_callback2(
    callbacks, pack_extension_callback);
  nghttp2_session_callbacks_set_select_padding_callback2(
    callbacks, select_padding_callback);
}

void pump(nghttp2_session *src, nghttp2_session *dst) {
  for (;;) {
    const uint8_t *out;
    nghttp2_ssize n = nghttp2_session_mem_send2(src, &out);
    if (n <= 0) {
      return;
    }
    nghttp2_session_mem_recv2(dst, out, static_cast<size_t>(n));
  }
}

std::string consume_token(FuzzedDataProvider &fdp, size_t max_len) {
  size_t len = fdp.ConsumeIntegralInRange<size_t>(0, max_len);
  return fdp.ConsumeBytesAsString(len);
}

std::string consume_padded_token(FuzzedDataProvider &fdp, size_t max_len) {
  size_t target_len = fdp.ConsumeIntegralInRange<size_t>(0, max_len);
  size_t seed_len = fdp.ConsumeIntegralInRange<size_t>(0, 32);
  std::string seed = fdp.ConsumeBytesAsString(seed_len);
  if (seed.empty()) {
    return std::string(target_len, 'a');
  }
  std::string out;
  out.reserve(target_len);
  while (out.size() < target_len) {
    out += seed;
  }
  out.resize(target_len);
  return out;
}

static const char *kSpecialHeaderNames[] = {
  "content-length", "te",       "priority",          "connection",
  "keep-alive",      "proxy-connection", "transfer-encoding", "upgrade",
  "host",
};
constexpr size_t kNumSpecialHeaderNames =
  sizeof(kSpecialHeaderNames) / sizeof(kSpecialHeaderNames[0]);

static const char *kMethods[] = {
  "GET", "POST", "HEAD", "OPTIONS", "CONNECT", "PUT", "DELETE",
};
constexpr size_t kNumMethods = sizeof(kMethods) / sizeof(kMethods[0]);

static const char *kStatuses[] = {
  "100", "103", "200", "204", "304", "404", "500",
};
constexpr size_t kNumStatuses = sizeof(kStatuses) / sizeof(kStatuses[0]);

std::vector<nghttp2_nv> build_headers(FuzzedDataProvider &fdp, bool is_request,
                                      std::vector<std::string> &storage) {
  if (is_request) {
    const char *method =
      kMethods[fdp.ConsumeIntegralInRange<size_t>(0, kNumMethods - 1)];
    bool is_connect = !strcmp(method, "CONNECT");

    storage.push_back(":method");
    storage.push_back(method);

    if (is_connect) {

      storage.push_back(":authority");
      storage.push_back("example.com:443");
      if (fdp.ConsumeBool()) {
        storage.push_back(":protocol");
        storage.push_back(fdp.ConsumeBool() ? "websocket" : consume_token(fdp, 16));

        if (fdp.ConsumeBool()) {
          storage.push_back(":scheme");
          storage.push_back("https");
          storage.push_back(":path");
          storage.push_back(fdp.ConsumeBool() ? "*" : "/" + consume_token(fdp, 16));
        }
      }
    } else {
      storage.push_back(":scheme");
      storage.push_back(fdp.ConsumeBool() ? "https" : "http");
      storage.push_back(":authority");
      storage.push_back("example.com");
      storage.push_back(":path");
      if (fdp.ConsumeIntegralInRange<int>(0, 9) == 0) {
        storage.push_back("*");
      } else {
        storage.push_back("/" + consume_token(fdp, 16));
      }
    }
  } else {
    const char *status =
      kStatuses[fdp.ConsumeIntegralInRange<size_t>(0, kNumStatuses - 1)];
    storage.push_back(":status");
    storage.push_back(status);
  }

  size_t extra = fdp.ConsumeIntegralInRange<size_t>(0, 6);
  for (size_t i = 0; i < extra; i++) {
    storage.push_back(consume_token(fdp, 16));
    storage.push_back(consume_token(fdp, 48));
  }

  size_t special_count = fdp.ConsumeIntegralInRange<size_t>(0, 3);
  for (size_t i = 0; i < special_count; i++) {
    const char *name = kSpecialHeaderNames[fdp.ConsumeIntegralInRange<size_t>(
      0, kNumSpecialHeaderNames - 1)];
    storage.push_back(name);
    if (!strcmp(name, "content-length")) {
      if (fdp.ConsumeBool()) {
        storage.push_back(
          std::to_string(fdp.ConsumeIntegralInRange<uint64_t>(0, 1ULL << 40)));
      } else {

        storage.push_back(consume_token(fdp, 16));
      }
    } else if (!strcmp(name, "te")) {
      storage.push_back(fdp.ConsumeBool() ? "trailers" : consume_token(fdp, 16));
    } else if (!strcmp(name, "priority")) {
      storage.push_back(fdp.ConsumeBool() ? "u=3, i" : consume_token(fdp, 16));
    } else {
      storage.push_back(consume_token(fdp, 16));
    }
  }

  if (fdp.ConsumeIntegralInRange<int>(0, 3) == 0) {
    storage.push_back("x-large");
    storage.push_back(consume_padded_token(fdp, 50000));
  }

  std::vector<nghttp2_nv> nva;
  for (size_t i = 0; i + 1 < storage.size(); i += 2) {
    nghttp2_nv nv;
    nv.name = reinterpret_cast<uint8_t *>(const_cast<char *>(storage[i].data()));
    nv.namelen = storage[i].size();
    nv.value =
      reinterpret_cast<uint8_t *>(const_cast<char *>(storage[i + 1].data()));
    nv.valuelen = storage[i + 1].size();
    nv.flags = NGHTTP2_NV_FLAG_NONE;
    nva.push_back(nv);
  }
  return nva;
}

void inject_raw_priority_frame(FuzzedDataProvider &fdp, nghttp2_session *target,
                               int32_t stream_id) {
  if (stream_id < 0) {
    stream_id = 0;
  }
  uint8_t frame[9 + 5];

  frame[0] = 0;
  frame[1] = 0;
  frame[2] = 5;
  frame[3] = 0x02;
  frame[4] = 0;
  uint32_t sid = static_cast<uint32_t>(stream_id);
  frame[5] = static_cast<uint8_t>((sid >> 24) & 0xff);
  frame[6] = static_cast<uint8_t>((sid >> 16) & 0xff);
  frame[7] = static_cast<uint8_t>((sid >> 8) & 0xff);
  frame[8] = static_cast<uint8_t>(sid & 0xff);

  uint32_t dep = fdp.ConsumeIntegral<uint32_t>();
  frame[9] = static_cast<uint8_t>((dep >> 24) & 0xff);
  frame[10] = static_cast<uint8_t>((dep >> 16) & 0xff);
  frame[11] = static_cast<uint8_t>((dep >> 8) & 0xff);
  frame[12] = static_cast<uint8_t>(dep & 0xff);
  frame[13] = fdp.ConsumeIntegral<uint8_t>();

  nghttp2_session_mem_recv2(target, frame, sizeof(frame));
}

void inject_raw_window_update_zero(nghttp2_session *target, int32_t stream_id) {
  if (stream_id < 0) {
    stream_id = 0;
  }
  uint8_t frame[9 + 4];
  frame[0] = 0;
  frame[1] = 0;
  frame[2] = 4;
  frame[3] = 0x08;
  frame[4] = 0;
  uint32_t sid = static_cast<uint32_t>(stream_id);
  frame[5] = static_cast<uint8_t>((sid >> 24) & 0xff);
  frame[6] = static_cast<uint8_t>((sid >> 16) & 0xff);
  frame[7] = static_cast<uint8_t>((sid >> 8) & 0xff);
  frame[8] = static_cast<uint8_t>(sid & 0xff);

  frame[9] = 0;
  frame[10] = 0;
  frame[11] = 0;
  frame[12] = 0;

  nghttp2_session_mem_recv2(target, frame, sizeof(frame));
}

void inject_raw_settings_bad_length(FuzzedDataProvider &fdp,
                                    nghttp2_session *target) {
  uint8_t payload_len = fdp.ConsumeIntegralInRange<uint8_t>(1, 5);
  std::vector<uint8_t> frame(9 + payload_len);
  frame[0] = 0;
  frame[1] = 0;
  frame[2] = payload_len;
  frame[3] = 0x04;
  frame[4] = 0;
  frame[5] = 0;
  frame[6] = 0;
  frame[7] = 0;
  frame[8] = 0;
  for (uint8_t i = 0; i < payload_len; i++) {
    frame[9 + i] = fdp.ConsumeIntegral<uint8_t>();
  }
  nghttp2_session_mem_recv2(target, frame.data(), frame.size());
}

void inject_raw_ping_bad_length(FuzzedDataProvider &fdp,
                                nghttp2_session *target) {
  uint8_t payload_len = fdp.ConsumeIntegralInRange<uint8_t>(0, 7);
  std::vector<uint8_t> frame(9 + payload_len);
  frame[0] = 0;
  frame[1] = 0;
  frame[2] = payload_len;
  frame[3] = 0x06;
  frame[4] = 0;
  frame[5] = 0;
  frame[6] = 0;
  frame[7] = 0;
  frame[8] = 0;
  for (uint8_t i = 0; i < payload_len; i++) {
    frame[9 + i] = fdp.ConsumeIntegral<uint8_t>();
  }
  nghttp2_session_mem_recv2(target, frame.data(), frame.size());
}

void inject_raw_rst_stream_zero_stream(FuzzedDataProvider &fdp,
                                       nghttp2_session *target) {
  uint8_t frame[9 + 4];
  frame[0] = 0;
  frame[1] = 0;
  frame[2] = 4;
  frame[3] = 0x03;
  frame[4] = 0;
  frame[5] = 0;
  frame[6] = 0;
  frame[7] = 0;
  frame[8] = 0;
  uint32_t err = fdp.ConsumeIntegral<uint32_t>();
  frame[9] = static_cast<uint8_t>((err >> 24) & 0xff);
  frame[10] = static_cast<uint8_t>((err >> 16) & 0xff);
  frame[11] = static_cast<uint8_t>((err >> 8) & 0xff);
  frame[12] = static_cast<uint8_t>(err & 0xff);

  nghttp2_session_mem_recv2(target, frame, sizeof(frame));
}

}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  FuzzedDataProvider fdp(data, size);

  bool as_client = fdp.ConsumeBool();

  nghttp2_session_callbacks *callbacks;
  nghttp2_session_callbacks_new(&callbacks);
  set_callbacks(callbacks);

  nghttp2_option *option;
  nghttp2_option_new(&option);
  nghttp2_option_set_builtin_recv_extension_type(option, NGHTTP2_ALTSVC);
  nghttp2_option_set_builtin_recv_extension_type(option, NGHTTP2_ORIGIN);
  nghttp2_option_set_builtin_recv_extension_type(option,
                                                 NGHTTP2_PRIORITY_UPDATE);
  nghttp2_option_set_user_recv_extension_type(option, 30);

  nghttp2_option_set_max_continuations(option, 2);
  nghttp2_option_set_stream_reset_rate_limit(option, 3, 1000000);
  nghttp2_option_set_max_outbound_ack(option, 2);
  nghttp2_option_set_max_settings(option, 2);

  nghttp2_session *target;
  nghttp2_session *peer;

  if (as_client) {
    nghttp2_session_client_new2(&target, callbacks, nullptr, option);
    nghttp2_session_server_new2(&peer, callbacks, nullptr, option);
  } else {
    nghttp2_session_server_new2(&target, callbacks, nullptr, option);
    nghttp2_session_client_new2(&peer, callbacks, nullptr, option);
  }

  if (fdp.ConsumeBool()) {
    size_t n_entries = fdp.ConsumeIntegralInRange<size_t>(0, 4);
    std::string settings_payload;
    for (size_t i = 0; i < n_entries; i++) {
      uint16_t id = fdp.ConsumeIntegral<uint16_t>();
      uint32_t val = fdp.ConsumeIntegral<uint32_t>();
      settings_payload.push_back(static_cast<char>((id >> 8) & 0xff));
      settings_payload.push_back(static_cast<char>(id & 0xff));
      settings_payload.push_back(static_cast<char>((val >> 24) & 0xff));
      settings_payload.push_back(static_cast<char>((val >> 16) & 0xff));
      settings_payload.push_back(static_cast<char>((val >> 8) & 0xff));
      settings_payload.push_back(static_cast<char>(val & 0xff));
    }
    int head_request = fdp.ConsumeBool() ? 1 : 0;
    nghttp2_session_upgrade2(
      target, reinterpret_cast<const uint8_t *>(settings_payload.data()),
      settings_payload.size(), head_request, nullptr);
  }

  {
    size_t early_raw_len = fdp.ConsumeIntegralInRange<size_t>(0, 48);
    std::vector<uint8_t> early_raw = fdp.ConsumeBytes<uint8_t>(early_raw_len);
    if (!early_raw.empty()) {
      nghttp2_session_mem_recv2(target, early_raw.data(), early_raw.size());
    }
  }

  nghttp2_option_del(option);
  nghttp2_session_callbacks_del(callbacks);

  uint32_t max_concurrent_streams =
      fdp.ConsumeIntegralInRange<uint32_t>(0, 1000);
  nghttp2_settings_entry iv{NGHTTP2_SETTINGS_MAX_CONCURRENT_STREAMS,
                             max_concurrent_streams};
  nghttp2_submit_settings(target, NGHTTP2_FLAG_NONE, &iv, 1);

  nghttp2_submit_settings(peer, NGHTTP2_FLAG_NONE, nullptr, 0);
  pump(peer, target);
  pump(target, peer);

  std::vector<std::unique_ptr<DataSrc>> data_sources;

  std::vector<std::unique_ptr<std::string>> ext_payloads;

  int32_t client_stream_id = -1;

  static const int32_t kSettingsIds[] = {
    NGHTTP2_SETTINGS_HEADER_TABLE_SIZE,
    NGHTTP2_SETTINGS_ENABLE_PUSH,
    NGHTTP2_SETTINGS_MAX_CONCURRENT_STREAMS,
    NGHTTP2_SETTINGS_INITIAL_WINDOW_SIZE,
    NGHTTP2_SETTINGS_MAX_FRAME_SIZE,
    NGHTTP2_SETTINGS_MAX_HEADER_LIST_SIZE,
    NGHTTP2_SETTINGS_ENABLE_CONNECT_PROTOCOL,
    NGHTTP2_SETTINGS_NO_RFC7540_PRIORITIES,
  };
  constexpr size_t kNumSettingsIds =
    sizeof(kSettingsIds) / sizeof(kSettingsIds[0]);

  int rounds = fdp.ConsumeIntegralInRange<int>(1, 12);
  for (int r = 0; r < rounds && fdp.remaining_bytes() > 0; r++) {
    nghttp2_session *client_sess = as_client ? target : peer;
    nghttp2_session *server_sess = as_client ? peer : target;

    int action = fdp.ConsumeIntegralInRange<int>(0, 18);
    switch (action) {
    case 0: {
      std::vector<std::string> storage;
      auto nva = build_headers(fdp,  true, storage);
      nghttp2_priority_spec pri;
      nghttp2_priority_spec_default_init(&pri);

      nghttp2_data_provider2 dp;
      const nghttp2_data_provider2 *dp_ptr = nullptr;
      if (fdp.ConsumeBool()) {
        auto ds = std::make_unique<DataSrc>();
        ds->body = consume_token(fdp, 256);
        dp.source.ptr = ds.get();
        dp.read_callback = data_read_cb;
        dp_ptr = &dp;
        data_sources.push_back(std::move(ds));
      }

      int32_t sid = nghttp2_submit_request2(client_sess, &pri, nva.data(),
                                            nva.size(), dp_ptr, nullptr);
      if (sid >= 0) {
        client_stream_id = sid;
      }
      break;
    }
    case 1: {
      if (client_stream_id >= 0) {
        std::vector<std::string> storage;
        auto nva = build_headers(fdp,  false, storage);

        nghttp2_data_provider2 dp;
        const nghttp2_data_provider2 *dp_ptr = nullptr;
        if (fdp.ConsumeBool()) {
          auto ds = std::make_unique<DataSrc>();
          ds->body = consume_token(fdp, 256);
          dp.source.ptr = ds.get();
          dp.read_callback = data_read_cb;
          dp_ptr = &dp;
          data_sources.push_back(std::move(ds));
        }

        nghttp2_submit_response2(server_sess, client_stream_id, nva.data(),
                                 nva.size(), dp_ptr);
      }
      break;
    }
    case 2: {
      if (client_stream_id >= 0) {
        auto ds = std::make_unique<DataSrc>();
        ds->body = consume_token(fdp, 256);
        nghttp2_data_provider2 dp;
        dp.source.ptr = ds.get();
        dp.read_callback = data_read_cb;

        nghttp2_session *sess = fdp.ConsumeBool() ? client_sess : server_sess;
        nghttp2_submit_data2(sess, NGHTTP2_FLAG_NONE, client_stream_id, &dp);
        data_sources.push_back(std::move(ds));
      }
      break;
    }
    case 3: {
      if (client_stream_id >= 0) {
        nghttp2_session *sess = fdp.ConsumeBool() ? client_sess : server_sess;
        nghttp2_submit_rst_stream(sess, NGHTTP2_FLAG_NONE, client_stream_id,
                                  fdp.ConsumeIntegral<uint32_t>());
      }
      break;
    }
    case 4: {
      nghttp2_session *sess = fdp.ConsumeBool() ? client_sess : server_sess;
      int32_t sid = fdp.ConsumeBool() ? 0 : client_stream_id;
      nghttp2_submit_window_update(
        sess, NGHTTP2_FLAG_NONE, sid,
        fdp.ConsumeIntegralInRange<int32_t>(0, 1 << 20));
      break;
    }
    case 5: {
      uint8_t opaque[8];
      for (auto &b : opaque) {
        b = fdp.ConsumeIntegral<uint8_t>();
      }
      nghttp2_session *sess = fdp.ConsumeBool() ? client_sess : server_sess;
      nghttp2_submit_ping(sess, NGHTTP2_FLAG_NONE, opaque);
      break;
    }
    case 6: {
      std::string origin_str = consume_token(fdp, 32);
      std::string value = consume_token(fdp, 64);
      int32_t sid = fdp.ConsumeBool() ? 0 : client_stream_id;
      nghttp2_submit_altsvc(
        server_sess, NGHTTP2_FLAG_NONE, sid,
        reinterpret_cast<const uint8_t *>(origin_str.data()),
        origin_str.size(), reinterpret_cast<const uint8_t *>(value.data()),
        value.size());
      break;
    }
    case 7: {
      std::string origin_str = consume_token(fdp, 32);
      nghttp2_origin_entry ent;
      ent.origin = reinterpret_cast<uint8_t *>(const_cast<char *>(origin_str.data()));
      ent.origin_len = origin_str.size();
      nghttp2_submit_origin(server_sess, NGHTTP2_FLAG_NONE, &ent, 1);
      break;
    }
    case 8: {
      if (client_stream_id >= 0) {
        std::string field_value = consume_token(fdp, 32);
        nghttp2_submit_priority_update(
          client_sess, NGHTTP2_FLAG_NONE, client_stream_id,
          reinterpret_cast<const uint8_t *>(field_value.data()),
          field_value.size());
      }
      break;
    }
    case 9: {
      std::string dbg = consume_token(fdp, 32);
      nghttp2_session *sess = fdp.ConsumeBool() ? client_sess : server_sess;
      int32_t last_sid = client_stream_id >= 0 ? client_stream_id : 0;
      nghttp2_submit_goaway(sess, NGHTTP2_FLAG_NONE, last_sid,
                            NGHTTP2_NO_ERROR,
                            reinterpret_cast<const uint8_t *>(dbg.data()),
                            dbg.size());
      break;
    }
    case 10: {

      nghttp2_session *sess = fdp.ConsumeBool() ? client_sess : server_sess;
      std::vector<nghttp2_settings_entry> siv;
      size_t n = fdp.ConsumeIntegralInRange<size_t>(0, 6);
      for (size_t i = 0; i < n; i++) {
        nghttp2_settings_entry e;
        int32_t sid =
          kSettingsIds[fdp.ConsumeIntegralInRange<size_t>(0, kNumSettingsIds - 1)];
        e.settings_id = sid;
        switch (sid) {
        case NGHTTP2_SETTINGS_MAX_FRAME_SIZE:

          e.value = fdp.ConsumeIntegralInRange<uint32_t>(0, 16777215 + 16);
          break;
        case NGHTTP2_SETTINGS_ENABLE_PUSH:
        case NGHTTP2_SETTINGS_ENABLE_CONNECT_PROTOCOL:
        case NGHTTP2_SETTINGS_NO_RFC7540_PRIORITIES:
          e.value = fdp.ConsumeIntegralInRange<uint32_t>(0, 1);
          break;
        default:
          e.value = fdp.ConsumeIntegral<uint32_t>();
          break;
        }
        siv.push_back(e);
      }
      nghttp2_submit_settings(sess, NGHTTP2_FLAG_NONE, siv.data(), siv.size());
      break;
    }
    case 11: {

      if (client_stream_id >= 0) {
        std::vector<std::string> storage;
        auto nva = build_headers(fdp,  true, storage);
        nghttp2_submit_push_promise(server_sess, NGHTTP2_FLAG_NONE,
                                    client_stream_id, nva.data(), nva.size(),
                                    nullptr);
      }
      break;
    }
    case 12: {
      if (client_stream_id >= 0) {
        std::vector<std::string> storage;
        storage.push_back(consume_token(fdp, 16));
        storage.push_back(consume_token(fdp, 48));
        std::vector<nghttp2_nv> nva;
        if (storage[0].empty()) {
          storage[0] = "x-trailer";
        }
        nghttp2_nv nv;
        nv.name =
          reinterpret_cast<uint8_t *>(const_cast<char *>(storage[0].data()));
        nv.namelen = storage[0].size();
        nv.value =
          reinterpret_cast<uint8_t *>(const_cast<char *>(storage[1].data()));
        nv.valuelen = storage[1].size();
        nv.flags = NGHTTP2_NV_FLAG_NONE;
        nva.push_back(nv);
        nghttp2_session *sess = fdp.ConsumeBool() ? client_sess : server_sess;
        nghttp2_submit_trailer(sess, client_stream_id, nva.data(), nva.size());
      }
      break;
    }
    case 13: {

      auto payload = std::make_unique<std::string>(consume_token(fdp, 64));
      nghttp2_session *sess = fdp.ConsumeBool() ? client_sess : server_sess;
      int32_t sid = fdp.ConsumeBool() ? 0 : client_stream_id;
      if (sid < 0) {
        sid = 0;
      }
      nghttp2_submit_extension(sess, 30, 0, sid, payload.get());
      ext_payloads.push_back(std::move(payload));
      break;
    }
    case 14: {

      inject_raw_priority_frame(fdp, target, client_stream_id);
      break;
    }
    case 15: {

      int32_t sid = fdp.ConsumeBool() ? 0 : client_stream_id;
      inject_raw_window_update_zero(target, sid);
      break;
    }
    case 16: {

      inject_raw_settings_bad_length(fdp, target);
      break;
    }
    case 17: {

      inject_raw_ping_bad_length(fdp, target);
      break;
    }
    case 18: {

      inject_raw_rst_stream_zero_stream(fdp, target);
      break;
    }
    }

    pump(peer, target);

    pump(target, peer);

    if (fdp.remaining_bytes() > 0) {
      size_t mid_raw_len = fdp.ConsumeIntegralInRange<size_t>(0, 32);
      std::vector<uint8_t> mid_raw = fdp.ConsumeBytes<uint8_t>(mid_raw_len);
      if (!mid_raw.empty()) {
        nghttp2_session_mem_recv2(target, mid_raw.data(), mid_raw.size());
      }
    }
  }

  std::vector<uint8_t> raw = fdp.ConsumeRemainingBytes<uint8_t>();
  nghttp2_session_mem_recv2(target, raw.data(), raw.size());
  pump(target, peer);

  nghttp2_session_del(peer);
  nghttp2_session_del(target);

  return 0;
}