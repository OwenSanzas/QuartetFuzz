// Copyright 2024 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <brotli/decode.h>

/**
 * LibFuzzer harness for Brotli decompression engine.
 * 
 * This harness tests the core Brotli decompression state machine by:
 * 1. Initializing a decoder instance.
 * 2. Configuring decoder parameters (large window, ring buffer reallocation) based on input.
 * 3. Optionally attaching a portion of the input as a shared dictionary.
 * 4. Feeding the remaining input to the streaming decompressor in varying chunk sizes.
 * 5. Managing output buffers and enforcing output size limits to prevent OOM/hangs.
 */
int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  // We need at least 2 bytes: 1 for config, 1 for input data.
  if (size < 2) return 0;

  // Use the first byte for configuration.
  uint8_t config = data[0];
  const uint8_t* input = data + 1;
  size_t input_size = size - 1;

  // Create the decoder instance.
  BrotliDecoderState* state = BrotliDecoderCreateInstance(NULL, NULL, NULL);
  if (!state) return 0;

  // Set parameters based on the config byte.
  // Bit 0: BROTLI_DECODER_PARAM_DISABLE_RING_BUFFER_REALLOCATION
  BrotliDecoderSetParameter(state, BROTLI_DECODER_PARAM_DISABLE_RING_BUFFER_REALLOCATION,
                            (config & 1) ? 1 : 0);
  // Bit 1: BROTLI_DECODER_PARAM_LARGE_WINDOW
  BrotliDecoderSetParameter(state, BROTLI_DECODER_PARAM_LARGE_WINDOW,
                            (config & 2) ? 1 : 0);

  // If bit 2 is set, use a portion of the input as a dictionary.
  // Dictionaries must be attached before decoding starts.
  if ((config & 4) && input_size > 8) {
    size_t dict_size = input_size / 8;
    if (dict_size > 4096) dict_size = 4096;
    BrotliDecoderAttachDictionary(state, BROTLI_SHARED_DICTIONARY_RAW,
                                  dict_size, input);
    input += dict_size;
    input_size -= dict_size;
  }

  // Determine addend for chunked decoding (bits 3-5 of config).
  // This allows the fuzzer to explore both fast and slow (byte-by-byte) decoding paths.
  size_t addend = (config >> 3) & 7;
  if (addend == 0) addend = input_size;

  const int kBufferSize = 4096;
  uint8_t* buffer = (uint8_t*)malloc(kBufferSize);
  if (!buffer) {
    BrotliDecoderDestroyInstance(state);
    return 0;
  }

  /* The biggest "magic number" in brotli is 16MiB - 16, so no need to check
     the cases with much longer output. Limit to 64MiB to be safe but bounded. */
  const size_t total_out_limit = (1 << 26);
  size_t total_out = 0;

  const uint8_t* next_in = input;
  for (size_t i = 0; i < input_size; ) {
    size_t chunk_size = addend;
    if (i + chunk_size > input_size) {
      chunk_size = input_size - i;
    }
    size_t avail_in = chunk_size;
    i += chunk_size;

    // Decompress the current chunk.
    BrotliDecoderResult result = BROTLI_DECODER_RESULT_NEEDS_MORE_OUTPUT;
    while (result == BROTLI_DECODER_RESULT_NEEDS_MORE_OUTPUT) {
      size_t avail_out = kBufferSize;
      uint8_t* next_out = buffer;
      result = BrotliDecoderDecompressStream(
          state, &avail_in, &next_in, &avail_out, &next_out, &total_out);
      
      // Stop if we exceed the output limit or encounter an error.
      if (total_out > total_out_limit) break;
      if (result == BROTLI_DECODER_RESULT_ERROR || result == BROTLI_DECODER_RESULT_SUCCESS) break;
    }
    
    if (total_out > total_out_limit) break;
    if (result == BROTLI_DECODER_RESULT_ERROR || result == BROTLI_DECODER_RESULT_SUCCESS) break;
  }

  // Finalize and cleanup.
  BrotliDecoderDestroyInstance(state);
  free(buffer);
  return 0;
}
