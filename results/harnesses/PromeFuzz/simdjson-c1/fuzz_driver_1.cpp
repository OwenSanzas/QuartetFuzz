#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <cstdint>
#include <cstddef>
#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <string>
#include <sstream>
#include <fstream>
#include <simdjson.h>
#include <simdjson/dom/element.h>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size) {

    const char *dummy_filename = "./dummy_file";
    std::ofstream ofs(dummy_filename, std::ios::binary);
    if (ofs.is_open()) {
        ofs.write(reinterpret_cast<const char *>(Data), static_cast<std::streamsize>(Size));
        ofs.close();
    }

    std::string input(reinterpret_cast<const char *>(Data), Size);

    simdjson::dom::parser parser;
    simdjson::dom::element elem;

    auto error = parser.parse(input).get(elem);
    if (error) {

        return 0;
    }

    {
        std::ostringstream oss;
        try {
            elem.dump_raw_tape(oss);
        } catch (...) {

        }
    }

    {
        std::ofstream ofs2("./dummy_file", std::ios::binary | std::ios::app);
        if (ofs2.is_open()) {
            try {
                elem.dump_raw_tape(ofs2);
            } catch (...) {
            }
            ofs2.close();
        }
    }

    {
        std::stringstream ss;
        try {
            elem.dump_raw_tape(ss);
        } catch (...) {
        }
    }

    return 0;
}