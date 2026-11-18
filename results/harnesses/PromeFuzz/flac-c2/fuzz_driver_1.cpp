#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <cstdint>
#include <cstddef>

#include "FLAC/stream_encoder.h"

extern "C" {
    unsigned int alloc_check_threshold = 0;
    unsigned int alloc_check_counter = 0;
    int alloc_check_keep_failing = 0;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size) {
    if (Size < 8) {
        return 0;
    }

    uint8_t channels_byte = Data[0];
    uint8_t bps_byte = Data[1];
    uint8_t sample_rate_byte = Data[2];
    uint8_t compression_byte = Data[3];

    unsigned channels = (channels_byte % 8) + 1;

    unsigned bps_options[] = {8, 16, 20, 24};
    unsigned bits_per_sample = bps_options[bps_byte % 4];

    unsigned sample_rate_options[] = {8000, 16000, 22050, 44100, 48000, 96000};
    unsigned sample_rate = sample_rate_options[sample_rate_byte % 6];

    unsigned compression_level = compression_byte % 9;

    const uint8_t *pcm_data = Data + 4;
    size_t pcm_data_size = Size - 4;

    if (pcm_data_size < sizeof(FLAC__int32)) {
        return 0;
    }

    FLAC__StreamEncoder *encoder = FLAC__stream_encoder_new();
    if (!encoder) {
        return 0;
    }

    FLAC__stream_encoder_set_channels(encoder, channels);
    FLAC__stream_encoder_set_bits_per_sample(encoder, bits_per_sample);
    FLAC__stream_encoder_set_sample_rate(encoder, sample_rate);
    FLAC__stream_encoder_set_compression_level(encoder, compression_level);

    FLAC__stream_encoder_set_blocksize(encoder, 0);

    const char *output_filename = "./dummy_file";

    FLAC__StreamEncoderInitStatus init_status =
        FLAC__stream_encoder_init_file(encoder, output_filename, nullptr, nullptr);

    if (init_status != FLAC__STREAM_ENCODER_INIT_STATUS_OK) {
        FLAC__stream_encoder_delete(encoder);
        return 0;
    }

    size_t num_int32_samples = pcm_data_size / sizeof(FLAC__int32);
    if (num_int32_samples == 0) {
        FLAC__stream_encoder_finish(encoder);
        FLAC__stream_encoder_delete(encoder);
        return 0;
    }

    std::vector<FLAC__int32> buffer(num_int32_samples);
    std::memcpy(buffer.data(), pcm_data, num_int32_samples * sizeof(FLAC__int32));

    int32_t max_val = (bits_per_sample >= 32) ? INT32_MAX : ((1 << (bits_per_sample - 1)) - 1);
    int32_t min_val = (bits_per_sample >= 32) ? INT32_MIN : -(1 << (bits_per_sample - 1));

    for (size_t i = 0; i < buffer.size(); ++i) {
        if (buffer[i] > max_val) buffer[i] = max_val;
        if (buffer[i] < min_val) buffer[i] = min_val;
    }

    uint32_t samples_per_channel = static_cast<uint32_t>(buffer.size() / channels);

    if (samples_per_channel > 0) {

        size_t total_interleaved_samples_used = samples_per_channel * channels;
        size_t offset = 0;

        uint32_t chunk_sizes[] = {samples_per_channel, samples_per_channel / 2, 1};

        for (uint32_t chunk : chunk_sizes) {
            if (chunk == 0) continue;
            size_t remaining = total_interleaved_samples_used - offset;
            uint32_t remaining_samples = static_cast<uint32_t>(remaining / channels);
            if (remaining_samples == 0) break;

            uint32_t to_process = (chunk < remaining_samples) ? chunk : remaining_samples;
            if (to_process == 0) break;

            FLAC__bool ok = FLAC__stream_encoder_process_interleaved(
                encoder,
                buffer.data() + offset,
                to_process
            );

            offset += static_cast<size_t>(to_process) * channels;

            if (!ok) {

                FLAC__StreamEncoderState state = FLAC__stream_encoder_get_state(encoder);
                if (state != FLAC__STREAM_ENCODER_OK) {
                    break;
                }
            }

            if (offset >= total_interleaved_samples_used) {
                break;
            }
        }
    }

    FLAC__stream_encoder_finish(encoder);
    FLAC__stream_encoder_delete(encoder);

    return 0;
}