#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <cstdint>
#include <cstddef>
#include <libtasn1.h>
#include <cstdint>
#include <cstddef>
#include <cstring>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size) {
  if (Size < 1)
    return 0;

  static const unsigned int etypes[] = {
    ASN1_ETYPE_BOOLEAN,
    ASN1_ETYPE_INTEGER,
    ASN1_ETYPE_OCTET_STRING,
    ASN1_ETYPE_GENERALSTRING,
    ASN1_ETYPE_NULL,
    ASN1_ETYPE_OBJECT_ID,
    ASN1_ETYPE_ENUMERATED,
    ASN1_ETYPE_GENERALIZED_TIME,
    ASN1_ETYPE_UTC_TIME,
    ASN1_ETYPE_BIT_STRING
  };

  size_t num_etypes = sizeof(etypes) / sizeof(etypes[0]);
  unsigned int etype = etypes[Data[0] % num_etypes];

  const uint8_t *der_data = Data + 1;
  size_t der_len = Size - 1;

  const unsigned char *str = nullptr;
  unsigned int str_len = 0;

  int result = asn1_decode_simple_der(etype, der_data,
                                       static_cast<unsigned int>(der_len),
                                       &str, &str_len);

  if (result == ASN1_SUCCESS) {

    if (str != nullptr && str_len > 0) {

      volatile unsigned char sum = 0;
      for (unsigned int i = 0; i < str_len; i++) {
        sum += str[i];
      }
      (void)sum;
    }
  }

  if (der_len > 0) {
    unsigned int etype2 = etypes[(Data[0] + 1) % num_etypes];
    const unsigned char *str2 = nullptr;
    unsigned int str_len2 = 0;
    int result2 = asn1_decode_simple_der(etype2, der_data,
                                          static_cast<unsigned int>(der_len),
                                          &str2, &str_len2);
    if (result2 == ASN1_SUCCESS && str2 != nullptr && str_len2 > 0) {
      volatile unsigned char sum2 = 0;
      for (unsigned int i = 0; i < str_len2; i++) {
        sum2 += str2[i];
      }
      (void)sum2;
    }
  }

  return 0;
}