#include <config.h>

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "idn2.h"
#include "fuzzer.h"

int
LLVMFuzzerTestOneInput (const uint8_t *data, size_t size)
{
  char *domain;
  char *out;

  if (size > 4096)
    return 0;

  domain = (char *) malloc (size + 1);
  assert (domain != NULL);

  memcpy (domain, data, size);
  domain[size] = 0;

  if (size == 0)
    {

      if (idn2_to_unicode_8z8z (NULL, &out, 0) == IDN2_OK)
	idn2_free (out);
      idn2_to_unicode_8z8z (NULL, NULL, 0);
      idn2_to_unicode_8z8z (domain, NULL, 0);
    }

  if (idn2_to_unicode_8z8z (domain, &out, 0) == IDN2_OK)
    idn2_free (out);

  idn2_to_unicode_8z8z (domain, NULL, 0);

  {
    char *ace_domain = (char *) malloc (4 + size + 1);
    assert (ace_domain != NULL);
    memcpy (ace_domain, "xn--", 4);
    memcpy (ace_domain + 4, data, size);
    ace_domain[4 + size] = 0;

    if (idn2_to_unicode_8z8z (ace_domain, &out, 0) == IDN2_OK)
      idn2_free (out);

    free (ace_domain);
  }

  {
    char *multi_ace = (char *) malloc (size + 4 * (size + 1) + 1);
    assert (multi_ace != NULL);
    size_t w = 0, label_start = 0;

    for (size_t i = 0; i <= size; i++)
      {
	if (i == size || data[i] == '.')
	  {
	    memcpy (multi_ace + w, "xn--", 4);
	    w += 4;
	    memcpy (multi_ace + w, data + label_start, i - label_start);
	    w += (i - label_start);
	    if (i < size)
	      multi_ace[w++] = '.';
	    label_start = i + 1;
	  }
      }
    multi_ace[w] = 0;

    if (idn2_to_unicode_8z8z (multi_ace, &out, 0) == IDN2_OK)
      idn2_free (out);

    free (multi_ace);
  }

  {
    enum { LONG_LABEL_SIZE = 300 };
    char long_label[LONG_LABEL_SIZE];
    size_t n = size < (size_t) (LONG_LABEL_SIZE - 1)
                 ? size : (size_t) (LONG_LABEL_SIZE - 1);

    memcpy (long_label, data, n);
    long_label[n] = 0;

    if (idn2_to_unicode_8z8z (long_label, &out, 0) == IDN2_OK)
      idn2_free (out);
  }

  {
    char label_buf[IDN2_LABEL_MAX_LENGTH + 8];
    size_t off = size ? (size_t) (data[0] % 4) : 0;
    size_t len = (IDN2_LABEL_MAX_LENGTH - 1) + off;

    if (len > sizeof (label_buf) - 1)
      len = sizeof (label_buf) - 1;

    for (size_t i = 0; i < len; i++)
      label_buf[i] = size ? (char) ('a' + (data[i % size] % 26)) : 'a';
    label_buf[len] = 0;

    if (idn2_to_unicode_8z8z (label_buf, &out, 0) == IDN2_OK)
      idn2_free (out);
  }

  {
    char domain_buf[320];
    size_t w = 0;
    int labels = 3 + (size ? (int) (data[0] % 3) : 0);

    for (int L = 0; L < labels && w + 61 < sizeof (domain_buf); L++)
      {
	for (int i = 0; i < 60; i++)
	  domain_buf[w++] =
	    size ? (char) ('a' + (data[(L * 60 + i) % size] % 26)) : 'a';
	if (L + 1 < labels)
	  domain_buf[w++] = '.';
      }
    domain_buf[w] = 0;

    if (idn2_to_unicode_8z8z (domain_buf, &out, 0) == IDN2_OK)
      idn2_free (out);
  }

  if (size > 0)
    {
      uint32_t small_u32[IDN2_LABEL_MAX_LENGTH];
      size_t small_len = sizeof (small_u32) / sizeof (small_u32[0]);

      idn2_punycode_decode ((const char *) data, size, small_u32, &small_len);
    }

  if (size >= 4)
    {
      uint32_t cps[16];
      size_t ncps = 0;

      for (size_t i = 0; i + 1 < size && ncps < 16; i += 2)
	{
	  uint16_t v = (uint16_t) (data[i] | (data[i + 1] << 8));
	  uint32_t cp;

	  switch (v & 0x3)
	    {
	    case 0:
	      cp = v % 0x80;
	      break;
	    case 1:
	      cp = 0x80 + (v % (0x800 - 0x80));
	      break;
	    case 2:
	      cp = 0x800 + (v % (0x10000 - 0x800));
	      if (cp >= 0xD800 && cp <= 0xDFFF)
		cp = 0xE000;
	      break;
	    default:
	      cp = 0x10000 + (v % (0x110000 - 0x10000));
	      break;
	    }
	  cps[ncps++] = cp;
	}

      if (ncps > 0)
	{
	  char punybuf[256];
	  size_t punylen = sizeof (punybuf) - 1;

	  if (idn2_punycode_encode (cps, ncps, punybuf, &punylen) == IDN2_OK)
	    {
	      char label[4 + sizeof (punybuf)];
	      size_t llen = 4 + punylen;

	      memcpy (label, "xn--", 4);
	      memcpy (label + 4, punybuf, punylen);
	      label[llen] = 0;

	      if (idn2_to_unicode_8z8z (label, &out, 0) == IDN2_OK)
		idn2_free (out);

	      {
		char *out_l2 = NULL;

		if (idn2_to_unicode_8zlz (label, &out_l2, 0) == IDN2_OK
		    && out_l2)
		  idn2_free (out_l2);

		out_l2 = NULL;
		if (idn2_to_unicode_lzlz (label, &out_l2, 0) == IDN2_OK
		    && out_l2)
		  idn2_free (out_l2);
	      }

	      enum { REPEATS = 6 };
	      char domain_multi[REPEATS * (sizeof (label) + 1) + 1];
	      size_t w = 0;

	      for (int r = 0; r < REPEATS; r++)
		{
		  memcpy (domain_multi + w, label, llen);
		  w += llen;
		  domain_multi[w++] = '.';
		}
	      domain_multi[w ? w - 1 : 0] = 0;

	      if (idn2_to_unicode_8z8z (domain_multi, &out, 0) == IDN2_OK)
		idn2_free (out);
	    }
	}
    }

  {
    uint32_t *out_u32 = NULL;

    idn2_to_unicode_8z4z (domain, NULL, 0);

    if (idn2_to_unicode_8z4z (domain, &out_u32, 0) == IDN2_OK && out_u32)
      free (out_u32);

    idn2_to_unicode_8z4z (NULL, NULL, 0);

    out_u32 = NULL;
    if (idn2_to_unicode_8z4z (NULL, &out_u32, 0) == IDN2_OK && out_u32)
      free (out_u32);
  }

  {
    uint32_t u32buf[64];
    size_t u32len = 0;

    for (size_t i = 0; i < size && u32len < 63; i++)
      u32buf[u32len++] = (uint32_t) data[i];
    u32buf[u32len] = 0;

    uint32_t *out4 = NULL;
    if (idn2_to_unicode_4z4z (u32buf, &out4, 0) == IDN2_OK && out4)
      free (out4);
    idn2_to_unicode_4z4z (u32buf, NULL, 0);
    idn2_to_unicode_4z4z (NULL, NULL, 0);
    out4 = NULL;
    if (idn2_to_unicode_4z4z (NULL, &out4, 0) == IDN2_OK && out4)
      free (out4);

    uint32_t out44[128];
    size_t out44len = sizeof (out44) / sizeof (out44[0]);
    idn2_to_unicode_44i (u32buf, u32len, out44, &out44len, 0);
    out44len = 0;
    idn2_to_unicode_44i (u32buf, u32len, NULL, &out44len, 0);
    out44len = sizeof (out44) / sizeof (out44[0]);
    idn2_to_unicode_44i (NULL, 0, out44, &out44len, 0);
  }

  {
    char *out_l = NULL;

    if (idn2_to_unicode_8zlz (domain, &out_l, 0) == IDN2_OK && out_l)
      idn2_free (out_l);
    idn2_to_unicode_8zlz (domain, NULL, 0);
    idn2_to_unicode_8zlz (NULL, NULL, 0);
    out_l = NULL;
    if (idn2_to_unicode_8zlz (NULL, &out_l, 0) == IDN2_OK && out_l)
      idn2_free (out_l);

    out_l = NULL;
    if (idn2_to_unicode_lzlz (domain, &out_l, 0) == IDN2_OK && out_l)
      idn2_free (out_l);
    idn2_to_unicode_lzlz (domain, NULL, 0);
    idn2_to_unicode_lzlz (NULL, NULL, 0);
  }

  {
    char overflow_label[4 + 32 + 1];
    size_t k = size ? (size_t) (data[0] % 24) + 8 : 8;

    memcpy (overflow_label, "xn--", 4);
    for (size_t i = 0; i < k; i++)
      overflow_label[4 + i] = (i & 1) ? '9' : 'z';
    overflow_label[4 + k] = 0;

    if (idn2_to_unicode_8z8z (overflow_label, &out, 0) == IDN2_OK)
      idn2_free (out);
  }

  {
    enum { BASIC_RUN = IDN2_LABEL_MAX_LENGTH + 8 };
    char big_basic[4 + BASIC_RUN + 2 + 1];

    memcpy (big_basic, "xn--", 4);
    for (int i = 0; i < BASIC_RUN; i++)
      big_basic[4 + i] = (char) ('a' + (i % 26));
    big_basic[4 + BASIC_RUN] = '-';
    big_basic[4 + BASIC_RUN + 1] = '0';
    big_basic[4 + BASIC_RUN + 2] = 0;

    if (idn2_to_unicode_8z8z (big_basic, &out, 0) == IDN2_OK)
      idn2_free (out);
  }

  {
    static const unsigned char bad_utf8_vectors[][4] = {
      { 0xC0, 0x80, 0, 0 },
      { 0xC1, 0xBF, 0, 0 },
      { 0xED, 0xA0, 0x80, 0 },
      { 0xF4, 0x90, 0x80, 0x80 },
      { 0xE0, 0x80, 0x80, 0 },
      { 0xC2, 0, 0, 0 },
      { 0xE1, 0x80, 0, 0 },
      { 0xF1, 0x80, 0x80, 0 },
    };
    for (size_t v = 0;
	 v < sizeof (bad_utf8_vectors) / sizeof (bad_utf8_vectors[0]); v++)
      {
	char lbl[8];
	size_t n = 0;

	for (; n < 4 && bad_utf8_vectors[v][n]; n++)
	  lbl[n] = (char) bad_utf8_vectors[v][n];
	lbl[n] = 0;

	if (idn2_to_unicode_8z8z (lbl, &out, 0) == IDN2_OK)
	  idn2_free (out);
      }
  }

  free (domain);
  return 0;
}