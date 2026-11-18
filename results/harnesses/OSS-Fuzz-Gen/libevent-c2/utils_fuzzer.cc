#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <fuzzer/FuzzedDataProvider.h>
#include <string>
#include <vector>

extern "C" {
#include "event2/util.h"
}

static const char *kTemplates[] = {

    "[ffff::1]:1000",
    "[ffff::1]",
    "[ffff::1",
    "[ffff::1]:65599",
    "[ffff::1]:0",
    "[ffff::1]:-1",
    "::1",
    "1:2::1",
    "f::1%lo",
    "f::1%9999",
    "192.168.0.1:50",
    "1.2.3.4",
    "256.1.1.1",
    "[::1]:80",
    "2001:db8::1",
    "::ffff:192.168.1.1",

    "1.2.3.4.5",
    "ABC",
    "176.192.208.244",

    "[foobar]",
    "[fo:obar]",
    "[ffff]",
    "[fffff::]",
    "[1:2:33333:4::]",
    "[1:2:3:4:5:6:7:8:9]",
    "[1::2::3]",
    "[:::1]",
    "[::1.0.1.1000]",
    "[1:2:3:4:5:6:7:8]",
    "[0:0:0:0:0:0:0:0]",
    "[::9:c0a8:1:1]",

    "2001:DB8::%0",
    "2001:DB8::%1",
    "2001:DB8::%does-not-exist",
    "[f::1%lo]:1234",
    "[f::1%does-not-exist]:1234",

    "[.1.2.3.4]",
    ".1:2:3",

    "[1:2:3:4:5:6:7:1.2.3.4]",
    "1:2:3:4:5:6:7:1.2.3.4",

    "1.256.1.1",
    "1.1.256.1",
    "1.1.1.256",

    "[::300.1.1.1]",
    "[::1.300.1.1]",
    "[::1.1.300.1]",

    "[::1.2.3]",
    "[::1.2.3.]",

    "[:1:2:3:4:5:6:7]",
    ":1:2:3:4:5:6:7:8",

    "[1:2:3:4:5:6:7]",

    "[1:2:3:4:5:6:7::]",

    "f::1%123abc",
    "2001:DB8::%42xyz",

    "%lo",
    "%0",
    "%doesnotexist",
    "[%lo]",
    "[%0]",

    "f::1%99999999999999999999",
    "2001:DB8::%18446744073709551616",

    "1.2.3.4:0",
    "1.2.3.4:-1",
    "1.2.3.4:65599",
};

static std::string BuildOverlongAddress(FuzzedDataProvider *stream,
                                         bool bracketed) {

  std::string s;
  if (bracketed) s.push_back('[');
  size_t len = 150 + (stream->ConsumeIntegral<uint8_t>() % 50);
  for (size_t i = 0; i < len; ++i) {
    s.push_back("0123456789abcdef:."[stream->ConsumeIntegral<uint8_t>() % 18]);
  }
  if (bracketed) {
    s.push_back(']');
    if (stream->ConsumeBool()) {
      s.push_back(':');
      s += std::to_string(stream->ConsumeIntegralInRange<int>(-5, 70000));
    }
  }
  return s;
}

static std::string BuildOverlongIPv4Port(FuzzedDataProvider *stream) {
  std::string s;
  size_t len = 150 + (stream->ConsumeIntegral<uint8_t>() % 50);
  for (size_t i = 0; i < len; ++i) {

    s.push_back("0123456789abcdef."[stream->ConsumeIntegral<uint8_t>() % 17]);
  }
  s.push_back(':');
  s += std::to_string(stream->ConsumeIntegralInRange<int>(-5, 70000));
  return s;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  FuzzedDataProvider stream(data, size);

  uint8_t outlen_choice = stream.ConsumeIntegralInRange<uint8_t>(0, 3);

  uint8_t mode = stream.ConsumeIntegralInRange<uint8_t>(0, 4);

  std::string input_str;
  switch (mode) {
    case 0: {
      input_str = stream.ConsumeRemainingBytesAsString();
      break;
    }
    case 1: {
      size_t idx = stream.ConsumeIntegralInRange<size_t>(
          0, (sizeof(kTemplates) / sizeof(kTemplates[0])) - 1);
      input_str = kTemplates[idx];
      break;
    }
    case 2: {
      size_t idx = stream.ConsumeIntegralInRange<size_t>(
          0, (sizeof(kTemplates) / sizeof(kTemplates[0])) - 1);
      std::string prefix = stream.ConsumeBool()
                                ? stream.ConsumeBytesAsString(
                                      stream.ConsumeIntegralInRange<size_t>(
                                          0, 8))
                                : "";
      std::string suffix = stream.ConsumeRemainingBytesAsString();
      input_str = prefix + kTemplates[idx] + suffix;
      break;
    }
    case 3: {
      bool bracketed = stream.ConsumeBool();
      input_str = BuildOverlongAddress(&stream, bracketed);
      break;
    }
    default: {
      input_str = BuildOverlongIPv4Port(&stream);
      break;
    }
  }

  struct sockaddr_storage out_addr;
  memset(&out_addr, 0, sizeof(out_addr));

  int outlen;
  switch (outlen_choice) {
    case 0:
      outlen = 0;
      break;
    case 1:
      outlen = sizeof(struct sockaddr_in) - 1;
      break;
    case 2:
      outlen = sizeof(struct sockaddr_in6) - 1;
      break;
    default:
      outlen = sizeof(out_addr);
      break;
  }

  evutil_parse_sockaddr_port(input_str.c_str(),
                              reinterpret_cast<struct sockaddr *>(&out_addr),
                              &outlen);

  return 0;
}