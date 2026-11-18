#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <cstdint>
#include <cstddef>
#include <boost/regex.hpp>
#include <string>
#include <vector>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iterator>

static std::string ExtractString(const uint8_t *Data, size_t Size, size_t &offset, size_t maxLen)
{
    if (offset >= Size) return std::string();
    size_t len = Size - offset;
    if (len > maxLen) len = maxLen;
    std::string s(reinterpret_cast<const char*>(Data + offset), len);
    offset += len;
    return s;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size)
{
    if (Size < 4) {
        return 0;
    }

    size_t offset = 0;

    uint8_t flagByte = Data[offset++];
    boost::regex_constants::match_flag_type flags = boost::regex_constants::match_default;

    switch (flagByte % 6) {
        case 0:
            flags = boost::regex_constants::match_default;
            break;
        case 1:
            flags = boost::regex_constants::format_default;
            break;
        case 2:
            flags = boost::regex_constants::format_no_copy;
            break;
        case 3:
            flags = boost::regex_constants::format_first_only;
            break;
        case 4:
            flags = boost::regex_constants::format_sed;
            break;
        case 5:
            flags = static_cast<boost::regex_constants::match_flag_type>(
                boost::regex_constants::format_no_copy |
                boost::regex_constants::format_first_only);
            break;
        default:
            flags = boost::regex_constants::match_default;
            break;
    }

    size_t remaining = (offset < Size) ? (Size - offset) : 0;
    size_t partLen = remaining / 3;
    if (partLen == 0) partLen = 1;

    std::string pattern = ExtractString(Data, Size, offset, partLen);
    std::string subject = ExtractString(Data, Size, offset, partLen);
    std::string fmt = ExtractString(Data, Size, offset, remaining);

    if (pattern.empty()) {
        pattern = ".";
    }

    boost::regex e;
    try {
        e.assign(pattern, boost::regex::normal);
    } catch (const boost::regex_error &) {
        return 0;
    } catch (const std::exception &) {
        return 0;
    }

    try {
        std::string result;
        boost::regex_replace(std::back_inserter(result),
                              subject.begin(),
                              subject.end(),
                              e,
                              fmt,
                              flags);
    } catch (const boost::regex_error &) {

    } catch (const std::exception &) {

    } catch (...) {

    }

    try {
        std::string result2 = boost::regex_replace(subject, e, fmt, flags);
        (void)result2;
    } catch (const boost::regex_error &) {

    } catch (const std::exception &) {

    } catch (...) {

    }

    try {
        boost::regex_constants::match_flag_type altFlags =
            static_cast<boost::regex_constants::match_flag_type>(
                flags | boost::regex_constants::match_not_null);
        std::string result3;
        boost::regex_replace(std::back_inserter(result3),
                              subject.begin(),
                              subject.end(),
                              e,
                              fmt,
                              altFlags);
    } catch (const boost::regex_error &) {
    } catch (const std::exception &) {
    } catch (...) {
    }

    {
        std::ofstream ofs("./dummy_file", std::ios::binary | std::ios::trunc);
        if (ofs.is_open()) {
            ofs.write(reinterpret_cast<const char*>(Data), static_cast<std::streamsize>(Size));
        }
    }

    return 0;
}