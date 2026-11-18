#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

#include "FLAC/stream_decoder.h"

#include "common.h"

namespace {

struct FuzzerClientData {
  const uint8_t *data;
  size_t size;
  size_t pos;
};

FLAC__StreamDecoderReadStatus read_callback_(const FLAC__StreamDecoder *  ,
                                              FLAC__byte buffer[],
                                              size_t *bytes,
                                              void *client_data) {
  FuzzerClientData *fcd = static_cast<FuzzerClientData *>(client_data);

  if (*bytes == 0) {
    return FLAC__STREAM_DECODER_READ_STATUS_CONTINUE;
  }

  if (fcd->pos >= fcd->size) {
    *bytes = 0;
    return FLAC__STREAM_DECODER_READ_STATUS_END_OF_STREAM;
  }

  size_t remaining = fcd->size - fcd->pos;
  size_t to_copy = (*bytes < remaining) ? *bytes : remaining;

  if (to_copy > 0) {
    std::memcpy(buffer, fcd->data + fcd->pos, to_copy);
  }
  fcd->pos += to_copy;
  *bytes = to_copy;

  return FLAC__STREAM_DECODER_READ_STATUS_CONTINUE;
}

FLAC__StreamDecoderWriteStatus write_callback_(const FLAC__StreamDecoder *  ,
                                                const FLAC__Frame *  ,
                                                const FLAC__int32 * const *  ,
                                                void *  ) {
  return FLAC__STREAM_DECODER_WRITE_STATUS_CONTINUE;
}

void metadata_callback_(const FLAC__StreamDecoder *  ,
                         const FLAC__StreamMetadata *  ,
                         void *  ) {

}

void error_callback_(const FLAC__StreamDecoder *  ,
                      FLAC__StreamDecoderErrorStatus  ,
                      void *  ) {

}

uint32_t OggCrc32(const uint8_t *data, size_t len) {
  static uint32_t table[256];
  static bool initialized = false;
  if (!initialized) {
    for (uint32_t i = 0; i < 256; i++) {
      uint32_t crc = i << 24;
      for (int j = 0; j < 8; j++) {
        crc = (crc << 1) ^ ((crc & 0x80000000u) ? 0x04c11db7u : 0u);
      }
      table[i] = crc;
    }
    initialized = true;
  }
  uint32_t crc = 0;
  for (size_t i = 0; i < len; i++) {
    crc = (crc << 8) ^ table[((crc >> 24) & 0xffu) ^ data[i]];
  }
  return crc;
}

class BitWriter {
 public:
  void WriteBits(uint64_t value, int nbits) {
    for (int i = nbits - 1; i >= 0; --i) {
      unsigned bit = static_cast<unsigned>((value >> i) & 1u);
      cur_byte_ = static_cast<uint8_t>((cur_byte_ << 1) | bit);
      if (++bits_in_cur_byte_ == 8) {
        bytes_.push_back(cur_byte_);
        cur_byte_ = 0;
        bits_in_cur_byte_ = 0;
      }
    }
  }

  std::vector<uint8_t> Finish() {
    if (bits_in_cur_byte_ > 0) {
      cur_byte_ = static_cast<uint8_t>(cur_byte_ << (8 - bits_in_cur_byte_));
      bytes_.push_back(cur_byte_);
      cur_byte_ = 0;
      bits_in_cur_byte_ = 0;
    }
    return bytes_;
  }

 private:
  std::vector<uint8_t> bytes_;
  uint8_t cur_byte_ = 0;
  int bits_in_cur_byte_ = 0;
};

std::vector<uint8_t> BuildStreamInfoData() {
  BitWriter bw;
  bw.WriteBits(4096, 16);
  bw.WriteBits(4096, 16);
  bw.WriteBits(0, 24);
  bw.WriteBits(0, 24);
  bw.WriteBits(44100, 20);
  bw.WriteBits(2 - 1, 3);
  bw.WriteBits(16 - 1, 5);
  bw.WriteBits(0, 36);
  std::vector<uint8_t> out = bw.Finish();
  out.resize(out.size() + 16, 0);
  return out;
}

std::vector<uint8_t> BuildNativeFlacHeader() {
  std::vector<uint8_t> out;
  const char magic[4] = {'f', 'L', 'a', 'C'};
  out.insert(out.end(), magic, magic + 4);
  std::vector<uint8_t> si = BuildStreamInfoData();
  out.push_back(0x80);
  uint32_t len = static_cast<uint32_t>(si.size());
  out.push_back(static_cast<uint8_t>((len >> 16) & 0xff));
  out.push_back(static_cast<uint8_t>((len >> 8) & 0xff));
  out.push_back(static_cast<uint8_t>(len & 0xff));
  out.insert(out.end(), si.begin(), si.end());
  return out;
}

std::vector<uint8_t> BuildOggFlacHeaderPacket() {
  std::vector<uint8_t> out;
  out.push_back(0x7f);
  const char magic[4] = {'F', 'L', 'A', 'C'};
  out.insert(out.end(), magic, magic + 4);
  out.push_back(1);
  out.push_back(0);
  out.push_back(0);
  out.push_back(0);
  std::vector<uint8_t> native = BuildNativeFlacHeader();
  out.insert(out.end(), native.begin(), native.end());
  return out;
}

std::vector<uint8_t> BuildSegmentTable(size_t len) {
  std::vector<uint8_t> segs;
  size_t remaining = len;
  while (remaining >= 255) {
    segs.push_back(255);
    remaining -= 255;
  }
  segs.push_back(static_cast<uint8_t>(remaining));
  return segs;
}

constexpr size_t kMaxSyntheticPagePayload = 254 * 255;

std::vector<uint8_t> BuildOggPage(uint32_t serial, uint32_t page_seq,
                                   uint8_t header_type_flags,
                                   const uint8_t *payload,
                                   size_t payload_len) {
  if (payload_len > kMaxSyntheticPagePayload) {
    payload_len = kMaxSyntheticPagePayload;
  }

  std::vector<uint8_t> page;
  const char capture[4] = {'O', 'g', 'g', 'S'};
  page.insert(page.end(), capture, capture + 4);
  page.push_back(0);
  page.push_back(header_type_flags);
  for (int i = 0; i < 8; i++) page.push_back(0);
  for (int i = 0; i < 4; i++) {
    page.push_back(static_cast<uint8_t>((serial >> (8 * i)) & 0xff));
  }
  for (int i = 0; i < 4; i++) {
    page.push_back(static_cast<uint8_t>((page_seq >> (8 * i)) & 0xff));
  }
  const size_t crc_offset = page.size();
  for (int i = 0; i < 4; i++) page.push_back(0);

  std::vector<uint8_t> segs = BuildSegmentTable(payload_len);
  page.push_back(static_cast<uint8_t>(segs.size()));
  page.insert(page.end(), segs.begin(), segs.end());
  page.insert(page.end(), payload, payload + payload_len);

  uint32_t crc = OggCrc32(page.data(), page.size());
  page[crc_offset + 0] = static_cast<uint8_t>(crc & 0xff);
  page[crc_offset + 1] = static_cast<uint8_t>((crc >> 8) & 0xff);
  page[crc_offset + 2] = static_cast<uint8_t>((crc >> 16) & 0xff);
  page[crc_offset + 3] = static_cast<uint8_t>((crc >> 24) & 0xff);
  return page;
}

std::vector<uint8_t> BuildSyntheticOggFlacStream(const uint8_t *payload,
                                                  size_t payload_len,
                                                  bool chained,
                                                  uint32_t serial1,
                                                  uint32_t serial2) {
  std::vector<uint8_t> out;

  const size_t link1_len = chained ? payload_len / 2 : payload_len;
  const size_t link2_len = chained ? payload_len - link1_len : 0;

  {
    std::vector<uint8_t> header_packet = BuildOggFlacHeaderPacket();
    std::vector<uint8_t> page0 = BuildOggPage(serial1, 0, 0x02  ,
                                               header_packet.data(),
                                               header_packet.size());
    out.insert(out.end(), page0.begin(), page0.end());

    std::vector<uint8_t> page1 =
        BuildOggPage(serial1, 1, 0x04  , payload, link1_len);
    out.insert(out.end(), page1.begin(), page1.end());
  }

  if (chained) {
    std::vector<uint8_t> header_packet2 = BuildOggFlacHeaderPacket();
    std::vector<uint8_t> page0 = BuildOggPage(serial2, 0, 0x02  ,
                                               header_packet2.data(),
                                               header_packet2.size());
    out.insert(out.end(), page0.begin(), page0.end());

    std::vector<uint8_t> page1 = BuildOggPage(
        serial2, 1, 0x04  , payload + link1_len, link2_len);
    out.insert(out.end(), page1.begin(), page1.end());
  }

  return out;
}

}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  if (size < 1) {
    return 0;
  }

  const uint8_t config = data[size - 1];
  const uint8_t *stream_data = data;
  const size_t stream_size = size - 1;

  unsigned char app_id[4] = {0, 0, 0, 0};
  if (stream_size >= 4) {
    std::memcpy(app_id, stream_data + stream_size - 4, 4);
  }

  FLAC__StreamDecoder *decoder = FLAC__stream_decoder_new();
  if (decoder == nullptr) {
    return 0;
  }

  if (config & 0x01) {
    FLAC__stream_decoder_set_metadata_respond_all(decoder);
  } else {
    FLAC__stream_decoder_set_metadata_ignore_all(decoder);
    FLAC__MetadataType type =
        static_cast<FLAC__MetadataType>((config >> 4) & 0x07);
    FLAC__stream_decoder_set_metadata_respond(decoder, type);

    FLAC__stream_decoder_set_metadata_respond(decoder,
                                               FLAC__METADATA_TYPE_STREAMINFO);
  }

  if (config & 0x08) {
    FLAC__stream_decoder_set_metadata_respond_application(decoder, app_id);
    FLAC__stream_decoder_set_metadata_ignore_application(decoder, app_id);
  }

  FLAC__stream_decoder_set_md5_checking(decoder, (config & 0x02) != 0);

  const bool use_ogg = (config & 0x04) != 0;
  const bool chained = use_ogg && ((config & 0x80) != 0);

  std::vector<uint8_t> synthetic_ogg;
  if (use_ogg) {
    synthetic_ogg = BuildSyntheticOggFlacStream(
        stream_data, stream_size, chained,  1,  2);
  }

  FuzzerClientData client_data;
  if (use_ogg) {
    client_data.data = synthetic_ogg.data();
    client_data.size = synthetic_ogg.size();
  } else {
    client_data.data = stream_data;
    client_data.size = stream_size;
  }
  client_data.pos = 0;

  if (chained) {

    FLAC__stream_decoder_set_decode_chained_stream(decoder, true);
  } else if (use_ogg) {

    FLAC__stream_decoder_set_ogg_serial_number(decoder, 1);
  }

  FLAC__StreamDecoderInitStatus init_status;

  if (use_ogg) {
    init_status = FLAC__stream_decoder_init_ogg_stream(
        decoder,
        read_callback_,
         nullptr,
         nullptr,
         nullptr,
         nullptr,
        write_callback_,
        metadata_callback_,
        error_callback_,
        &client_data);
  } else {
    init_status = FLAC__stream_decoder_init_stream(
        decoder,
        read_callback_,
         nullptr,
         nullptr,
         nullptr,
         nullptr,
        write_callback_,
        metadata_callback_,
        error_callback_,
        &client_data);
  }

  if (init_status == FLAC__STREAM_DECODER_INIT_STATUS_OK) {

    const int kMaxIterations = 100000;
    int iterations = 0;

    while (iterations < kMaxIterations) {
      ++iterations;

      FLAC__StreamDecoderState state = FLAC__stream_decoder_get_state(decoder);
      if (state == FLAC__STREAM_DECODER_END_OF_STREAM ||
          state == FLAC__STREAM_DECODER_ABORTED ||
          state == FLAC__STREAM_DECODER_END_OF_LINK) {

        if (state == FLAC__STREAM_DECODER_END_OF_LINK && chained) {
          FLAC__stream_decoder_finish_link(decoder);
          continue;
        }
        break;
      }

      FLAC__bool ok = FLAC__stream_decoder_process_single(decoder);
      if (!ok) {

        break;
      }
    }

    FLAC__stream_decoder_finish(decoder);
  }

  FLAC__stream_decoder_delete(decoder);

  return 0;
}