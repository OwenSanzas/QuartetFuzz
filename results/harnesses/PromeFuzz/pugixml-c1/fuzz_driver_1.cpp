#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <cstdint>
#include <cstddef>
#include "pugixml.hpp"
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <vector>
#include <fstream>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size) {
    if (Data == nullptr || Size == 0) {
        return 0;
    }

    {
        pugi::xml_document doc;
        pugi::xml_parse_result result = doc.load_buffer(Data, Size);
        (void)result;
    }

    {
        pugi::xml_document doc;
        pugi::xml_parse_result result = doc.load_buffer(Data, Size, pugi::parse_full);
        (void)result;
    }

    {
        pugi::xml_document doc;
        pugi::xml_parse_result result = doc.load_buffer(Data, Size, pugi::parse_minimal);
        (void)result;
    }

    {
        pugi::xml_document doc;
        pugi::xml_parse_result result = doc.load_buffer(Data, Size, pugi::parse_default, pugi::encoding_utf8);
        (void)result;
    }

    {
        pugi::xml_document doc;
        pugi::xml_parse_result result = doc.load_buffer(Data, Size, pugi::parse_default, pugi::encoding_utf16);
        (void)result;
    }

    {
        pugi::xml_document doc;
        pugi::xml_parse_result result = doc.load_buffer(Data, Size, pugi::parse_default, pugi::encoding_utf32);
        (void)result;
    }

    {
        pugi::xml_document doc;
        pugi::xml_parse_result result = doc.load_buffer(Data, Size, pugi::parse_default, pugi::encoding_latin1);
        (void)result;
    }

    {
        pugi::xml_document doc;
        pugi::xml_parse_result result = doc.load_buffer(Data, Size, pugi::parse_default, pugi::encoding_wchar);
        (void)result;
    }

    {
        pugi::xml_document doc;
        unsigned int options = pugi::parse_default | pugi::parse_ws_pcdata |
                                pugi::parse_comments | pugi::parse_cdata |
                                pugi::parse_declaration;
        pugi::xml_parse_result result = doc.load_buffer(Data, Size, options, pugi::encoding_auto);
        (void)result;
    }

    {
        pugi::xml_document doc;
        pugi::xml_parse_result result = doc.load_buffer(Data, Size);
        if (result) {
            pugi::xml_node root = doc.document_element();
            if (root) {
                for (pugi::xml_node child = root.first_child(); child; child = child.next_sibling()) {

                    const char* name = child.name();
                    (void)name;
                }
            }
        }
    }

    {
        std::ofstream ofs("./dummy_file", std::ios::binary);
        if (ofs.is_open()) {
            ofs.write(reinterpret_cast<const char*>(Data), Size);
            ofs.close();
        }
    }

    if (Size > 1) {
        pugi::xml_document doc;
        size_t reducedSize = Size / 2;
        pugi::xml_parse_result result = doc.load_buffer(Data, reducedSize);
        (void)result;
    }

    {
        pugi::xml_document doc;
        pugi::xml_parse_result result = doc.load_buffer(Data, Size, pugi::parse_trim_pcdata);
        (void)result;
    }

    {
        pugi::xml_document doc;
        pugi::xml_parse_result result1 = doc.load_buffer(Data, Size, pugi::parse_default, pugi::encoding_auto);
        (void)result1;
        pugi::xml_parse_result result2 = doc.load_buffer(Data, Size, pugi::parse_full, pugi::encoding_utf8);
        (void)result2;
    }

    return 0;
}