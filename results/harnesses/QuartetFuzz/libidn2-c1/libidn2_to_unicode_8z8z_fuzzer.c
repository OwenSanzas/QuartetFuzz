#include <config.h>

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "idn2.h"

static const int flag_table[] = {
  0,
  IDN2_NFC_INPUT,
  IDN2_TRANSITIONAL,
  IDN2_NONTRANSITIONAL,
  IDN2_TRANSITIONAL | IDN2_USE_STD3_ASCII_RULES,
  IDN2_NONTRANSITIONAL | IDN2_USE_STD3_ASCII_RULES,
  IDN2_ALABEL_ROUNDTRIP,
  IDN2_NO_ALABEL_ROUNDTRIP,
  IDN2_NO_TR46,
  IDN2_ALLOW_UNASSIGNED,
};

#define MAX_INPUT 4096

#ifdef __cplusplus
extern "C"
#endif
int
LLVMFuzzerTestOneInput (const uint8_t *data, size_t size)
{
  if (size > MAX_INPUT)
    return 0;

  {
    char *out = NULL;
    if (idn2_to_unicode_8z8z (NULL, &out, 0) == IDN2_OK)
      idn2_free (out);
    idn2_to_unicode_8z8z (NULL, NULL, 0);
  }

  if (size == 0)
    return 0;

  int flags =
    flag_table[data[0] % (sizeof (flag_table) / sizeof (flag_table[0]))];
  size_t len = size - 1;
  const uint8_t *body = data + 1;

  char *domain = (char *) malloc (len + 1);
  if (!domain)
    return 0;
  memcpy (domain, body, len);
  domain[len] = '\0';

  char *decoded = NULL;
  int rc = idn2_to_unicode_8z8z (domain, &decoded, flags);

  idn2_to_unicode_8z8z (domain, NULL, flags);

  {
    uint32_t *u32out = NULL;
    if (idn2_to_unicode_8z4z (domain, &u32out, flags) == IDN2_OK)
      free (u32out);
    idn2_to_unicode_8z4z (domain, NULL, flags);
  }

  {
    char *lout = NULL;
    if (idn2_to_unicode_8zlz (domain, &lout, flags) == IDN2_OK)
      idn2_free (lout);
    idn2_to_unicode_8zlz (domain, NULL, flags);
  }

  {
    char *lout2 = NULL;
    if (idn2_to_unicode_lzlz (domain, &lout2, flags) == IDN2_OK)
      idn2_free (lout2);
  }

  if (rc == IDN2_OK && decoded)
    {

      char *ace = NULL;
      if (idn2_to_ascii_8z (decoded, &ace, flags) == IDN2_OK)
        {
          char *redecoded = NULL;
          if (idn2_to_unicode_8z8z (ace, &redecoded, flags) == IDN2_OK)
            idn2_free (redecoded);
          idn2_free (ace);
        }
      idn2_free (decoded);
    }

  if (len >= 4)
    {
      size_t n = len / 4;
      uint32_t *u32in = (uint32_t *) malloc ((n + 1) * sizeof (uint32_t));
      if (u32in)
        {
          memcpy (u32in, body, n * 4);
          u32in[n] = 0;

          uint32_t *u32out2 = NULL;
          if (idn2_to_unicode_4z4z (u32in, &u32out2, flags) == IDN2_OK)
            free (u32out2);

          uint32_t outbuf[300];
          size_t outlen = sizeof (outbuf) / sizeof (outbuf[0]);
          idn2_to_unicode_44i (u32in, n, outbuf, &outlen, flags);

          free (u32in);
        }
    }

  free (domain);
  return 0;
}
