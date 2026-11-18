#include <boost/regex.hpp>
#include <fuzzer/FuzzedDataProvider.h>
#include <string>
#include <iterator>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size) {
  if (Size < 4)
    return 0;

  FuzzedDataProvider fdp(Data, Size);

  uint8_t compile_sel = fdp.ConsumeIntegral<uint8_t>();
  boost::regex_constants::syntax_option_type compile_flags;
  switch (compile_sel % 8) {
    case 0: compile_flags = boost::regex_constants::normal; break;
    case 1: compile_flags = boost::regex_constants::extended; break;
    case 2: compile_flags = boost::regex_constants::basic; break;
    case 3: compile_flags = boost::regex_constants::awk; break;
    case 4: compile_flags = boost::regex_constants::egrep; break;
    case 5: compile_flags = boost::regex_constants::grep; break;
    case 6: compile_flags = boost::regex_constants::perl; break;
    default: compile_flags = boost::regex_constants::ECMAScript; break;
  }
  if (compile_sel & 0x08)
    compile_flags |= boost::regex_constants::icase;
  if (compile_sel & 0x10)
    compile_flags |= boost::regex_constants::nosubs;
  if (compile_sel & 0x20)
    compile_flags |= boost::regex_constants::collate;

  uint16_t match_sel = fdp.ConsumeIntegral<uint16_t>();
  boost::regex_constants::match_flag_type match_flags =
      boost::regex_constants::match_default;
  if (match_sel & 0x0001)
    match_flags |= boost::regex_constants::match_not_bol;
  if (match_sel & 0x0002)
    match_flags |= boost::regex_constants::match_not_eol;
  if (match_sel & 0x0004)
    match_flags |= boost::regex_constants::match_continuous;
  if (match_sel & 0x0008)
    match_flags |= boost::regex_constants::match_partial;
  if (match_sel & 0x0010)
    match_flags |= boost::regex_constants::match_any;
  if (match_sel & 0x0020)
    match_flags |= boost::regex_constants::match_posix;
  if (match_sel & 0x0040)
    match_flags |= boost::regex_constants::match_nosubs;
  if (match_sel & 0x0080)
    match_flags |= boost::regex_constants::match_single_line;
  if (match_sel & 0x0100)
    match_flags |= boost::regex_constants::format_sed;
  if (match_sel & 0x0200)
    match_flags |= boost::regex_constants::format_all;
  if (match_sel & 0x0400)
    match_flags |= boost::regex_constants::format_no_copy;
  if (match_sel & 0x0800)
    match_flags |= boost::regex_constants::format_first_only;

  uint8_t variant_sel = fdp.ConsumeIntegral<uint8_t>();

  uint8_t pattern_len = fdp.ConsumeIntegral<uint8_t>();
  std::string pattern = fdp.ConsumeBytesAsString(pattern_len);

  uint8_t fmt_len = fdp.ConsumeIntegral<uint8_t>();
  std::string fmt = fdp.ConsumeBytesAsString(fmt_len);

  std::string text = fdp.ConsumeRemainingBytesAsString();

  try {
    boost::regex e(pattern, compile_flags);

    std::string out;
    boost::regex_replace(std::back_inserter(out), text.begin(), text.end(),
                          e, fmt, match_flags);

    try {
      std::string out2;
      boost::regex_replace(std::back_inserter(out2), out.begin(), out.end(),
                            e, fmt, match_flags);
    } catch (const std::exception &) {
    }

    switch (variant_sel % 4) {
      case 0: {

        std::string s = boost::regex_replace(text, e, fmt, match_flags);
        try {
          boost::smatch what;
          if (boost::regex_search(s, what, e, match_flags))
            (void)what.str();
        } catch (const std::exception &) {
        }
        break;
      }
      case 1: {

        std::string out3;
        boost::regex_merge(std::back_inserter(out3), text.begin(),
                            text.end(), e, fmt.c_str(), match_flags);
        try {
          boost::smatch what;
          if (boost::regex_match(out3, what, e,
                                  match_flags | boost::regex_constants::match_partial))
            (void)what.str();
        } catch (const std::exception &) {
        }
        break;
      }
      case 2: {

        boost::smatch what;
        bool found = boost::regex_search(text, what, e, match_flags);
        if (found) {
          (void)what.str();
        }
        break;
      }
      case 3: {

        boost::smatch what;
        bool found = boost::regex_match(text, what, e, match_flags);
        if (found) {
          (void)what.str();
        }
        break;
      }
    }
  } catch (const std::exception &) {
  }

  return 0;
}
