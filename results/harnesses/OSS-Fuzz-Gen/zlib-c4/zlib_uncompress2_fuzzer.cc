#include <fuzzer/FuzzedDataProvider.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <utility>
#include <vector>

#include "zlib.h"

static uint8_t kDestBuffer[256 * 1024];

class BitWriter {
 public:
  void WriteBits(uint32_t value, int nbits) {
    for (int i = 0; i < nbits; ++i) {
      if (bit_pos_ == 0) bytes_.push_back(0);
      if (value & (1u << i)) bytes_.back() = static_cast<uint8_t>(
          bytes_.back() | (1u << bit_pos_));
      bit_pos_ = (bit_pos_ + 1) % 8;
    }
  }

  void PadToByte() {
    if (bit_pos_ != 0) {
      WriteBits(0, 8 - bit_pos_);
    }
  }

  std::vector<uint8_t> TakeBytes() { return std::move(bytes_); }

 private:
  std::vector<uint8_t> bytes_;
  int bit_pos_ = 0;
};

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  FuzzedDataProvider stream(data, size);

  uLongf dest_len = static_cast<uLongf>(
      stream.ConsumeIntegralInRange<uint32_t>(0, sizeof(kDestBuffer)));

  uint8_t mode = stream.ConsumeIntegralInRange<uint8_t>(0, 5);
  int level = static_cast<int>(stream.ConsumeIntegralInRange<uint8_t>(
      Z_NO_COMPRESSION, Z_BEST_COMPRESSION));

  std::vector<uint8_t> source_bytes;

  if (mode == 1) {
    size_t payload_size =
        stream.ConsumeIntegralInRange<size_t>(0, stream.remaining_bytes());
    std::vector<uint8_t> payload = stream.ConsumeBytes<uint8_t>(payload_size);

    if (!payload.empty() && stream.ConsumeBool()) {
      uint8_t divisor = stream.ConsumeIntegralInRange<uint8_t>(2, 8);
      uLongf shrunk = static_cast<uLongf>(payload.size() / divisor);
      if (shrunk < dest_len) dest_len = shrunk;
    }

    bool use_deflate2 = stream.ConsumeBool();
    bool compressed_ok = false;
    std::vector<uint8_t> comp_buf;
    uLong comp_len = 0;

    if (use_deflate2) {

      int strategy =
          stream.ConsumeIntegralInRange<int>(Z_FILTERED, Z_FIXED);
      int window_bits = stream.ConsumeIntegralInRange<int>(8, 15);
      int mem_level = stream.ConsumeIntegralInRange<int>(1, 9);

      z_stream zs;
      memset(&zs, 0, sizeof(zs));
      if (deflateInit2(&zs, level, Z_DEFLATED, window_bits, mem_level,
                        strategy) == Z_OK) {
        uLong bound = deflateBound(&zs, static_cast<uLong>(payload.size()));
        comp_buf.resize(bound > 0 ? bound : 1);
        zs.next_in = payload.empty()
                         ? nullptr
                         : const_cast<Bytef *>(payload.data());
        zs.avail_in = static_cast<uInt>(payload.size());
        zs.next_out = comp_buf.data();
        zs.avail_out = static_cast<uInt>(comp_buf.size());
        int dret = deflate(&zs, Z_FINISH);
        if (dret == Z_STREAM_END) {
          comp_len = comp_buf.size() - zs.avail_out;
          compressed_ok = true;
        }
        deflateEnd(&zs);
      }
    } else {
      uLongf comp_cap = compressBound(static_cast<uLong>(payload.size()));
      comp_buf.resize(comp_cap);
      uLongf cl = comp_cap;
      int cret = compress2(comp_buf.data(), &cl,
                            payload.empty() ? nullptr : payload.data(),
                            static_cast<uLong>(payload.size()), level);
      if (cret == Z_OK) {
        comp_len = cl;
        compressed_ok = true;
      }
    }

    if (compressed_ok) {
      comp_buf.resize(comp_len);
      source_bytes = std::move(comp_buf);

      uint8_t num_flips = stream.ConsumeIntegralInRange<uint8_t>(0, 8);
      for (uint8_t i = 0; i < num_flips && !source_bytes.empty(); ++i) {
        size_t idx = stream.ConsumeIntegralInRange<size_t>(
            0, source_bytes.size() - 1);
        uint8_t xor_byte = stream.ConsumeIntegral<uint8_t>();
        source_bytes[idx] ^= (xor_byte == 0 ? 0xFF : xor_byte);
      }

      if (!source_bytes.empty() &&
          stream.ConsumeIntegralInRange<uint8_t>(0, 3) == 0) {
        size_t new_size =
            stream.ConsumeIntegralInRange<size_t>(0, source_bytes.size());
        source_bytes.resize(new_size);
      }
    } else {

      source_bytes = std::move(payload);
    }
  } else if (mode == 2) {

    uint8_t cinfo = stream.ConsumeIntegralInRange<uint8_t>(0, 15);
    uint8_t flevel = stream.ConsumeIntegralInRange<uint8_t>(0, 3);
    uint8_t fdict = stream.ConsumeBool() ? 1 : 0;
    uint8_t cmf = static_cast<uint8_t>((cinfo << 4) | 0x08);

    uint8_t flg_hi =
        static_cast<uint8_t>((flevel << 6) | (fdict << 5));
    int base = ((static_cast<int>(cmf) << 8) | flg_hi) % 31;
    uint8_t fcheck = static_cast<uint8_t>((31 - base) % 31);
    uint8_t flg = static_cast<uint8_t>(flg_hi | fcheck);

    source_bytes.push_back(cmf);
    source_bytes.push_back(flg);

    if (fdict) {

      uint32_t dictid = stream.ConsumeIntegral<uint32_t>();
      for (int i = 3; i >= 0; --i)
        source_bytes.push_back(
            static_cast<uint8_t>((dictid >> (8 * i)) & 0xFF));
    }

    std::vector<uint8_t> body = stream.ConsumeRemainingBytes<uint8_t>();
    source_bytes.insert(source_bytes.end(), body.begin(), body.end());

    if (stream.ConsumeBool()) {
      uint32_t adler = stream.ConsumeIntegral<uint32_t>();
      for (int i = 3; i >= 0; --i)
        source_bytes.push_back(
            static_cast<uint8_t>((adler >> (8 * i)) & 0xFF));
    }
  } else if (mode == 3) {

    uint8_t cinfo = stream.ConsumeIntegralInRange<uint8_t>(0, 7);
    uint8_t cmf = static_cast<uint8_t>((cinfo << 4) | 0x08);
    uint16_t head16 = static_cast<uint16_t>(static_cast<uint16_t>(cmf) << 8);
    head16 = static_cast<uint16_t>(head16 + ((31 - (head16 % 31)) % 31));
    uint8_t flg = static_cast<uint8_t>(head16 & 0xFF);

    source_bytes.push_back(cmf);
    source_bytes.push_back(flg);

    BitWriter bw;
    uint8_t bfinal = stream.ConsumeIntegralInRange<uint8_t>(0, 1);
    bw.WriteBits(bfinal, 1);
    bw.WriteBits(2, 2);

    uint8_t hlit = stream.ConsumeIntegralInRange<uint8_t>(0, 31);
    uint8_t hdist = stream.ConsumeIntegralInRange<uint8_t>(0, 31);
    uint8_t hclen = stream.ConsumeIntegralInRange<uint8_t>(0, 15);
    bw.WriteBits(hlit, 5);
    bw.WriteBits(hdist, 5);
    bw.WriteBits(hclen, 4);

    int num_cl_codes = static_cast<int>(hclen) + 4;
    for (int i = 0; i < num_cl_codes; ++i) {
      uint8_t cl_len = stream.ConsumeIntegralInRange<uint8_t>(0, 7);
      bw.WriteBits(cl_len, 3);
    }

    std::vector<uint8_t> rest = stream.ConsumeRemainingBytes<uint8_t>();
    for (uint8_t b : rest) bw.WriteBits(b, 8);

    std::vector<uint8_t> body = bw.TakeBytes();
    source_bytes.insert(source_bytes.end(), body.begin(), body.end());
  } else if (mode == 4) {

    uint8_t cinfo = stream.ConsumeIntegralInRange<uint8_t>(0, 7);
    uint8_t cmf = static_cast<uint8_t>((cinfo << 4) | 0x08);
    uint16_t head16 = static_cast<uint16_t>(static_cast<uint16_t>(cmf) << 8);
    head16 = static_cast<uint16_t>(head16 + ((31 - (head16 % 31)) % 31));
    uint8_t flg = static_cast<uint8_t>(head16 & 0xFF);

    source_bytes.push_back(cmf);
    source_bytes.push_back(flg);

    BitWriter bw;
    uint8_t bfinal = stream.ConsumeIntegralInRange<uint8_t>(0, 1);
    bw.WriteBits(bfinal, 1);
    bw.WriteBits(0, 2);
    bw.PadToByte();

    uint16_t len = stream.ConsumeIntegral<uint16_t>();
    bool correct_nlen = stream.ConsumeBool();
    uint16_t nlen = correct_nlen ? static_cast<uint16_t>(~len)
                                  : stream.ConsumeIntegral<uint16_t>();

    std::vector<uint8_t> header_body = bw.TakeBytes();
    source_bytes.insert(source_bytes.end(), header_body.begin(),
                         header_body.end());
    source_bytes.push_back(static_cast<uint8_t>(len & 0xFF));
    source_bytes.push_back(static_cast<uint8_t>((len >> 8) & 0xFF));
    source_bytes.push_back(static_cast<uint8_t>(nlen & 0xFF));
    source_bytes.push_back(static_cast<uint8_t>((nlen >> 8) & 0xFF));

    std::vector<uint8_t> rest = stream.ConsumeRemainingBytes<uint8_t>();
    source_bytes.insert(source_bytes.end(), rest.begin(), rest.end());
  } else if (mode == 5) {

    dest_len = static_cast<uLongf>(stream.ConsumeIntegralInRange<uint32_t>(
        40000, static_cast<uint32_t>(sizeof(kDestBuffer))));

    uint8_t pattern_len = stream.ConsumeIntegralInRange<uint8_t>(1, 32);
    std::vector<uint8_t> pattern = stream.ConsumeBytes<uint8_t>(pattern_len);
    if (pattern.empty()) pattern.push_back(0x41);

    uint32_t payload_size =
        stream.ConsumeIntegralInRange<uint32_t>(40000, 220000);
    std::vector<uint8_t> payload(payload_size);
    for (uint32_t i = 0; i < payload_size; ++i) {
      payload[i] = pattern[i % pattern.size()];
    }

    int strategy = stream.ConsumeIntegralInRange<int>(Z_FILTERED, Z_FIXED);
    int window_bits = stream.ConsumeIntegralInRange<int>(8, 15);
    int mem_level = stream.ConsumeIntegralInRange<int>(1, 9);

    bool compressed_ok = false;
    std::vector<uint8_t> comp_buf;
    uLong comp_len = 0;

    z_stream zs;
    memset(&zs, 0, sizeof(zs));
    if (deflateInit2(&zs, level, Z_DEFLATED, window_bits, mem_level,
                      strategy) == Z_OK) {
      uLong bound = deflateBound(&zs, static_cast<uLong>(payload.size()));
      comp_buf.resize(bound > 0 ? bound : 1);
      zs.next_in = const_cast<Bytef *>(payload.data());
      zs.avail_in = static_cast<uInt>(payload.size());
      zs.next_out = comp_buf.data();
      zs.avail_out = static_cast<uInt>(comp_buf.size());
      int dret = deflate(&zs, Z_FINISH);
      if (dret == Z_STREAM_END) {
        comp_len = comp_buf.size() - zs.avail_out;
        compressed_ok = true;
      }
      deflateEnd(&zs);
    }

    if (compressed_ok) {
      comp_buf.resize(comp_len);
      source_bytes = std::move(comp_buf);

      if (stream.ConsumeBool() && !source_bytes.empty()) {
        uint8_t num_flips = stream.ConsumeIntegralInRange<uint8_t>(0, 2);
        for (uint8_t i = 0; i < num_flips && !source_bytes.empty(); ++i) {
          size_t idx = stream.ConsumeIntegralInRange<size_t>(
              0, source_bytes.size() - 1);
          uint8_t xor_byte = stream.ConsumeIntegral<uint8_t>();
          source_bytes[idx] ^= (xor_byte == 0 ? 0xFF : xor_byte);
        }
      }
    } else {

      source_bytes.clear();
    }
  } else {
    source_bytes = stream.ConsumeRemainingBytes<uint8_t>();
  }

  bool force_null_source_mismatch = stream.ConsumeBool();
  bool force_null_dest_mismatch = stream.ConsumeBool();

  uLong source_len = static_cast<uLong>(source_bytes.size());

  const Bytef *source_ptr =
      (source_len > 0 && !force_null_source_mismatch)
          ? reinterpret_cast<const Bytef *>(source_bytes.data())
          : nullptr;
  Bytef *dest_ptr =
      (dest_len > 0 && !force_null_dest_mismatch) ? kDestBuffer : nullptr;

  uncompress2(dest_ptr, &dest_len, source_ptr, &source_len);

  return 0;
}