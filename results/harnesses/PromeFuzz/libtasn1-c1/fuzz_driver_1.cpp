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
#include <cstdlib>
#include <cstring>
#include "libtasn1.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size) {
  if (Size < 2)
    return 0;

  unsigned int etype_selector = Data[0] % 20;
  unsigned int etype = ASN1_ETYPE_OCTET_STRING;

  switch (etype_selector) {
    case 0: etype = ASN1_ETYPE_OCTET_STRING; break;
    case 1: etype = ASN1_ETYPE_GENERALSTRING; break;
    case 2: etype = ASN1_ETYPE_NUMERIC_STRING; break;
    case 3: etype = ASN1_ETYPE_IA5_STRING; break;
    case 4: etype = ASN1_ETYPE_TELETEX_STRING; break;
    case 5: etype = ASN1_ETYPE_PRINTABLE_STRING; break;
    case 6: etype = ASN1_ETYPE_UNIVERSAL_STRING; break;
    case 7: etype = ASN1_ETYPE_BMP_STRING; break;
    case 8: etype = ASN1_ETYPE_UTF8_STRING; break;
    case 9: etype = ASN1_ETYPE_VISIBLE_STRING; break;
    case 10: etype = ASN1_ETYPE_BIT_STRING; break;
    case 11: etype = ASN1_ETYPE_INTEGER; break;
    case 12: etype = ASN1_ETYPE_ENUMERATED; break;
    case 13: etype = ASN1_ETYPE_BOOLEAN; break;
    case 14: etype = ASN1_ETYPE_OBJECT_ID; break;
    case 15: etype = ASN1_ETYPE_GENERALIZED_TIME; break;
    case 16: etype = ASN1_ETYPE_UTC_TIME; break;
    case 17: etype = ASN1_ETYPE_NULL; break;
    case 18: etype = ASN1_ETYPE_SEQUENCE; break;
    case 19: etype = ASN1_ETYPE_SET; break;
    default: etype = ASN1_ETYPE_OCTET_STRING; break;
  }

  const unsigned char *der = Data + 1;
  unsigned int der_len = static_cast<unsigned int>(Size - 1);

  unsigned char *str = nullptr;
  unsigned int str_len = 0;
  unsigned int ber_len = 0;

  int ret = asn1_decode_simple_ber(etype, der, der_len, &str, &str_len, &ber_len);

  if (ret == ASN1_SUCCESS && str != nullptr) {

    if (str_len > 0) {
      volatile unsigned char sink = 0;
      for (unsigned int i = 0; i < str_len; ++i) {
        sink ^= str[i];
      }
      (void)sink;
    }
    free(str);
  }

  if (der_len > 1) {
    unsigned char *str2 = nullptr;
    unsigned int str_len2 = 0;
    unsigned int ber_len2 = 0;
    unsigned int shorter_len = der_len / 2;
    int ret2 = asn1_decode_simple_ber(etype, der, shorter_len, &str2, &str_len2, &ber_len2);
    if (ret2 == ASN1_SUCCESS && str2 != nullptr) {
      free(str2);
    }
  }

  return 0;
}