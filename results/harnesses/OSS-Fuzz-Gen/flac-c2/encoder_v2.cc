#include <fuzzer/FuzzedDataProvider.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "FLAC/stream_encoder.h"
#include "FLAC/format.h"
#include "FLAC/metadata.h"
#include "common.h"

extern "C" {
#include "share/private.h"
}

extern "C" {

struct SeekableSink {
  std::vector<uint8_t> data;
  size_t pos = 0;
  bool fail_write = false;
  bool fail_seek = false;
  bool fail_tell = false;

  size_t fail_after_bytes = 0;
};

static FLAC__StreamEncoderWriteStatus write_callback(
    const FLAC__StreamEncoder *encoder,
    const FLAC__byte buffer[],
    size_t bytes,
    uint32_t samples,
    uint32_t current_frame,
    void *client_data) {
  (void)encoder;
  (void)samples;
  (void)current_frame;
  SeekableSink *sink = static_cast<SeekableSink *>(client_data);
  if (sink->fail_write && sink->pos >= sink->fail_after_bytes) {
    return FLAC__STREAM_ENCODER_WRITE_STATUS_FATAL_ERROR;
  }
  if (sink->pos + bytes > sink->data.size()) {
    sink->data.resize(sink->pos + bytes);
  }
  if (bytes > 0) {
    std::memcpy(sink->data.data() + sink->pos, buffer, bytes);
  }
  sink->pos += bytes;
  return FLAC__STREAM_ENCODER_WRITE_STATUS_OK;
}

static FLAC__StreamEncoderSeekStatus seek_callback(
    const FLAC__StreamEncoder *encoder,
    FLAC__uint64 absolute_byte_offset,
    void *client_data) {
  (void)encoder;
  SeekableSink *sink = static_cast<SeekableSink *>(client_data);
  if (sink->fail_seek) {
    return FLAC__STREAM_ENCODER_SEEK_STATUS_ERROR;
  }
  sink->pos = static_cast<size_t>(absolute_byte_offset);
  return FLAC__STREAM_ENCODER_SEEK_STATUS_OK;
}

static FLAC__StreamEncoderTellStatus tell_callback(
    const FLAC__StreamEncoder *encoder,
    FLAC__uint64 *absolute_byte_offset,
    void *client_data) {
  (void)encoder;
  SeekableSink *sink = static_cast<SeekableSink *>(client_data);
  if (sink->fail_tell) {
    return FLAC__STREAM_ENCODER_TELL_STATUS_ERROR;
  }
  *absolute_byte_offset = static_cast<FLAC__uint64>(sink->pos);
  return FLAC__STREAM_ENCODER_TELL_STATUS_OK;
}

static const char *kApodizations[] = {
  "bartlett", "bartlett_hann", "blackman",
  "blackman_harris_4term_92db", "connes", "flattop",
  "hamming", "hann", "kaiser_bessel", "nuttall",
  "rectangle", "triangle", "welch"
};

enum class PatternMode : uint8_t {
  kRaw = 0,
  kConstantPerBlock = 1,
  kRamp = 2,
  kSilence = 3,
  kCorrelatedChannels = 4
};

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  FuzzedDataProvider stream(data, size);

  const bool force_stereo = stream.ConsumeBool();
  const uint32_t channels = force_stereo
      ? 2
      : stream.ConsumeIntegralInRange<uint32_t>(1, 8);

  static const uint32_t kSubsetBps[] = {8, 12, 16, 20, 24, 32};
  const bool force_subset_bps = stream.ConsumeBool();
  const uint32_t bits_per_sample = force_subset_bps
      ? kSubsetBps[stream.ConsumeIntegralInRange<uint8_t>(0, 5)]
      : stream.ConsumeIntegralInRange<uint32_t>(4, 32);

  static const uint32_t kSubsetRates[] = {
      8000, 16000, 22050, 24000, 32000, 44100, 48000, 96000, 192000};
  const bool force_subset_rate = stream.ConsumeBool();
  const uint32_t sample_rate = force_subset_rate
      ? kSubsetRates[stream.ConsumeIntegralInRange<uint8_t>(0, 8)]
      : stream.ConsumeIntegralInRange<uint32_t>(1, 655350);

  const uint32_t blocksize = stream.ConsumeIntegralInRange<uint32_t>(16, 256);
  const uint32_t compression_level =
      stream.ConsumeIntegralInRange<uint32_t>(0, 8);
  const bool verify = stream.ConsumeBool();

  const bool do_md5 = stream.ConsumeBool();
  const bool streamable_subset = stream.ConsumeBool();
  const bool do_escape_coding = stream.ConsumeBool();
  const bool do_qlp_prec_search = stream.ConsumeBool();

  const bool allow_expensive_lpc_search =
      stream.ConsumeIntegralInRange<uint8_t>(0, 7) == 0;
  const uint32_t max_lpc_order = allow_expensive_lpc_search
      ? stream.ConsumeIntegralInRange<uint32_t>(0, 32)
      : stream.ConsumeIntegralInRange<uint32_t>(0, 8);
  const bool do_exhaustive_search =
      allow_expensive_lpc_search && stream.ConsumeBool();

  const bool disable_constant = stream.ConsumeBool();
  const bool disable_fixed = stream.ConsumeBool();
  const bool disable_verbatim = stream.ConsumeBool();

  const uint8_t instruction_set_disable_mask =
      stream.ConsumeIntegralInRange<uint8_t>(0, 127);

  const bool force_multithread = stream.ConsumeBool();
  const uint32_t num_threads = force_multithread
      ? stream.ConsumeIntegralInRange<uint32_t>(2, 3)
      : stream.ConsumeIntegralInRange<uint32_t>(1, 3);

  const uint32_t chunk_frames = stream.ConsumeIntegralInRange<uint32_t>(1, 128);

  const bool do_mid_side_stereo = stream.ConsumeBool();
  const bool loose_mid_side_stereo = stream.ConsumeBool();
  const uint32_t max_residual_partition_order =
      stream.ConsumeIntegralInRange<uint32_t>(0, 6);
  const uint32_t min_residual_partition_order =
      stream.ConsumeIntegralInRange<uint32_t>(0, max_residual_partition_order);
  const uint32_t qlp_coeff_precision =
      stream.ConsumeIntegralInRange<uint32_t>(0, 15);
  const bool limit_min_bitrate = stream.ConsumeBool();
  const uint64_t total_samples_estimate =
      stream.ConsumeIntegralInRange<uint64_t>(0, 100000);

  const bool use_seektable = stream.ConsumeBool();
  const uint32_t num_seekpoints = stream.ConsumeIntegralInRange<uint32_t>(0, 32);

  const bool use_seek_tell = stream.ConsumeBool();

  const bool inject_fail_write = stream.ConsumeBool();
  const bool inject_fail_seek = stream.ConsumeBool();
  const bool inject_fail_tell = stream.ConsumeBool();
  const uint32_t fail_after_bytes =
      stream.ConsumeIntegralInRange<uint32_t>(0, 4096);

  const PatternMode mode =
      static_cast<PatternMode>(stream.ConsumeIntegralInRange<uint8_t>(0, 4));
  uint32_t synth_frames = stream.ConsumeIntegralInRange<uint32_t>(0, 2000);

  if (synth_frames == 0) {
    synth_frames = 1;
  }
  if (num_threads > 1) {

    const uint32_t min_frames_for_threads = std::min<uint32_t>(
        2000, (num_threads + 2) * std::max<uint32_t>(blocksize, 1) * 2);
    if (synth_frames < min_frames_for_threads) {
      synth_frames = min_frames_for_threads;
    }
  }

  std::string apod_spec;
  {
    const bool allow_multi_window =
        stream.ConsumeIntegralInRange<uint8_t>(0, 9) >= 8;
    const uint32_t num_windows = allow_multi_window
        ? stream.ConsumeIntegralInRange<uint32_t>(2, 3)
        : 1;
    for (uint32_t i = 0; i < num_windows; ++i) {
      if (i) apod_spec += ';';
      const uint8_t kind = stream.ConsumeIntegralInRange<uint8_t>(0, 6);
      switch (kind) {
        case 0:
          apod_spec += kApodizations[stream.ConsumeIntegralInRange<uint8_t>(
              0, sizeof(kApodizations) / sizeof(kApodizations[0]) - 1)];
          break;
        case 1:
          apod_spec += "gauss(" +
              std::to_string(stream.ConsumeFloatingPointInRange<double>(
                  -1.0, 1.0)) +
              ")";
          break;
        case 2:
          apod_spec += "tukey(" +
              std::to_string(stream.ConsumeFloatingPointInRange<double>(
                  -1.0, 2.0)) +
              ")";
          break;
        case 3:
          apod_spec += "partial_tukey(" +
              std::to_string(stream.ConsumeIntegralInRange<int>(0, 8)) +
              ")";
          break;
        case 4:
          apod_spec += "punchout_tukey(" +
              std::to_string(stream.ConsumeIntegralInRange<int>(0, 8)) +
              ")";
          break;
        case 5:
          apod_spec += "subdivide_tukey(" +
              std::to_string(stream.ConsumeIntegralInRange<int>(0, 8)) +
              ")";
          break;
        default:

          apod_spec += "not_a_real_window";
          break;
      }
    }
    if (apod_spec.empty()) {
      apod_spec = "tukey(0.5)";
    }
  }

  FLAC__StreamEncoder *encoder = FLAC__stream_encoder_new();
  if (encoder == nullptr) {
    return 0;
  }

  FLAC__stream_encoder_set_channels(encoder, channels);
  FLAC__stream_encoder_set_bits_per_sample(encoder, bits_per_sample);
  FLAC__stream_encoder_set_sample_rate(encoder, sample_rate);
  FLAC__stream_encoder_set_blocksize(encoder, blocksize);
  FLAC__stream_encoder_set_compression_level(encoder, compression_level);

  FLAC__stream_encoder_set_do_escape_coding(encoder, do_escape_coding);
  FLAC__stream_encoder_set_do_qlp_coeff_prec_search(encoder, do_qlp_prec_search);
  FLAC__stream_encoder_set_do_exhaustive_model_search(encoder, do_exhaustive_search);
  FLAC__stream_encoder_set_apodization(encoder, apod_spec.c_str());
  FLAC__stream_encoder_set_streamable_subset(encoder, streamable_subset);
  FLAC__stream_encoder_set_num_threads(encoder, num_threads);

  FLAC__stream_encoder_set_do_mid_side_stereo(encoder, do_mid_side_stereo);
  FLAC__stream_encoder_set_loose_mid_side_stereo(encoder, loose_mid_side_stereo);
  FLAC__stream_encoder_set_max_residual_partition_order(
      encoder, max_residual_partition_order);
  FLAC__stream_encoder_set_min_residual_partition_order(
      encoder, min_residual_partition_order);
  FLAC__stream_encoder_set_max_lpc_order(encoder, max_lpc_order);
  FLAC__stream_encoder_set_qlp_coeff_precision(encoder, qlp_coeff_precision);
  FLAC__stream_encoder_set_limit_min_bitrate(encoder, limit_min_bitrate);
  FLAC__stream_encoder_set_total_samples_estimate(encoder, total_samples_estimate);

  FLAC__stream_encoder_set_do_md5(encoder, do_md5);
  FLAC__stream_encoder_disable_constant_subframes(encoder, disable_constant);
  FLAC__stream_encoder_disable_fixed_subframes(encoder, disable_fixed);
  FLAC__stream_encoder_disable_verbatim_subframes(encoder, disable_verbatim);
  FLAC__stream_encoder_disable_instruction_set(encoder, instruction_set_disable_mask);

  FLAC__stream_encoder_set_verify(encoder, verify);

  FLAC__StreamMetadata *seektable = nullptr;
  FLAC__StreamMetadata *metadata_blocks[1];
  if (use_seektable) {
    seektable = FLAC__metadata_object_new(FLAC__METADATA_TYPE_SEEKTABLE);
    if (seektable != nullptr) {
      FLAC__uint64 total_samples_hint =
          static_cast<FLAC__uint64>(synth_frames);
      FLAC__metadata_object_seektable_template_append_spaced_points_by_samples(
          seektable,
          blocksize > 0 ? blocksize : 1,
          total_samples_hint);

      if (num_seekpoints > 0) {
        FLAC__metadata_object_seektable_template_append_spaced_points_by_samples(
            seektable,
            std::max<uint32_t>(1, (blocksize * 2) / (num_seekpoints + 1)),
            total_samples_hint);
      }
      FLAC__metadata_object_seektable_template_sort(seektable,  true);
      metadata_blocks[0] = seektable;
      FLAC__stream_encoder_set_metadata(encoder, metadata_blocks, 1);
    }
  }

  SeekableSink sink;
  sink.fail_write = inject_fail_write;
  sink.fail_seek = inject_fail_seek;
  sink.fail_tell = inject_fail_tell;
  sink.fail_after_bytes = fail_after_bytes;

  FLAC__StreamEncoderInitStatus init_status = FLAC__stream_encoder_init_stream(
      encoder,
      write_callback,
      use_seek_tell ? seek_callback : nullptr,
      use_seek_tell ? tell_callback : nullptr,
       nullptr,
      &sink);

  if (init_status != FLAC__STREAM_ENCODER_INIT_STATUS_OK) {
    FLAC__stream_encoder_delete(encoder);
    if (seektable != nullptr) {
      FLAC__metadata_object_delete(seektable);
    }
    return 0;
  }

  const int32_t sample_min =
      (bits_per_sample >= 32) ? INT32_MIN
                               : (INT32_MIN >> (32 - bits_per_sample));
  const int32_t sample_max =
      (bits_per_sample >= 32) ? INT32_MAX
                               : (INT32_MAX >> (32 - bits_per_sample));

  auto clamp_sample = [&](int64_t v) -> FLAC__int32 {
    if (v < sample_min) v = sample_min;
    if (v > sample_max) v = sample_max;
    return static_cast<FLAC__int32>(v);
  };

  std::vector<uint8_t> raw_bytes = stream.ConsumeRemainingBytes<uint8_t>();
  size_t byte_idx = 0;
  auto next_raw_int32 = [&]() -> FLAC__int32 {
    FLAC__int32 v = 0;
    if (!raw_bytes.empty()) {
      for (size_t b = 0; b < sizeof(FLAC__int32); ++b) {
        v |= static_cast<FLAC__int32>(raw_bytes[byte_idx % raw_bytes.size()])
             << (8 * b);
        ++byte_idx;
      }
    }
    return v;
  };

  if (synth_frames > 0 && channels > 0) {
    std::vector<FLAC__int32> buffer(
        static_cast<size_t>(synth_frames) * channels);
    std::vector<FLAC__int32> per_channel_const(channels, 0);
    FLAC__int32 correlated_base = 0;
    const uint32_t effective_blocksize = blocksize > 0 ? blocksize : 1;

    for (uint32_t f = 0; f < synth_frames; ++f) {
      if (mode == PatternMode::kConstantPerBlock &&
          (f % effective_blocksize) == 0) {
        for (uint32_t c = 0; c < channels; ++c) {
          per_channel_const[c] = clamp_sample(next_raw_int32());
        }
      }
      if (mode == PatternMode::kCorrelatedChannels) {
        correlated_base = clamp_sample(
            static_cast<int64_t>(f % 2000) - 1000);
      }
      for (uint32_t c = 0; c < channels; ++c) {
        FLAC__int32 value = 0;
        switch (mode) {
          case PatternMode::kRaw:

            value = next_raw_int32();
            break;
          case PatternMode::kConstantPerBlock:
            value = per_channel_const[c];
            break;
          case PatternMode::kRamp:
            value = clamp_sample(static_cast<int64_t>(f) +
                                  static_cast<int64_t>(c));
            break;
          case PatternMode::kSilence:
            value = 0;
            break;
          case PatternMode::kCorrelatedChannels:
            if (c == 0) {
              value = correlated_base;
            } else {
              int32_t delta =
                  static_cast<int32_t>((next_raw_int32() % 5) - 2);
              value = clamp_sample(static_cast<int64_t>(correlated_base) +
                                    delta);
            }
            break;
        }
        buffer[static_cast<size_t>(f) * channels + c] = value;
      }
    }

    uint32_t fed = 0;
    while (fed < synth_frames) {
      uint32_t n = std::min(chunk_frames, synth_frames - fed);
      if (n == 0) {
        break;
      }
      if (!FLAC__stream_encoder_process_interleaved(
              encoder, buffer.data() + static_cast<size_t>(fed) * channels,
              n)) {
        break;
      }
      fed += n;
    }
  }

  FLAC__stream_encoder_finish(encoder);
  FLAC__stream_encoder_delete(encoder);
  if (seektable != nullptr) {
    FLAC__metadata_object_delete(seektable);
  }

  return 0;
}

}  