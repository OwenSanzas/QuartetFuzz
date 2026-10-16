#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "api/yajl_parse.h"

/*
 * Copyright 2010, Lloyd Hilaiel.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 * 
 *  1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 * 
 *  2. Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in
 *     the documentation and/or other materials provided with the
 *     distribution.
 * 
 *  3. Neither the name of Lloyd Hilaiel nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */ 

// Define dummy callbacks to exercise the parser's logic
static int dummy_null(void * ctx) { (void)ctx; return 1; }
static int dummy_boolean(void * ctx, int boolVal) { (void)ctx; (void)boolVal; return 1; }
static int dummy_integer(void * ctx, long integerVal) { (void)ctx; (void)integerVal; return 1; }
static int dummy_double(void * ctx, double doubleVal) { (void)ctx; (void)doubleVal; return 1; }
static int dummy_number(void * ctx, const char * s, unsigned int l) { (void)ctx; (void)s; (void)l; return 1; }
static int dummy_string(void * ctx, const unsigned char * s, unsigned int l) { (void)ctx; (void)s; (void)l; return 1; }
static int dummy_start_map(void * ctx) { (void)ctx; return 1; }
static int dummy_map_key(void * ctx, const unsigned char * s, unsigned int l) { (void)ctx; (void)s; (void)l; return 1; }
static int dummy_end_map(void * ctx) { (void)ctx; return 1; }
static int dummy_start_array(void * ctx) { (void)ctx; return 1; }
static int dummy_end_array(void * ctx) { (void)ctx; return 1; }

static const yajl_callbacks callbacks = {
    dummy_null,
    dummy_boolean,
    dummy_integer,
    dummy_double,
    dummy_number,
    dummy_string,
    dummy_start_map,
    dummy_map_key,
    dummy_end_map,
    dummy_start_array,
    dummy_end_array
};

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size < 3) return 0;

    // Use the first few bytes to configure the parser
    yajl_parser_config cfg;
    cfg.allowComments = data[0] & 1;
    cfg.checkUTF8 = data[1] & 1;
    int verbose_error = data[2] & 1;

    const unsigned char *json_data = (const unsigned char *)(data + 3);
    unsigned int json_size = (unsigned int)(size - 3);

    yajl_handle hand = yajl_alloc(&callbacks, &cfg, NULL, NULL);
    if (!hand) {
        return 0;
    }

    yajl_status stat = yajl_parse(hand, json_data, json_size);
    
    // According to P2.7, if yajl_get_error is called, yajl_free_error must be called.
    unsigned char * err = yajl_get_error(hand, verbose_error, json_data, json_size);
    if (err) {
        yajl_free_error(hand, err);
    }

    if (stat == yajl_status_ok || stat == yajl_status_insufficient_data) {
        yajl_parse_complete(hand);
    }

    // Exercise status string
    yajl_status_to_string(stat);
    
    // Exercise bytes consumed
    yajl_get_bytes_consumed(hand);

    yajl_free(hand);

    return 0;
}
