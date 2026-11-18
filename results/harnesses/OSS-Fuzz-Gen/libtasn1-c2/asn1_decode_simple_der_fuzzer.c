#include <config.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "libtasn1.h"
#include "fuzzer.h"

static const unsigned int string_etypes[] = {
  ASN1_ETYPE_GENERALSTRING,
  ASN1_ETYPE_NUMERIC_STRING,
  ASN1_ETYPE_IA5_STRING,
  ASN1_ETYPE_TELETEX_STRING,
  ASN1_ETYPE_PRINTABLE_STRING,
  ASN1_ETYPE_UNIVERSAL_STRING,
  ASN1_ETYPE_BMP_STRING,
  ASN1_ETYPE_UTF8_STRING,
  ASN1_ETYPE_VISIBLE_STRING,
  ASN1_ETYPE_OCTET_STRING
};

static const unsigned char matching_tag_byte[] = {
  ASN1_TAG_GENERALSTRING,
  ASN1_TAG_NUMERIC_STRING,
  ASN1_TAG_IA5_STRING,
  ASN1_TAG_TELETEX_STRING,
  ASN1_TAG_PRINTABLE_STRING,
  ASN1_TAG_UNIVERSAL_STRING,
  ASN1_TAG_BMP_STRING,
  ASN1_TAG_UTF8_STRING,
  ASN1_TAG_VISIBLE_STRING,
  ASN1_TAG_OCTET_STRING
};

static const unsigned int nonstring_valid_etypes[] = {
  ASN1_ETYPE_INTEGER,
  ASN1_ETYPE_BOOLEAN,
  ASN1_ETYPE_SEQUENCE,
  ASN1_ETYPE_NULL,
  ASN1_ETYPE_OBJECT_ID,
  ASN1_ETYPE_BIT_STRING,
  ASN1_ETYPE_ENUMERATED,
  ASN1_ETYPE_SET,
  ASN1_ETYPE_UTC_TIME
};

static const unsigned int invalid_etypes[] = {
  ASN1_ETYPE_INVALID, 1, 2, 8, 9, 10, 13, 16, 17, 18, 19,
  1000u, 0xFFFFFFFFu
};

int
LLVMFuzzerTestOneInput (const uint8_t *data, size_t size)
{
  unsigned int idx;
  unsigned int etype;
  unsigned char mode;
  unsigned char mode2;
  const unsigned char *rest;
  size_t rest_size;
  const unsigned char *der;
  unsigned int der_len;
  unsigned char *buf = NULL;
  const unsigned char *str = NULL;
  unsigned int str_len = 0;
  int ret;
  unsigned char tag_byte = 0;
  int have_matching_tag = 0;
  int etype_category;

  if (size < 2)
    return 0;

  mode = data[1];

  mode2 = (size >= 3) ? data[2] : 0;

  etype_category = (mode >> 6) & 0x3;

  if (etype_category == 2 || etype_category == 3)
    {
      idx = data[0] % (sizeof (invalid_etypes) / sizeof (invalid_etypes[0]));
      etype = invalid_etypes[idx];
      have_matching_tag = 0;
    }
  else if (etype_category == 1)
    {
      idx = data[0] % (sizeof (nonstring_valid_etypes) /
		       sizeof (nonstring_valid_etypes[0]));
      etype = nonstring_valid_etypes[idx];
      have_matching_tag = 0;
    }
  else
    {
      idx = data[0] % (sizeof (string_etypes) / sizeof (string_etypes[0]));
      etype = string_etypes[idx];
      tag_byte = matching_tag_byte[idx];
      have_matching_tag = 1;
    }

  {
    const unsigned char *null_str = NULL;
    unsigned int null_str_len = 0;

    (void) asn1_decode_simple_der (etype, NULL, 10, &null_str,
				    &null_str_len);
  }

  rest = data + 2;
  rest_size = size - 2;

  if ((mode & 1) && have_matching_tag)
    {

      int use_long_form_tag = (mode & 2) != 0;

      if (use_long_form_tag)
	{

	  int use_continuation = (mode & 0x10) != 0;

	  int force_tag_overflow = use_continuation && ((mode2 & 0x1) != 0);

	  int force_post_loop_tag_overflow =
	    use_continuation && ((mode2 & 0x2) != 0) && !force_tag_overflow;

	  int force_truncated_long_tag =
	    use_continuation && ((mode2 & 0x8) != 0) &&
	    !force_tag_overflow && !force_post_loop_tag_overflow;

	  if (force_tag_overflow)
	    {

	      buf = (unsigned char *) malloc (rest_size + 7);
	      if (!buf)
		return 0;
	      buf[0] = 0x1F;
	      memset (buf + 1, 0xFF, 5);
	      buf[6] = tag_byte;
	      if (rest_size > 0)
		memcpy (buf + 7, rest, rest_size);
	      der = buf;
	      der_len = (unsigned int) (rest_size + 7);
	    }
	  else if (force_post_loop_tag_overflow)
	    {

	      buf = (unsigned char *) malloc (rest_size + 6);
	      if (!buf)
		return 0;
	      buf[0] = 0x1F;
	      memset (buf + 1, 0xFF, 4);
	      buf[5] = tag_byte;
	      if (rest_size > 0)
		memcpy (buf + 6, rest, rest_size);
	      der = buf;
	      der_len = (unsigned int) (rest_size + 6);
	    }
	  else if (force_truncated_long_tag)
	    {

	      buf = (unsigned char *) malloc (2);
	      if (!buf)
		return 0;
	      buf[0] = 0x1F;
	      buf[1] = 0x81;
	      der = buf;
	      der_len = 2;
	    }
	  else if (use_continuation)
	    {

	      buf = (unsigned char *) malloc (rest_size + 3);
	      if (!buf)
		return 0;
	      buf[0] = 0x1F;
	      buf[1] = 0x81;
	      buf[2] = tag_byte;
	      if (rest_size > 0)
		memcpy (buf + 3, rest, rest_size);
	      der = buf;
	      der_len = (unsigned int) (rest_size + 3);
	    }
	  else
	    {

	      buf = (unsigned char *) malloc (rest_size + 2);
	      if (!buf)
		return 0;
	      buf[0] = 0x1F;
	      buf[1] = tag_byte;
	      if (rest_size > 0)
		memcpy (buf + 2, rest, rest_size);
	      der = buf;
	      der_len = (unsigned int) (rest_size + 2);
	    }
	}
      else
	{

	  buf = (unsigned char *) malloc (rest_size + 1);
	  if (!buf)
	    return 0;
	  buf[0] = tag_byte;
	  if (rest_size > 0)
	    memcpy (buf + 1, rest, rest_size);
	  der = buf;
	  der_len = (unsigned int) (rest_size + 1);

	  {
	    int length_mode;

	    if ((mode2 & 0x4) != 0)
	      length_mode = 4;
	    else
	      length_mode = (mode >> 2) & 0x3;

	    switch (length_mode)
	      {
	      case 1:
		if (rest_size >= 1)
		  buf[1] = 0x80;
		break;
	      case 2:
		if (rest_size >= 6)
		  {
		    buf[1] = 0x85;
		    memset (buf + 2, 0xFF, 5);
		  }
		break;
	      case 3:
		if (rest_size >= 3)
		  {
		    buf[1] = 0x82;
		    buf[2] = 0xFF;
		    buf[3] = 0xFF;
		  }
		break;
	      case 4:
		if (rest_size >= 3)
		  {

		    buf[1] = 0x85;
		    der_len = 4;
		  }
		break;
	      default:
		break;
	      }
	  }
	}
    }
  else
    {

      der = rest;
      der_len = (unsigned int) rest_size;
    }

  ret = asn1_decode_simple_der (etype, der, der_len, &str, &str_len);
  if (ret == ASN1_SUCCESS)
    {
      assert (str != NULL);

      if (str >= der && (size_t) (str - der) <= der_len)
	{
	  size_t avail = der_len - (size_t) (str - der);
	  size_t to_check = (size_t) str_len < avail ? (size_t) str_len : avail;
	  volatile unsigned char sink = 0;
	  size_t i;

	  for (i = 0; i < to_check; i++)
	    sink ^= str[i];
	  (void) sink;
	}
    }

  free (buf);

  return 0;
}