#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <cstdint>
#include <cstddef>

extern "C" {
#include "jq.h"
#include "jv.h"
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size) {
    if (Size == 0) {
        return 0;
    }

    size_t sep = Size;
    for (size_t i = 0; i < Size; ++i) {
        if (Data[i] == 0) {
            sep = i;
            break;
        }
    }

    std::string program;
    std::string json_input;

    if (sep < Size) {
        program.assign(reinterpret_cast<const char *>(Data), sep);
        size_t remaining_start = sep + 1;
        if (remaining_start < Size) {
            json_input.assign(reinterpret_cast<const char *>(Data + remaining_start),
                               Size - remaining_start);
        } else {
            json_input = "null";
        }
    } else {
        program.assign(reinterpret_cast<const char *>(Data), Size);
        json_input = "null";
    }

    if (program.empty()) {
        program = ".";
    }

    jq_state *jq = jq_init();
    if (!jq) {
        return 0;
    }

    int compiled = jq_compile(jq, program.c_str());
    if (!compiled) {
        jq_teardown(&jq);
        return 0;
    }

    jv input = jv_parse_sized(json_input.c_str(), (int)json_input.size());
    if (!jv_is_valid(input)) {
        jv_free(input);
        input = jv_null();
    }

    jq_start(jq, input, 0);

    int max_iterations = 1000;
    while (max_iterations-- > 0) {
        jv result = jq_next(jq);
        if (!jv_is_valid(result)) {

            jv msg = jv_invalid_get_msg(result);
            jv_free(msg);
            break;
        }
        jv_free(result);
    }

    jq_teardown(&jq);

    return 0;
}