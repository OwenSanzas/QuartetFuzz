#include <fuzzer/FuzzedDataProvider.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

extern "C" {
#include "libevent/include/event2/event.h"
#include "libevent/include/event2/buffer.h"
#include "libevent/include/event2/tag.h"
#include "libevent/include/event2/util.h"

int evtag_encode_tag(struct evbuffer *evbuf, ev_uint32_t tag);
}

static void ref_cleanup_cb(const void *  , size_t  ,
                            void *  ) {

}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  FuzzedDataProvider stream(data, size);

  struct evbuffer *src = evbuffer_new();
  struct evbuffer *dst = evbuffer_new();
  if (src == nullptr || dst == nullptr) {
    if (src != nullptr) {
      evbuffer_free(src);
    }
    if (dst != nullptr) {
      evbuffer_free(dst);
    }
    return 0;
  }

  enum Mode {
    kWellFormed = 0,
    kRawMalformed,
    kValidTagBadLength,
    kOverflowTag,
    kFrozenDst,
    kFragmentedPayload,
    kNibbleOverflow,
    kLengthExceedsBuffer,
    kEmptySrc,
    kFrozenSrc,
    kHugeLength,
    kZeroLengthPayload,
    kShortLengthBytes,
    kMultiRecordInterleaved,
    kDstPreFilled,
    kSrcReferenceChain,
    kNumModes
  };
  const int mode = stream.ConsumeIntegralInRange<int>(0, kNumModes - 1);
  const bool null_ptag = stream.ConsumeBool();

  switch (mode) {
    case kWellFormed: {

      const ev_uint32_t tag = stream.ConsumeIntegral<ev_uint32_t>();
      std::vector<uint8_t> payload = stream.ConsumeRemainingBytes<uint8_t>();

      evtag_encode_tag(src, tag);
      evtag_encode_int(src, static_cast<ev_uint32_t>(payload.size()));
      if (!payload.empty()) {
        evbuffer_add(src, payload.data(), payload.size());
      }
      break;
    }
    case kRawMalformed: {

      std::vector<uint8_t> raw = stream.ConsumeRemainingBytes<uint8_t>();
      if (!raw.empty()) {
        evbuffer_add(src, raw.data(), raw.size());
      }
      break;
    }
    case kValidTagBadLength: {

      const ev_uint32_t tag = stream.ConsumeIntegral<ev_uint32_t>();
      evtag_encode_tag(src, tag);
      std::vector<uint8_t> length_and_payload =
          stream.ConsumeRemainingBytes<uint8_t>();
      if (!length_and_payload.empty()) {
        evbuffer_add(src, length_and_payload.data(),
                     length_and_payload.size());
      }
      break;
    }
    case kOverflowTag: {

      std::vector<uint8_t> tail = stream.ConsumeRemainingBytes<uint8_t>();
      uint8_t tag_bytes[5] = {0x80, 0x80, 0x80, 0x80, 0x00};
      if (!tail.empty()) {
        tag_bytes[4] = tail[0];
      }
      evbuffer_add(src, tag_bytes, sizeof(tag_bytes));
      if (tail.size() > 1) {
        evbuffer_add(src, tail.data() + 1, tail.size() - 1);
      }
      break;
    }
    case kFrozenDst: {

      const ev_uint32_t tag = stream.ConsumeIntegral<ev_uint32_t>();
      std::vector<uint8_t> payload = stream.ConsumeRemainingBytes<uint8_t>();

      evtag_encode_tag(src, tag);
      evtag_encode_int(src, static_cast<ev_uint32_t>(payload.size()));
      if (!payload.empty()) {
        evbuffer_add(src, payload.data(), payload.size());
      }
      evbuffer_freeze(dst, 0);
      break;
    }
    case kFragmentedPayload: {

      const ev_uint32_t tag = stream.ConsumeIntegral<ev_uint32_t>();
      std::vector<uint8_t> payload = stream.ConsumeRemainingBytes<uint8_t>();
      if (payload.size() < 1200) {
        payload.resize(1200, 0x41);
      }

      evtag_encode_tag(src, tag);
      evtag_encode_int(src, static_cast<ev_uint32_t>(payload.size()));

      size_t off = 0;
      size_t chunk = 600;
      while (off < payload.size()) {
        size_t n = std::min(chunk, payload.size() - off);
        evbuffer_add(src, payload.data() + off, n);
        off += n;
        chunk = std::max<size_t>(1, chunk / 3);
      }
      break;
    }
    case kNibbleOverflow: {

      const ev_uint32_t tag = stream.ConsumeIntegral<ev_uint32_t>();
      evtag_encode_tag(src, tag);
      uint8_t bad_len_byte = 0x80;
      evbuffer_add(src, &bad_len_byte, 1);
      std::vector<uint8_t> tail = stream.ConsumeRemainingBytes<uint8_t>();
      if (!tail.empty()) {
        evbuffer_add(src, tail.data(), tail.size());
      }
      break;
    }
    case kLengthExceedsBuffer: {

      const ev_uint32_t tag = stream.ConsumeIntegral<ev_uint32_t>();
      evtag_encode_tag(src, tag);
      evtag_encode_int(src, 0x7fffffff);
      std::vector<uint8_t> tail = stream.ConsumeRemainingBytes<uint8_t>();
      if (tail.size() > 8) {
        tail.resize(8);
      }
      if (!tail.empty()) {
        evbuffer_add(src, tail.data(), tail.size());
      }
      break;
    }
    case kEmptySrc: {

      break;
    }
    case kFrozenSrc: {

      const ev_uint32_t tag = stream.ConsumeIntegral<ev_uint32_t>();
      std::vector<uint8_t> payload = stream.ConsumeRemainingBytes<uint8_t>();

      evtag_encode_tag(src, tag);
      evtag_encode_int(src, static_cast<ev_uint32_t>(payload.size()));
      if (!payload.empty()) {
        evbuffer_add(src, payload.data(), payload.size());
      }
      evbuffer_freeze(src, 1);
      break;
    }
    case kHugeLength: {

      const ev_uint32_t tag = stream.ConsumeIntegral<ev_uint32_t>();
      evtag_encode_tag(src, tag);
      ev_uint32_t huge = 0x80000000u | stream.ConsumeIntegral<ev_uint32_t>();
      evtag_encode_int(src, huge);
      std::vector<uint8_t> tail = stream.ConsumeRemainingBytes<uint8_t>();
      if (!tail.empty()) {
        evbuffer_add(src, tail.data(), tail.size());
      }
      break;
    }
    case kZeroLengthPayload: {

      const ev_uint32_t tag = stream.ConsumeIntegral<ev_uint32_t>();
      evtag_encode_tag(src, tag);
      evtag_encode_int(src, 0);

      break;
    }
    case kShortLengthBytes: {

      const ev_uint32_t tag = stream.ConsumeIntegral<ev_uint32_t>();
      evtag_encode_tag(src, tag);

      uint8_t len_byte = 0x30;
      evbuffer_add(src, &len_byte, 1);

      break;
    }
    case kMultiRecordInterleaved: {

      int records = stream.ConsumeIntegralInRange<int>(1, 6);
      for (int i = 0; i < records && stream.remaining_bytes() > 0; ++i) {
        const ev_uint32_t tag = stream.ConsumeIntegral<ev_uint32_t>();
        size_t max_chunk = std::min<size_t>(stream.remaining_bytes(), 64);
        size_t chunk_len =
            max_chunk > 0
                ? stream.ConsumeIntegralInRange<size_t>(0, max_chunk)
                : 0;
        std::vector<uint8_t> payload =
            stream.ConsumeBytes<uint8_t>(chunk_len);

        evtag_encode_tag(src, tag);
        evtag_encode_int(src, static_cast<ev_uint32_t>(payload.size()));
        if (!payload.empty()) {
          evbuffer_add(src, payload.data(), payload.size());
        }

        ev_uint32_t out_tag = 0;
        evtag_unmarshal(src, null_ptag ? nullptr : &out_tag, dst);

        evbuffer_drain(dst, evbuffer_get_length(dst));
      }

      break;
    }
    case kDstPreFilled: {

      std::vector<uint8_t> filler = stream.ConsumeBytes<uint8_t>(
          stream.ConsumeIntegralInRange<size_t>(0, 300));
      if (!filler.empty()) {
        evbuffer_add(dst, filler.data(), filler.size());
        size_t drain_n =
            stream.ConsumeIntegralInRange<size_t>(0, filler.size());
        evbuffer_drain(dst, drain_n);
      }

      const ev_uint32_t tag = stream.ConsumeIntegral<ev_uint32_t>();
      std::vector<uint8_t> payload = stream.ConsumeRemainingBytes<uint8_t>();
      evtag_encode_tag(src, tag);
      evtag_encode_int(src, static_cast<ev_uint32_t>(payload.size()));
      if (!payload.empty()) {
        evbuffer_add(src, payload.data(), payload.size());
      }
      break;
    }
    case kSrcReferenceChain: {

      const ev_uint32_t tag = stream.ConsumeIntegral<ev_uint32_t>();
      std::vector<uint8_t> payload = stream.ConsumeRemainingBytes<uint8_t>();
      const bool use_cleanup = stream.ConsumeBool();

      evtag_encode_tag(src, tag);
      evtag_encode_int(src, static_cast<ev_uint32_t>(payload.size()));
      if (!payload.empty()) {
        evbuffer_add_reference(src, payload.data(), payload.size(),
                                use_cleanup ? ref_cleanup_cb : nullptr,
                                nullptr);
      }

      ev_uint32_t out_tag = 0;
      evtag_unmarshal(src, null_ptag ? nullptr : &out_tag, dst);
      evbuffer_free(src);
      evbuffer_free(dst);
      return 0;
    }
    default:
      break;
  }

  ev_uint32_t out_tag = 0;
  evtag_unmarshal(src, null_ptag ? nullptr : &out_tag, dst);

  evbuffer_free(src);
  evbuffer_free(dst);
  return 0;
}