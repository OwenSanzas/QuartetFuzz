#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <cstdint>
#include <cstddef>
#include <memory>

#include "simdjson.h"
#include "simdjson/dom/parser-inl.h"

using namespace simdjson;

static void try_parse_char_buf(dom::parser &parser, const char *buf, size_t len, bool realloc_if_needed) {
    dom::element elem;
    auto err = parser.parse(buf, len, realloc_if_needed).get(elem);
    if (!err) {

        auto type = elem.type();
        (void)type;
        switch (type) {
            case dom::element_type::OBJECT: {
                dom::object obj;
                if (!elem.get(obj)) {
                    for (auto field : obj) {
                        (void)field.key;
                        (void)field.value;
                    }
                }
                break;
            }
            case dom::element_type::ARRAY: {
                dom::array arr;
                if (!elem.get(arr)) {
                    for (auto item : arr) {
                        (void)item;
                    }
                }
                break;
            }
            case dom::element_type::STRING: {
                std::string_view sv;
                (void)elem.get(sv);
                break;
            }
            case dom::element_type::INT64: {
                int64_t v;
                (void)elem.get(v);
                break;
            }
            case dom::element_type::UINT64: {
                uint64_t v;
                (void)elem.get(v);
                break;
            }
            case dom::element_type::DOUBLE: {
                double v;
                (void)elem.get(v);
                break;
            }
            case dom::element_type::BOOL: {
                bool v;
                (void)elem.get(v);
                break;
            }
            case dom::element_type::NULL_VALUE:
            default:
                break;
        }
    }
}

static void try_parse_uint8_buf(dom::parser &parser, const uint8_t *buf, size_t len, bool realloc_if_needed) {
    dom::element elem;
    auto err = parser.parse(buf, len, realloc_if_needed).get(elem);
    if (!err) {
        auto type = elem.type();
        (void)type;
    }
}

static void try_parse_std_string(dom::parser &parser, const std::string &s) {
    dom::element elem;
    auto err = parser.parse(s).get(elem);
    if (!err) {
        auto type = elem.type();
        (void)type;
    }
}

static void try_parse_padded_string(dom::parser &parser, const padded_string &ps) {
    dom::element elem;
    auto err = parser.parse(ps).get(elem);
    if (!err) {
        auto type = elem.type();
        (void)type;
    }
}

static void try_parse_padded_string_view(dom::parser &parser, const padded_string_view &psv) {
    dom::element elem;
    auto err = parser.parse(psv).get(elem);
    if (!err) {
        auto type = elem.type();
        (void)type;
    }
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size) {
    if (Size == 0) {
        return 0;
    }

    std::string json_str(reinterpret_cast<const char *>(Data), Size);

    {
        dom::parser parser;
        try_parse_char_buf(parser, json_str.data(), json_str.size(), true);
    }

    {
        dom::parser parser;
        std::vector<char> padded_buf(Size + SIMDJSON_PADDING, 0);
        std::memcpy(padded_buf.data(), Data, Size);
        try_parse_char_buf(parser, padded_buf.data(), Size, false);
    }

    {
        dom::parser parser;
        try_parse_uint8_buf(parser, Data, Size, true);
    }

    {
        dom::parser parser;
        std::vector<uint8_t> padded_buf(Size + SIMDJSON_PADDING, 0);
        std::memcpy(padded_buf.data(), Data, Size);
        try_parse_uint8_buf(parser, padded_buf.data(), Size, false);
    }

    {
        dom::parser parser;
        try_parse_std_string(parser, json_str);
    }

    {
        dom::parser parser;
        padded_string ps(json_str);
        try_parse_padded_string(parser, ps);
    }

    {
        dom::parser parser;

        std::string mutable_json_str = json_str;
        auto view_result = simdjson::pad(mutable_json_str);
        try_parse_padded_string_view(parser, view_result);
    }

    {
        dom::parser parser1;
        try_parse_char_buf(parser1, json_str.data(), json_str.size(), true);
        dom::parser parser2(std::move(parser1));

        try_parse_char_buf(parser2, json_str.data(), json_str.size(), true);
    }

    return 0;
}