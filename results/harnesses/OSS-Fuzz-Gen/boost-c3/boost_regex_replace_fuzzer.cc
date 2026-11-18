#include <boost/regex.hpp>
#include <fuzzer/FuzzedDataProvider.h>

#include <iterator>
#include <string>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size) {
  FuzzedDataProvider fuzzed_data(Data, Size);

  size_t regex_length = fuzzed_data.ConsumeIntegral<uint8_t>();
  std::string regex_string = fuzzed_data.ConsumeBytesAsString(regex_length);

  size_t fmt_length = fuzzed_data.ConsumeIntegral<uint8_t>();
  std::string fmt_string = fuzzed_data.ConsumeBytesAsString(fmt_length);

  std::string text = fuzzed_data.ConsumeRemainingBytesAsString();

  try {
    boost::regex e(regex_string);

    std::string result;

    boost::regex_replace(std::back_inserter(result), text.begin(), text.end(),
                          e, fmt_string, boost::match_default);
  } catch (const std::runtime_error &) {

  }

  return 0;
}