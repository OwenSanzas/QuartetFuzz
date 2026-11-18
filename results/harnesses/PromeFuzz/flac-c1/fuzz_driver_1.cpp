#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <cstdint>
#include <cstddef>
#include "FLAC++/encoder.h"

extern "C" {
    unsigned long alloc_check_counter = 0;
    unsigned long alloc_check_threshold = static_cast<unsigned long>(-1);
    int alloc_check_keep_failing = 0;
}

class FuzzStreamEncoder : public FLAC::Encoder::Stream {
public:
    FuzzStreamEncoder() : FLAC::Encoder::Stream() {}

protected:
    ::FLAC__StreamEncoderWriteStatus write_callback(const FLAC__byte buffer[],
                                                      size_t bytes,
                                                      uint32_t samples,
                                                      uint32_t current_frame) override {
        (void)buffer;
        (void)bytes;
        (void)samples;
        (void)current_frame;

        return FLAC__STREAM_ENCODER_WRITE_STATUS_OK;
    }

    void metadata_callback(const ::FLAC__StreamMetadata *metadata) override {
        (void)metadata;
    }
};

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size) {
    if (Size < 8) {
        return 0;
    }

    unsigned channels = 1 + (Data[0] % 2);
    unsigned bits_per_sample = (Data[1] % 2 == 0) ? 16 : 24;
    unsigned sample_rate = 8000 + (Data[2] % 4) * 11025;
    unsigned compression_level = Data[3] % 9;

    FuzzStreamEncoder encoder;

    if (!encoder.is_valid()) {
        return 0;
    }

    encoder.set_channels(channels);
    encoder.set_bits_per_sample(bits_per_sample);
    encoder.set_sample_rate(sample_rate);
    encoder.set_compression_level(compression_level);

    FLAC__StreamEncoderInitStatus init_status = encoder.init();
    if (init_status != FLAC__STREAM_ENCODER_INIT_STATUS_OK) {

        return 0;
    }

    size_t header_bytes = 4;
    size_t remaining = (Size > header_bytes) ? (Size - header_bytes) : 0;
    size_t num_samples = remaining / sizeof(FLAC__int32);

    if (num_samples == 0) {
        encoder.finish();
        return 0;
    }

    std::vector<FLAC__int32> buffer(num_samples);
    memcpy(buffer.data(), Data + header_bytes, num_samples * sizeof(FLAC__int32));

    uint32_t frames = static_cast<uint32_t>(num_samples / channels);

    if (frames > 0) {

        size_t offset = 0;
        size_t frames_per_call = frames;

        if (frames > 4) {
            frames_per_call = frames / 2;
        }

        while (offset < frames) {
            uint32_t chunk = static_cast<uint32_t>(
                (frames - offset) < frames_per_call ? (frames - offset) : frames_per_call);
            if (chunk == 0) break;

            const FLAC__int32 *chunk_ptr = buffer.data() + (offset * channels);
            bool ok = encoder.process_interleaved(chunk_ptr, chunk);
            if (!ok) {
                break;
            }
            offset += chunk;
        }
    }

    encoder.finish();

    return 0;
}