#include <config.h>

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "idn2.h"
#include "fuzzer.h"

#pragma GCC optimize ("O0")

static const uint32_t interesting_cps[] = {
  0x200C, 0x200D,
  0x064B,
  0x0670,
  0x0644, 0x0627,
  0x00B7,
  0x006C,
  0x0375, 0x03B1,
  0x05F3, 0x05F4, 0x05D0,
  0x0660, 0x0661, 0x06F0, 0x06F1,
  0x30FB, 0x3042, 0x30A2, 0x4E00,
  0x05D1, 0x05D2,
  0x0628,
  0x00DF, 0x03C2,
  0x002D,
  0x0301,
  0x094D,
  0x005F,
  0x0020,
  0x0023,
};

#define NUM_INTERESTING_CPS (sizeof (interesting_cps) / sizeof (interesting_cps[0]))

static size_t
append_utf8 (uint8_t *out, uint32_t cp)
{
  if (cp >= 0xD800 && cp <= 0xDFFF)
    return 0;

  if (cp <= 0x7F)
    {
      out[0] = (uint8_t) cp;
      return 1;
    }
  else if (cp <= 0x7FF)
    {
      out[0] = (uint8_t) (0xC0 | (cp >> 6));
      out[1] = (uint8_t) (0x80 | (cp & 0x3F));
      return 2;
    }
  else if (cp <= 0xFFFF)
    {
      out[0] = (uint8_t) (0xE0 | (cp >> 12));
      out[1] = (uint8_t) (0x80 | ((cp >> 6) & 0x3F));
      out[2] = (uint8_t) (0x80 | (cp & 0x3F));
      return 3;
    }
  else if (cp <= 0x10FFFF)
    {
      out[0] = (uint8_t) (0xF0 | (cp >> 18));
      out[1] = (uint8_t) (0x80 | ((cp >> 12) & 0x3F));
      out[2] = (uint8_t) (0x80 | ((cp >> 6) & 0x3F));
      out[3] = (uint8_t) (0x80 | (cp & 0x3F));
      return 4;
    }
  return 0;
}

static int g_prev_was_curated = 0;

static uint32_t
pick_codepoint (const uint8_t *data, size_t i)
{
  int force_curated = g_prev_was_curated && ((data[i] & 0x03) != 0);

  if (force_curated || (data[i] & 0x0F) == 0)
    {
      g_prev_was_curated = 1;
      return interesting_cps[data[i + 1] % NUM_INTERESTING_CPS];
    }

  g_prev_was_curated = 0;

  uint32_t cp = ((uint32_t) data[i] << 16)
		| ((uint32_t) data[i + 1] << 8)
		| (uint32_t) data[i + 2];
  cp &= 0x1FFFFF;
  if (cp > 0x10FFFF)
    cp = 0x10FFFF;
  return cp;
}

static void
pad_to_boundary (char *buf, size_t *len, size_t cap, uint8_t ctrl)
{
  size_t target;

  switch (ctrl & 0x03)
    {
    case 0:
      target = IDN2_LABEL_MAX_LENGTH - 1;
      break;
    case 1:
      target = IDN2_LABEL_MAX_LENGTH;
      break;
    case 2:
      target = IDN2_LABEL_MAX_LENGTH + 1;
      break;
    default:
      target = IDN2_DOMAIN_MAX_LENGTH + ((ctrl & 0x04) ? 0 : -1);
      break;
    }

  if (target >= cap)
    target = cap - 1;

  if (*len < target)
    {

      while (*len < target)
	buf[(*len)++] = 'a';
    }
  else if (*len > target)
    {
      *len = target;
    }

  buf[*len] = 0;
}

int
LLVMFuzzerTestOneInput (const uint8_t *data, size_t size)
{
  char *domain;
  char *out;
  int flags;
  int use_raw_mode;
  int use_alabel_mode = 0;
  int use_boundary_pad = 0;
  uint8_t boundary_ctrl = 0;

  if (size > 1024)
    return 0;

  if (size > 0)
    {
      uint8_t fb = data[size - 1];
      size -= 1;

      if ((fb & 0x0F) == 0)
	{

	  flags = fb;
	}
      else
	{
	  flags = fb;

	  if ((flags & (IDN2_TRANSITIONAL | IDN2_NONTRANSITIONAL))
	      == (IDN2_TRANSITIONAL | IDN2_NONTRANSITIONAL))
	    flags &= ~IDN2_NONTRANSITIONAL;

	  if ((flags & IDN2_NO_TR46)
	      && (flags & (IDN2_TRANSITIONAL | IDN2_NONTRANSITIONAL)))
	    flags &= ~IDN2_NO_TR46;

	  if ((flags & IDN2_ALABEL_ROUNDTRIP) && (flags & IDN2_NO_ALABEL_ROUNDTRIP))
	    flags &= ~IDN2_NO_ALABEL_ROUNDTRIP;
	}
    }
  else
    {
      flags = 0;
    }

  if (size > 0)
    {
      uint8_t ctrl = data[size - 1];
      use_raw_mode = (ctrl & 0x03) == 0;
      use_alabel_mode = (ctrl & 0x06) == 0x06;
      use_boundary_pad = (ctrl & 0x08) != 0;
      boundary_ctrl = ctrl;
      size -= 1;
    }
  else
    {
      use_raw_mode = 1;
    }

  if (use_alabel_mode && size > 0)
    {

      static const char alnum[] = "abcdefghijklmnopqrstuvwxyz0123456789-";
      size_t n = size > 120 ? 120 : size;
      size_t cap = n + 20;

      domain = (char *) malloc (cap);
      assert (domain != NULL);

      size_t out_len = 0;
      memcpy (domain, "xn--", 4);
      out_len = 4;

      for (size_t i = 0; i < n && out_len + 1 < cap; i++)
	{
	  domain[out_len++] = alnum[data[i] % (sizeof (alnum) - 1)];

	  if ((data[i] & 0x1F) == 0 && out_len + 5 < cap)
	    {
	      domain[out_len++] = '.';
	      if (data[i] & 0x20)
		{
		  memcpy (domain + out_len, "xn--", 4);
		  out_len += 4;
		}
	    }
	}
      domain[out_len] = 0;
      size = out_len;
    }
  else if (use_raw_mode)
    {

      domain = (char *) malloc (size + 1);
      assert (domain != NULL);

      memcpy (domain, data, size);
      domain[size] = 0;
    }
  else
    {

      size_t extra_pad = IDN2_DOMAIN_MAX_LENGTH + 16;
      size_t cap = (size / 3 + 1) * 4 + size + 2 + extra_pad;
      domain = (char *) malloc (cap);
      assert (domain != NULL);

      g_prev_was_curated = 0;

      size_t out_len = 0, i = 0;
      while (i + 3 <= size)
	{
	  uint32_t cp = pick_codepoint (data, i);

	  out_len += append_utf8 ((uint8_t *) domain + out_len, cp);

	  if ((data[i + 2] & 0x1F) == 0 && out_len < cap - 1)
	    domain[out_len++] = '.';

	  i += 3;
	}
      for (; i < size; i++)
	domain[out_len++] = (char) (data[i] & 0x7F);

      domain[out_len] = 0;

      if (use_boundary_pad)
	pad_to_boundary (domain, &out_len, cap, boundary_ctrl);

      size = out_len;
    }

  if (size == 0)
    {
      out = NULL;
      if (idn2_to_ascii_8z (NULL, &out, flags) == IDN2_OK)
	{
	  if (out)
	    idn2_free (out);
	}

      idn2_to_ascii_8z (NULL, NULL, flags);
    }

  out = NULL;
  if (idn2_to_ascii_8z (domain, &out, flags) == IDN2_OK)
    {
      if (out)
	idn2_free (out);
    }

  idn2_to_ascii_8z (domain, NULL, flags);

  free (domain);
  return 0;
}