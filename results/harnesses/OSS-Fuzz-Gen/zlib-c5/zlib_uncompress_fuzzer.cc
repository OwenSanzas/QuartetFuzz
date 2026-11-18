#include <fuzzer/FuzzedDataProvider.h>

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

#include "zlib.h"

struct BitWriter {
  std::vector<uint8_t> buf;
  uint32_t acc = 0;
  int nbits = 0;
  void put(uint32_t val, int bits) {
    uint32_t mask = (bits >= 32) ? 0xFFFFFFFFu : ((1u << bits) - 1u);
    acc |= (val & mask) << nbits;
    nbits += bits;
    while (nbits >= 8) {
      buf.push_back(static_cast<uint8_t>(acc & 0xFF));
      acc >>= 8;
      nbits -= 8;
    }
  }
  void flush() {
    if (nbits > 0) {
      buf.push_back(static_cast<uint8_t>(acc & 0xFF));
      acc = 0;
      nbits = 0;
    }
  }
};

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  if (size < 10) {
    return 0;
  }

  FuzzedDataProvider stream(data, size);

  uint8_t mode = stream.ConsumeIntegral<uint8_t>();
  int level = stream.ConsumeIntegralInRange<int>(0, 9);
  bool use_patterned_plaintext = stream.ConsumeBool();
  size_t pattern_len = stream.ConsumeIntegralInRange<size_t>(1, 32);
  size_t repeat = stream.ConsumeIntegralInRange<size_t>(1, 200);
  bool force_fdict = stream.ConsumeBool();
  uint8_t null_mode = stream.ConsumeIntegral<uint8_t>() % 8;
  uint8_t size_choice = stream.ConsumeIntegral<uint8_t>() % 4;
  uint32_t dest_len_seed = stream.ConsumeIntegral<uint32_t>();
  uint8_t num_corruptions = stream.ConsumeIntegral<uint8_t>() % 8;

  uint8_t deterministic_mode = stream.ConsumeIntegral<uint8_t>() % 9;
  std::vector<std::pair<uint8_t, uint8_t>> corruptions;
  for (int i = 0; i < num_corruptions; ++i) {
    uint8_t idx = stream.ConsumeIntegral<uint8_t>();
    uint8_t xorv = stream.ConsumeIntegral<uint8_t>();
    corruptions.push_back({idx, xorv});
  }

  if (deterministic_mode == 6 || deterministic_mode == 7 ||
      deterministic_mode == 8) {
    size_choice = 2;
  }

  std::vector<uint8_t> source_buffer;

  if (deterministic_mode == 5) {

    source_buffer = {
        0x78, 0x01,
        0x01,
        0x05, 0x00,
        0x00, 0x00,

        'h', 'e', 'l', 'l', 'o'
    };
  } else if (deterministic_mode == 7) {

    source_buffer = {0x78, 0x01};
    BitWriter block;
    block.put(1, 1);
    block.put(2, 2);
    block.put(31, 5);
    block.put(0, 5);
    block.put(0, 4);
    block.flush();

    block.buf.push_back(0x00);
    block.buf.push_back(0x00);
    source_buffer.insert(source_buffer.end(), block.buf.begin(),
                          block.buf.end());
  } else if (deterministic_mode == 8) {

    source_buffer = {0x78, 0x01};
    BitWriter block;
    block.put(1, 1);
    block.put(2, 2);
    block.put(0, 5);
    block.put(0, 5);
    block.put(15, 4);
    for (int i = 0; i < 19; ++i) {
      block.put(1, 3);
    }
    block.flush();
    block.buf.push_back(0x00);
    block.buf.push_back(0x00);
    source_buffer.insert(source_buffer.end(), block.buf.begin(),
                          block.buf.end());
  } else if (mode % 3 == 0) {

    source_buffer = stream.ConsumeRemainingBytes<uint8_t>();
  } else {

    std::vector<uint8_t> plain;
    if (use_patterned_plaintext) {

      std::vector<uint8_t> pattern =
          stream.ConsumeBytes<uint8_t>(pattern_len);
      if (pattern.empty()) {
        pattern.push_back(0);
      }
      for (size_t i = 0; i < repeat; ++i) {
        plain.insert(plain.end(), pattern.begin(), pattern.end());
      }

      std::vector<uint8_t> tail = stream.ConsumeRemainingBytes<uint8_t>();
      plain.insert(plain.end(), tail.begin(), tail.end());
    } else {
      plain = stream.ConsumeRemainingBytes<uint8_t>();
    }

    if (plain.empty()) {
      plain.push_back(0);
    }

    uLongf bound = compressBound(static_cast<uLong>(plain.size()));
    std::vector<uint8_t> compressed(bound);
    uLongf compressed_len = bound;
    int cret = compress2(compressed.data(), &compressed_len, plain.data(),
                          static_cast<uLong>(plain.size()), level);
    if (cret != Z_OK) {
      return 0;
    }
    compressed.resize(compressed_len);
    source_buffer = std::move(compressed);

    if (mode % 3 == 2) {

      if (source_buffer.size() >= 2 && force_fdict) {
        source_buffer[1] |= 0x20;
      } else {

        for (auto &c : corruptions) {
          if (source_buffer.empty()) break;
          source_buffer[c.first % source_buffer.size()] ^= c.second;
        }
      }
    }

    switch (deterministic_mode) {
      case 0:

        if (source_buffer.size() >= 4) {
          source_buffer[source_buffer.size() - 1] ^= 0xFF;
        }
        break;
      case 1:

        if (!source_buffer.empty()) {
          source_buffer[0] = (source_buffer[0] & 0x0F) | 0xF0;
        }
        break;
      case 2:

        if (source_buffer.size() >= 2) {
          source_buffer[1] = (source_buffer[1] & 0xE0) ^ 0x07;
        }
        break;
      case 3:

        if (source_buffer.size() > 2) {
          source_buffer[2] ^= 0xFF;
        }
        break;
      case 6: {

        if (source_buffer.size() > 6) {
          size_t max_cut = source_buffer.size() - 6;
          size_t cut = 1 + (dest_len_seed % max_cut);
          source_buffer.resize(source_buffer.size() - cut);
        }
        break;
      }
      case 4:
      default:

        break;
    }
  }

  if (source_buffer.empty()) {
    return 0;
  }

  uLong source_len = static_cast<uLong>(source_buffer.size());

  if (null_mode == 0) {

    uLongf dest_len_nz = static_cast<uLongf>(source_len) * 4 + 1024;
    int ret = uncompress(nullptr, &dest_len_nz, source_buffer.data(),
                          source_len);
    (void)ret;
    return 0;
  } else if (null_mode == 1) {

    uLongf dest_len_nz = static_cast<uLongf>(source_len) * 4 + 1024;
    std::vector<uint8_t> dest_scratch(dest_len_nz);
    int ret = uncompress(dest_scratch.data(), &dest_len_nz, nullptr,
                          source_len);
    (void)ret;
    return 0;
  } else if (null_mode == 2) {

    uLongf zero_len = 0;
    int ret =
        uncompress(nullptr, &zero_len, source_buffer.data(), source_len);
    (void)ret;
    return 0;
  }

  uLongf dest_len;
  switch (size_choice) {
    case 0:
      dest_len = 0;
      break;
    case 1:
      dest_len = source_len / 4;
      break;
    case 2:
      dest_len = static_cast<uLongf>(source_len) * 4 + 1024;
      break;
    case 3:
    default: {
      uLongf max_len = static_cast<uLongf>(source_len) * 4 + 1024;
      dest_len = static_cast<uLongf>(dest_len_seed % (max_len + 1));
      break;
    }
  }

  const uLongf kMaxDestLen = 64 * 1024 * 1024;
  if (dest_len > kMaxDestLen) {
    dest_len = kMaxDestLen;
  }

  std::vector<uint8_t> dest_buffer(dest_len > 0 ? dest_len : 1);

  uLongf actual_dest_len = dest_len;
  Bytef *dest_ptr = dest_len > 0 ? dest_buffer.data() : nullptr;
  int ret = uncompress(dest_ptr, &actual_dest_len, source_buffer.data(),
                        source_len);
  (void)ret;

  return 0;
}