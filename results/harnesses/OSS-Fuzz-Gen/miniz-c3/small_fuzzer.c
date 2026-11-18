#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "miniz.h"

#define MAX_SHAPED_SIZE (48 * 1024)

static int map_flush(uint8_t b) {
    switch (b % 5) {
        case 0: return MZ_NO_FLUSH;
        case 1: return MZ_PARTIAL_FLUSH;
        case 2: return MZ_SYNC_FLUSH;
        case 3: return MZ_FULL_FLUSH;
        default: return MZ_FINISH;
    }
}

static void *custom_alloc_func(void *opaque, size_t items, size_t size) {
    (void)opaque;
    return calloc(items, size);
}

static void custom_free_func(void *opaque, void *address) {
    (void)opaque;
    free(address);
}

static unsigned char *build_payload(const uint8_t *payload, size_t payload_size,
                                     uint8_t mode, uint8_t size_sel, uint8_t seed_sel,
                                     uint8_t run_sel, size_t *out_len) {
    size_t shaped_size;
    unsigned char *buf;
    size_t seed_len;
    size_t cap;
    size_t i;
    size_t run_len;
    uint8_t val;
    size_t run_idx;
    size_t chunk;
    size_t half;

    switch (mode % 4) {
        case 0: {
            if (payload_size == 0) { *out_len = 0; return NULL; }
            buf = (unsigned char *)malloc(payload_size);
            if (!buf) { *out_len = 0; return NULL; }
            memcpy(buf, payload, payload_size);
            *out_len = payload_size;
            return buf;
        }
        case 1: {

            if (payload_size == 0) {
                seed_len = 1;
            } else {
                cap = payload_size > 64 ? 64 : payload_size;
                seed_len = 1 + (seed_sel % cap);
            }
            shaped_size = 1024 + (size_t)(size_sel % 64) * 768;
            if (shaped_size > MAX_SHAPED_SIZE) shaped_size = MAX_SHAPED_SIZE;
            buf = (unsigned char *)malloc(shaped_size);
            if (!buf) { *out_len = 0; return NULL; }
            for (i = 0; i < shaped_size; i++) {
                buf[i] = payload_size > 0 ? payload[i % seed_len] : (uint8_t)(0x41 + (i % 23));
            }
            *out_len = shaped_size;
            return buf;
        }
        case 2: {

            shaped_size = 1024 + (size_t)(size_sel % 64) * 768;
            if (shaped_size > MAX_SHAPED_SIZE) shaped_size = MAX_SHAPED_SIZE;
            buf = (unsigned char *)malloc(shaped_size);
            if (!buf) { *out_len = 0; return NULL; }
            run_len = 16 + (run_sel % 240);
            val = payload_size > 0 ? payload[0] : 0xAA;
            i = 0;
            run_idx = 0;
            while (i < shaped_size) {
                chunk = run_len;
                if (chunk > shaped_size - i) chunk = shaped_size - i;
                memset(buf + i, val, chunk);
                i += chunk;
                run_idx++;
                val = payload_size > 0 ? payload[run_idx % payload_size] : (uint8_t)(val + 1);
            }
            *out_len = shaped_size;
            return buf;
        }
        default: {

            shaped_size = 1024 + (size_t)(size_sel % 64) * 768;
            if (shaped_size > MAX_SHAPED_SIZE) shaped_size = MAX_SHAPED_SIZE;
            buf = (unsigned char *)malloc(shaped_size);
            if (!buf) { *out_len = 0; return NULL; }
            if (payload_size == 0) {
                seed_len = 1;
            } else {
                cap = payload_size > 32 ? 32 : payload_size;
                seed_len = 1 + (seed_sel % cap);
            }
            half = shaped_size / 2;
            for (i = 0; i < half; i++) {
                buf[i] = payload_size > 0 ? payload[i % payload_size] : (uint8_t)(i & 0xff);
            }
            for (i = half; i < shaped_size; i++) {
                buf[i] = payload_size > 0 ? payload[(i - half) % seed_len] : (uint8_t)0x42;
            }
            *out_len = shaped_size;
            return buf;
        }
    }
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    uint8_t level_sel;
    int window_bits;
    int mem_level;
    int strategy;
    uint8_t flush_byte1;
    uint8_t flush_byte2;
    uint8_t out_buf_mode;
    uint8_t extra_calls_after_done;
    uint8_t reset_sel;
    uint8_t probe_sel;
    uint8_t probe_byte;
    uint8_t payload_mode;
    uint8_t size_sel;
    uint8_t seed_sel;
    uint8_t use_custom_alloc;
    uint8_t full_flush_repeat_sel;
    int level;
    int method;
    int bp_window_bits;
    int bp_mem_level;
    int bp_method;
    const uint8_t *payload;
    size_t payload_size;
    size_t shaped_len;
    unsigned char *shaped;
    mz_stream stream;
    int init_status;
    size_t out_buf_size;
    unsigned char *out_buf;
    size_t chunk1;
    size_t chunk2;
    const uint8_t *chunks[2];
    size_t chunk_sizes[2];
    int flushes[2];
    int status;
    int c;
    int iterations;
    int flush;
    int repeats;
    int r;
    int bad_flush;
    unsigned char tiny;
    mz_stream bp_stream;
    unsigned char tiny_out[4];
    int bp_status;
    int r_status;
    int expensive;
    unsigned char bufprobe_out[4];

    if (size < 16) return 0;

    level_sel              = data[0];
    window_bits            = (data[1] & 1) ? MZ_DEFAULT_WINDOW_BITS : -MZ_DEFAULT_WINDOW_BITS;
    mem_level               = 1 + (data[2] % 9);
    strategy                = data[3] % 5;
    flush_byte1             = data[4];
    flush_byte2             = data[5];
    out_buf_mode            = data[6];
    extra_calls_after_done  = data[7] & 1;
    reset_sel               = data[8];
    probe_sel               = data[9];
    probe_byte              = data[10];
    payload_mode            = data[11];
    size_sel                = data[12];
    seed_sel                = data[13];
    use_custom_alloc        = data[14] & 1;
    full_flush_repeat_sel   = data[15];
    method                  = MZ_DEFLATED;

    switch (level_sel % 4) {
        case 0: level = 0; break;
        case 1: level = 1; break;
        default: level = ((int)(level_sel % 12)) - 1; break;
    }

    if ((data[2] % 32) == 0) {

        mem_level = (data[2] & 1) ? 0 : 10 + (data[2] % 5);
    }
    if ((data[1] % 32) == 0) {

        window_bits = (data[1] & 1) ? 9 : 31;
    }
    if ((data[3] % 64) == 0) {

        method = MZ_DEFLATED + 1;
    }

    payload = data + 16;
    payload_size = size - 16;

    shaped_len = 0;
    shaped = build_payload(payload, payload_size, payload_mode,
                            size_sel, seed_sel, full_flush_repeat_sel,
                            &shaped_len);

    memset(&stream, 0, sizeof(stream));
    if (use_custom_alloc) {

        stream.zalloc = custom_alloc_func;
        stream.zfree  = custom_free_func;
    }

    if (data[0] % 16 == 0) {

        init_status = mz_deflateInit(&stream, level);
    } else {
        init_status = mz_deflateInit2(&stream, level, method, window_bits, mem_level, strategy);
    }
    if (init_status != MZ_OK) {

        free(shaped);
        return 0;
    }

    stream.next_in  = NULL;
    stream.avail_in = 0;
    stream.next_out = bufprobe_out;
    stream.avail_out = sizeof(bufprobe_out);
    (void)mz_deflate(&stream, MZ_NO_FLUSH);

    switch (out_buf_mode % 7) {
        case 0: out_buf_size = 1;   break;
        case 1: out_buf_size = 8;   break;
        case 2: out_buf_size = 64;  break;
        case 3: out_buf_size = (size_t)TDEFL_OUT_BUF_SIZE - 1; break;
        case 4: out_buf_size = (size_t)TDEFL_OUT_BUF_SIZE + 1; break;
        case 5: out_buf_size = (size_t)TDEFL_LZ_CODE_BUF_SIZE; break;
        default: {
            mz_ulong bound = mz_deflateBound(&stream, (mz_ulong)shaped_len);
            out_buf_size = (size_t)(bound ? bound : 1024) + 1024;
        }
    }
    out_buf = (unsigned char *)malloc(out_buf_size);
    if (!out_buf) { mz_deflateEnd(&stream); free(shaped); return 0; }

    expensive = (shaped_len > 16 * 1024) && (level > 6 || level < 0);

    chunk1 = shaped_len / 2;
    chunk2 = shaped_len - chunk1;

    if (shaped != NULL && shaped_len > 0) {
        chunks[0] = shaped;
        chunks[1] = shaped + chunk1;
    } else {
        chunks[0] = NULL;
        chunks[1] = NULL;
    }
    chunk_sizes[0] = chunk1;
    chunk_sizes[1] = chunk2;
    flushes[0] = map_flush(flush_byte1);
    flushes[1] = map_flush(flush_byte2);

    status = MZ_OK;
    for (c = 0; c < 2; c++) {
        stream.next_in  = chunk_sizes[c] > 0 ? chunks[c] : NULL;
        stream.avail_in = (unsigned int)chunk_sizes[c];
        flush = (c == 1) ? MZ_FINISH : flushes[c];

        iterations = 0;
        do {
            stream.next_out  = out_buf;
            stream.avail_out = (unsigned int)out_buf_size;
            status = mz_deflate(&stream, flush);
            iterations++;
            if (status != MZ_OK && status != MZ_STREAM_END) break;
        } while (stream.avail_in > 0 && iterations < 10000 && status != MZ_STREAM_END);

        if (status == MZ_STREAM_END) break;
    }

    if (!expensive && status != MZ_STREAM_END && (full_flush_repeat_sel & 1)) {
        repeats = 1 + (full_flush_repeat_sel % 4);
        for (r = 0; r < repeats; r++) {
            stream.next_in = NULL;
            stream.avail_in = 0;
            stream.next_out = out_buf;
            stream.avail_out = (unsigned int)out_buf_size;
            (void)mz_deflate(&stream, MZ_FULL_FLUSH);
        }
    }

    if (extra_calls_after_done) {
        stream.next_out = out_buf;
        stream.avail_out = (unsigned int)out_buf_size;
        stream.next_in = NULL;
        stream.avail_in = 0;
        (void)mz_deflate(&stream, MZ_FINISH);
        (void)mz_deflate(&stream, MZ_NO_FLUSH);
    }

    if (!expensive && (reset_sel & 1)) {
        if (mz_deflateReset(&stream) == MZ_OK) {
            stream.next_in = shaped_len > 0 ? shaped : NULL;
            stream.avail_in = (unsigned int)shaped_len;
            stream.next_out = out_buf;
            stream.avail_out = (unsigned int)out_buf_size;
            iterations = 0;
            do {
                r_status = mz_deflate(&stream, MZ_FINISH);
                iterations++;
                if (stream.avail_out == 0 && r_status == MZ_OK) {
                    stream.next_out = out_buf;
                    stream.avail_out = (unsigned int)out_buf_size;
                }
            } while (r_status == MZ_OK && iterations < 10000);
        }
    }

    if ((probe_sel % 10) == 0) {
        bad_flush = (int)probe_byte - 128;
        stream.next_out = (probe_byte & 1) ? NULL : &tiny;
        stream.avail_out = (probe_byte & 2) ? 0 : 1;
        stream.next_in = NULL;
        stream.avail_in = 0;
        (void)mz_deflate(&stream, bad_flush);
    }

    mz_deflateEnd(&stream);

    bp_window_bits = (data[6] & 1) ? MZ_DEFAULT_WINDOW_BITS : -MZ_DEFAULT_WINDOW_BITS;
    bp_mem_level = 1 + (data[7] % 9);
    bp_method = MZ_DEFLATED;
    if ((data[8] % 32) == 0) {
        bp_mem_level = (data[8] & 1) ? 0 : 10 + (data[8] % 5);
    }
    if ((data[9] % 32) == 0) {
        bp_window_bits = (data[9] & 1) ? 9 : 31;
    }
    if ((data[10] % 64) == 0) {
        bp_method = MZ_DEFLATED + 1;
    }

    memset(&bp_stream, 0, sizeof(bp_stream));
    if (mz_deflateInit2(&bp_stream, level, bp_method, bp_window_bits, bp_mem_level, strategy) == MZ_OK) {
        bp_stream.next_in = shaped_len > 0 ? shaped : NULL;
        bp_stream.avail_in = (unsigned int)shaped_len;
        bp_stream.next_out = tiny_out;
        bp_stream.avail_out = sizeof(tiny_out);
        bp_status = mz_deflate(&bp_stream, MZ_FINISH);
        if (bp_status == MZ_OK) {

            bp_stream.next_out = tiny_out;
            bp_stream.avail_out = sizeof(tiny_out);
            bp_stream.next_in = NULL;
            bp_stream.avail_in = 0;
            (void)mz_deflate(&bp_stream, MZ_NO_FLUSH);
        }
        mz_deflateEnd(&bp_stream);
    }

    free(out_buf);
    free(shaped);
    return 0;
}