#include <cstdint>
#include <cstddef>
#include <cstring>
#include <sstream>
#include <stdexcept>

#include "msgpack.hpp"

static bool always_reference(msgpack::type::object_type  ,
                             std::size_t  , void*  ) {
    return true;
}

static void consume_object(const msgpack::object& obj) {
    std::ostringstream oss;
    oss << obj;

    try {
        msgpack::sbuffer sbuf;
        msgpack::pack(sbuf, obj);
    } catch (const std::exception&) {
    }

    switch (obj.type) {
        case msgpack::type::POSITIVE_INTEGER:
        case msgpack::type::NEGATIVE_INTEGER: {
            try {
                int64_t v = obj.as<int64_t>();
                (void)v;
            } catch (const std::exception&) {
            }
            break;
        }
        case msgpack::type::STR: {
            try {
                std::string s = obj.as<std::string>();
                (void)s;
            } catch (const std::exception&) {
            }
            break;
        }
        default:
            break;
    }
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (size < 2) return 0;

    uint8_t selector = data[0];
    const char* payload = reinterpret_cast<const char*>(data + 1);
    std::size_t payload_len = size - 1;

    int path = selector % 4;

    try {
        if (path == 0) {

            msgpack::object_handle oh = msgpack::unpack(payload, payload_len);
            consume_object(oh.get());
        } else if (path == 1) {

            msgpack::object_handle oh = msgpack::unpack(
                payload, payload_len, always_reference, nullptr);
            consume_object(oh.get());
        } else if (path == 2) {

            if (payload_len < 4) return 0;
            std::size_t lim_array = static_cast<uint8_t>(payload[0]);
            std::size_t lim_map   = static_cast<uint8_t>(payload[1]);
            std::size_t lim_str   = static_cast<uint8_t>(payload[2]);
            std::size_t lim_bin   = static_cast<uint8_t>(payload[3]);
            msgpack::unpack_limit limit(lim_array, lim_map, lim_str, lim_bin,
                                         lim_bin, lim_array + lim_map);
            const char* sub_payload = payload + 4;
            std::size_t sub_len = payload_len - 4;
            msgpack::object_handle oh = msgpack::unpack(
                sub_payload, sub_len, nullptr, nullptr, limit);
            consume_object(oh.get());
        } else {

            std::size_t off = 0;
            bool referenced = false;
            while (off < payload_len) {
                std::size_t prev_off = off;
                msgpack::object_handle oh;
                try {
                    oh = msgpack::unpack(payload, payload_len, off, referenced);
                } catch (const msgpack::unpack_error&) {
                    break;
                } catch (const std::bad_alloc&) {
                    break;
                }
                consume_object(oh.get());
                if (off <= prev_off) break;
            }

            msgpack::unpacker pac;
            std::size_t first_chunk = payload_len / 2;
            std::size_t second_chunk = payload_len - first_chunk;

            pac.reserve_buffer(first_chunk > 0 ? first_chunk : 1);
            if (first_chunk > 0) {
                std::memcpy(pac.buffer(), payload, first_chunk);
            }
            pac.buffer_consumed(first_chunk);

            msgpack::object_handle sh;
            try {
                while (pac.next(sh)) {
                    consume_object(sh.get());
                }
            } catch (const msgpack::unpack_error&) {
            } catch (const std::bad_alloc&) {
            }

            if (second_chunk > 0) {
                pac.reserve_buffer(second_chunk);
                std::memcpy(pac.buffer(), payload + first_chunk, second_chunk);
                pac.buffer_consumed(second_chunk);
                try {
                    while (pac.next(sh)) {
                        consume_object(sh.get());
                    }
                } catch (const msgpack::unpack_error&) {
                } catch (const std::bad_alloc&) {
                }
            }
        }
    } catch (const msgpack::unpack_error&) {

    } catch (const std::bad_alloc&) {

    } catch (const std::exception&) {

    }

    return 0;
}
