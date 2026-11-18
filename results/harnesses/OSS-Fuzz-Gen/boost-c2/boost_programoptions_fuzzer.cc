#include <fuzzer/FuzzedDataProvider.h>

#include <cstddef>
#include <cstdint>
#include <sstream>
#include <string>
#include <vector>

#include <boost/program_options/options_description.hpp>
#include <boost/program_options/parsers.hpp>
#include <boost/program_options/value_semantic.hpp>
#include <boost/program_options/errors.hpp>

using namespace boost::program_options;

namespace {

options_description build_description(FuzzedDataProvider &stream,
                                       std::vector<std::string> &known_names) {
  options_description desc;
  desc.add_options()
      ("gv1", new untyped_value())
      ("gv2", new untyped_value())
      ("empty_value", new untyped_value())
      ("plug*", new untyped_value())
      ("m1.v1", new untyped_value())
      ("m1.v2", new untyped_value())
      ("m1.v3", new untyped_value())
      ("b", bool_switch());

  known_names.push_back("gv1");
  known_names.push_back("gv2");
  known_names.push_back("empty_value");
  known_names.push_back("m1.v1");
  known_names.push_back("m1.v2");
  known_names.push_back("m1.v3");
  known_names.push_back("b");

  if (stream.ConsumeIntegralInRange<uint8_t>(0, 7) == 0) {
    desc.add_options()("plugx*", new untyped_value());
  }

  if (stream.ConsumeIntegralInRange<uint8_t>(0, 15) == 0) {
    desc.add_options()(",x", new untyped_value());
  }

  std::string fuzzed_name =
      stream.ConsumeBytesAsString(stream.ConsumeIntegralInRange<uint8_t>(0, 32));
  if (!fuzzed_name.empty()) {

    desc.add_options()(fuzzed_name.c_str(), new untyped_value());
    known_names.push_back(fuzzed_name);
  }

  return desc;
}

std::string build_config_content(FuzzedDataProvider &stream,
                                  const std::vector<std::string> &known_names) {
  std::string out;
  uint8_t line_count = stream.ConsumeIntegralInRange<uint8_t>(0, 8);
  for (uint8_t i = 0; i < line_count && stream.remaining_bytes() > 0; ++i) {
    switch (stream.ConsumeIntegralInRange<uint8_t>(0, 6)) {
      case 0:
        out += "[" +
               stream.ConsumeBytesAsString(
                   stream.ConsumeIntegralInRange<uint8_t>(0, 8)) +
               "]\n";
        break;
      case 1:
        out += "[m1.]\n";
        break;
      case 2: {

        if (!known_names.empty()) {
          size_t idx = stream.ConsumeIntegralInRange<size_t>(
              0, known_names.size() - 1);
          out += known_names[idx] + " = " +
                 stream.ConsumeBytesAsString(
                     stream.ConsumeIntegralInRange<uint8_t>(0, 8)) +
                 "\n";
        }
        break;
      }
      case 3:

        out += "plug" +
               stream.ConsumeBytesAsString(
                   stream.ConsumeIntegralInRange<uint8_t>(0, 6)) +
               " = 1\n";
        break;
      case 4:

        out += stream.ConsumeBytesAsString(
                   stream.ConsumeIntegralInRange<uint8_t>(0, 16)) +
               "\n";
        break;
      case 5:
        out += "# just a comment\n";
        break;
      default:
        out += "\r\n";
        break;
    }
  }
  out += stream.ConsumeRemainingBytesAsString();
  return out;
}

std::wstring build_wide_content_narrow(const std::string &ascii_skeleton) {
  return std::wstring(ascii_skeleton.begin(), ascii_skeleton.end());
}

std::wstring build_wide_content_fullrange(FuzzedDataProvider &stream,
                                           const std::string &ascii_skeleton) {
  std::wstring out;
  out.reserve(ascii_skeleton.size() + 32);
  for (char c : ascii_skeleton) {
    out.push_back(static_cast<wchar_t>(static_cast<unsigned char>(c)));
  }

  size_t extra = stream.ConsumeIntegralInRange<uint8_t>(0, 32);
  for (size_t i = 0; i < extra && stream.remaining_bytes() >= 4; ++i) {
    uint32_t cp = stream.ConsumeIntegral<uint32_t>();

    cp &= 0x7FFFFFFFu;

    out.push_back(static_cast<wchar_t>(cp));
  }
  out.push_back(L'\n');
  return out;
}

}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  FuzzedDataProvider stream(data, size);

  const bool allow_unregistered = stream.ConsumeBool();
  const bool use_wide = stream.ConsumeBool();
  const bool use_fullrange_wide = stream.ConsumeBool();

  std::vector<std::string> known_names;
  options_description desc = build_description(stream, known_names);

  std::string config_content = build_config_content(stream, known_names);

  if (use_wide) {
    std::wistringstream wiss;
    std::wstring wide_content;
    if (use_fullrange_wide) {

      wide_content = build_wide_content_fullrange(stream, config_content);
    } else {

      wide_content = build_wide_content_narrow(config_content);
    }
    wiss.str(wide_content);

    try {
      basic_parsed_options<wchar_t> parsed =
          parse_config_file<wchar_t>(wiss, desc, allow_unregistered);
      (void)parsed;
    } catch (const boost::program_options::error &) {

    } catch (const std::exception &) {

    } catch (...) {

    }
  } else {
    std::istringstream iss(config_content);

    try {
      basic_parsed_options<char> parsed =
          parse_config_file<char>(iss, desc, allow_unregistered);
      (void)parsed;
    } catch (const boost::program_options::error &) {

    } catch (const std::exception &) {

    } catch (...) {

    }
  }

  return 0;
}