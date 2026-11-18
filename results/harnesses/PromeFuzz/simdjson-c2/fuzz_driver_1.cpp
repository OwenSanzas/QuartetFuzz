#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <cstdint>
#include <cstddef>
#include <exception>

#include "simdjson.h"
#include "simdjson/dom/serialization.h"

static void try_minify_element(simdjson::dom::element &elem) {
    try {
        std::string result = simdjson::minify(elem);
        (void)result;
    } catch (const std::exception &e) {
        (void)e;
    } catch (...) {
    }
}

static void try_minify_result(simdjson::simdjson_result<simdjson::dom::element> &res) {
    try {
        std::string result = simdjson::minify(res);
        (void)result;
    } catch (const std::exception &e) {
        (void)e;
    } catch (...) {
    }
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size) {
    if (Data == nullptr || Size == 0) {
        return 0;
    }

    const char *dummy_filename = "./dummy_file";
    FILE *fp = fopen(dummy_filename, "wb");
    if (fp) {
        fwrite(Data, 1, Size, fp);
        fclose(fp);
    }

    std::string json_str(reinterpret_cast<const char *>(Data), Size);

    simdjson::dom::parser parser;

    try {
        simdjson::dom::element elem = parser.parse(json_str);

        try_minify_element(elem);
    } catch (const std::exception &e) {
        (void)e;
    } catch (...) {
    }

    try {
        simdjson::simdjson_result<simdjson::dom::element> res = parser.parse(json_str);
        try_minify_result(res);
    } catch (const std::exception &e) {
        (void)e;
    } catch (...) {
    }

    try {
        simdjson::dom::parser file_parser;
        simdjson::dom::element file_elem = file_parser.load(dummy_filename);
        try_minify_element(file_elem);
    } catch (const std::exception &e) {
        (void)e;
    } catch (...) {
    }

    try {
        simdjson::dom::parser file_parser2;
        simdjson::simdjson_result<simdjson::dom::element> file_res = file_parser2.load(dummy_filename);
        try_minify_result(file_res);
    } catch (const std::exception &e) {
        (void)e;
    } catch (...) {
    }

    try {
        simdjson::padded_string padded_data(reinterpret_cast<const char *>(Data), Size);
        simdjson::dom::parser padded_parser;
        simdjson::dom::element padded_elem = padded_parser.parse(padded_data);
        try_minify_element(padded_elem);
    } catch (const std::exception &e) {
        (void)e;
    } catch (...) {
    }

    try {
        simdjson::padded_string padded_data2(reinterpret_cast<const char *>(Data), Size);
        simdjson::dom::parser padded_parser2;
        simdjson::simdjson_result<simdjson::dom::element> padded_res = padded_parser2.parse(padded_data2);
        try_minify_result(padded_res);
    } catch (const std::exception &e) {
        (void)e;
    } catch (...) {
    }

    return 0;
}