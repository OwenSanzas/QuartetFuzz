#include <config.h>
#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <limits.h>
#include <stdint.h>
#include <vector>

#include <fuzzer/FuzzedDataProvider.h>

#include "libtasn1.h"
#include "fuzzer.h"

static const unsigned int kStringEtypes[] = {
  ASN1_ETYPE_GENERALSTRING,
  ASN1_ETYPE_NUMERIC_STRING,
  ASN1_ETYPE_IA5_STRING,
  ASN1_ETYPE_TELETEX_STRING,
  ASN1_ETYPE_PRINTABLE_STRING,
  ASN1_ETYPE_UNIVERSAL_STRING,
  ASN1_ETYPE_BMP_STRING,
  ASN1_ETYPE_UTF8_STRING,
  ASN1_ETYPE_VISIBLE_STRING,
  ASN1_ETYPE_OCTET_STRING,
};
#define N_STRING_ETYPES (sizeof (kStringEtypes) / sizeof (kStringEtypes[0]))

static const unsigned int kStructuredEtypes[] = {
  ASN1_ETYPE_SEQUENCE,
  ASN1_ETYPE_SEQUENCE_OF,
  ASN1_ETYPE_SET,
  ASN1_ETYPE_SET_OF,
};
#define N_STRUCTURED_ETYPES (sizeof (kStructuredEtypes) / sizeof (kStructuredEtypes[0]))

static const unsigned int kNonStringEtypes[] = {
  ASN1_ETYPE_BOOLEAN,
  ASN1_ETYPE_INTEGER,
  ASN1_ETYPE_NULL,
  ASN1_ETYPE_ENUMERATED,
  ASN1_ETYPE_OBJECT_ID,
  ASN1_ETYPE_BIT_STRING,
  ASN1_ETYPE_UTC_TIME,
  ASN1_ETYPE_GENERALIZED_TIME,
};
#define N_NONSTRING_ETYPES (sizeof (kNonStringEtypes) / sizeof (kNonStringEtypes[0]))

static const unsigned int kBoundaryEtypes[] = {
  ASN1_ETYPE_INVALID,
  1, 2,
  8, 9, 10,
  13,
  16,
  18, 19,
  26,
  ASN1_ETYPE_GENERALIZED_TIME,
  38, 39,
  100,
  0xFFFFFFFFu
};
#define N_BOUNDARY_ETYPES (sizeof (kBoundaryEtypes) / sizeof (kBoundaryEtypes[0]))

static unsigned char
tag_for_etype (unsigned int etype)
{
  switch (etype)
    {
    case ASN1_ETYPE_GENERALSTRING:
      return ASN1_TAG_GENERALSTRING;
    case ASN1_ETYPE_NUMERIC_STRING:
      return ASN1_TAG_NUMERIC_STRING;
    case ASN1_ETYPE_IA5_STRING:
      return ASN1_TAG_IA5_STRING;
    case ASN1_ETYPE_TELETEX_STRING:
      return ASN1_TAG_TELETEX_STRING;
    case ASN1_ETYPE_PRINTABLE_STRING:
      return ASN1_TAG_PRINTABLE_STRING;
    case ASN1_ETYPE_UNIVERSAL_STRING:
      return ASN1_TAG_UNIVERSAL_STRING;
    case ASN1_ETYPE_BMP_STRING:
      return ASN1_TAG_BMP_STRING;
    case ASN1_ETYPE_UTF8_STRING:
      return ASN1_TAG_UTF8_STRING;
    case ASN1_ETYPE_VISIBLE_STRING:
      return ASN1_TAG_VISIBLE_STRING;
    case ASN1_ETYPE_OCTET_STRING:
      return ASN1_TAG_OCTET_STRING;
    case ASN1_ETYPE_BIT_STRING:
      return ASN1_TAG_BIT_STRING;
    case ASN1_ETYPE_BOOLEAN:
      return ASN1_TAG_BOOLEAN;
    case ASN1_ETYPE_INTEGER:
      return ASN1_TAG_INTEGER;
    case ASN1_ETYPE_NULL:
      return ASN1_TAG_NULL;
    case ASN1_ETYPE_ENUMERATED:
      return ASN1_TAG_ENUMERATED;
    case ASN1_ETYPE_OBJECT_ID:
      return ASN1_TAG_OBJECT_ID;
    case ASN1_ETYPE_SEQUENCE:
    case ASN1_ETYPE_SEQUENCE_OF:
      return ASN1_TAG_SEQUENCE;
    case ASN1_ETYPE_SET:
    case ASN1_ETYPE_SET_OF:
      return ASN1_TAG_SET;
    case ASN1_ETYPE_UTC_TIME:
      return ASN1_TAG_UTCTime;
    case ASN1_ETYPE_GENERALIZED_TIME:
      return ASN1_TAG_GENERALIZEDTime;
    default:
      return 0x00;
    }
}

static void
append_definite_length (std::vector<unsigned char> &out, size_t len)
{
  if (len < 128)
    {
      out.push_back ((unsigned char) len);
    }
  else if (len < 256)
    {
      out.push_back (0x81);
      out.push_back ((unsigned char) len);
    }
  else if (len < 65536)
    {
      out.push_back (0x82);
      out.push_back ((unsigned char) (len >> 8));
      out.push_back ((unsigned char) (len & 0xff));
    }
  else
    {
      out.push_back (0x83);
      out.push_back ((unsigned char) (len >> 16));
      out.push_back ((unsigned char) (len >> 8));
      out.push_back ((unsigned char) (len & 0xff));
    }
}

static std::vector<unsigned char>
build_nested_ber (unsigned int etype, const std::vector<unsigned char> &content,
		   int nest_levels, bool indefinite, bool drop_eoc)
{
  unsigned char tag = tag_for_etype (etype);
  std::vector<unsigned char> buf;

  buf.push_back (tag);
  append_definite_length (buf, content.size ());
  buf.insert (buf.end (), content.begin (), content.end ());

  for (int i = 0; i < nest_levels; i++)
    {
      std::vector<unsigned char> wrapped;
      wrapped.push_back ((unsigned char) (tag | 0x20));

      if (indefinite)
	{
	  wrapped.push_back (0x80);
	  wrapped.insert (wrapped.end (), buf.begin (), buf.end ());
	  if (!drop_eoc)
	    {
	      wrapped.push_back (0x00);
	      wrapped.push_back (0x00);
	    }
	}
      else
	{
	  append_definite_length (wrapped, buf.size ());
	  wrapped.insert (wrapped.end (), buf.begin (), buf.end ());
	}

      buf = wrapped;
    }

  return buf;
}

static std::vector<unsigned char>
build_nested_ber_mixed (unsigned int etype,
			const std::vector<unsigned char> &content,
			int nest_levels, unsigned int indefinite_mask,
			bool drop_eoc)
{
  unsigned char tag = tag_for_etype (etype);
  std::vector<unsigned char> buf;

  buf.push_back (tag);
  append_definite_length (buf, content.size ());
  buf.insert (buf.end (), content.begin (), content.end ());

  for (int i = 0; i < nest_levels; i++)
    {
      bool indefinite = (indefinite_mask >> i) & 1;
      std::vector<unsigned char> wrapped;
      wrapped.push_back ((unsigned char) (tag | 0x20));

      if (indefinite)
	{
	  wrapped.push_back (0x80);
	  wrapped.insert (wrapped.end (), buf.begin (), buf.end ());
	  if (!drop_eoc)
	    {
	      wrapped.push_back (0x00);
	      wrapped.push_back (0x00);
	    }
	}
      else
	{
	  append_definite_length (wrapped, buf.size ());
	  wrapped.insert (wrapped.end (), buf.begin (), buf.end ());
	}

      buf = wrapped;
    }

  return buf;
}

static std::vector<unsigned char>
build_multi_segment_wrapper (unsigned int etype,
			     const std::vector<std::vector<unsigned char>> &segments,
			     bool indefinite, bool drop_eoc, bool corrupt_eoc)
{
  unsigned char tag = tag_for_etype (etype);
  std::vector<unsigned char> inner;

  for (size_t i = 0; i < segments.size (); i++)
    {
      inner.push_back (tag);
      append_definite_length (inner, segments[i].size ());
      inner.insert (inner.end (), segments[i].begin (), segments[i].end ());
    }

  std::vector<unsigned char> wrapped;
  wrapped.push_back ((unsigned char) (tag | 0x20));
  if (indefinite)
    {
      wrapped.push_back (0x80);
      wrapped.insert (wrapped.end (), inner.begin (), inner.end ());
      if (!drop_eoc)
	{
	  wrapped.push_back (0x00);
	  wrapped.push_back (0x00);
	}
      else if (corrupt_eoc)
	{

	  wrapped.push_back (0xAA);
	  wrapped.push_back (0xBB);
	}
    }
  else
    {
      append_definite_length (wrapped, inner.size ());
      wrapped.insert (wrapped.end (), inner.begin (), inner.end ());
    }

  return wrapped;
}

static void
try_decode (unsigned int etype, const std::vector<unsigned char> &buf,
	    bool use_ber_len)
{
  if (buf.empty () || buf.size () > (size_t) INT_MAX)
    return;

  unsigned char *str = NULL;
  unsigned int str_len = 0;
  unsigned int ber_len = 0;
  int ret = asn1_decode_simple_ber (etype, buf.data (),
				    (unsigned int) buf.size (),
				    &str, &str_len,
				    use_ber_len ? &ber_len : NULL);
  if (ret == ASN1_SUCCESS)
    {
      assert (str != NULL || str_len == 0);
      if (use_ber_len)
	assert (ber_len <= buf.size ());
      free (str);
    }
}

int
LLVMFuzzerTestOneInput (const uint8_t *data, size_t size)
{
  if (size == 0 || size > (size_t) INT_MAX)
    return 0;

  FuzzedDataProvider fdp (data, size);

  {
    unsigned int der_len = (unsigned int) size;
    unsigned char *str;
    unsigned int str_len;
    unsigned int ber_len;

    static const unsigned int raw_etypes[] = {
      ASN1_ETYPE_OCTET_STRING, ASN1_ETYPE_IA5_STRING,
      ASN1_ETYPE_UTF8_STRING, ASN1_ETYPE_GENERALSTRING,
    };

    for (size_t i = 0; i < sizeof (raw_etypes) / sizeof (raw_etypes[0]); i++)
      {
	str = NULL;
	str_len = 0;
	ber_len = 0;
	int ret = asn1_decode_simple_ber (raw_etypes[i], data, der_len,
					  &str, &str_len, &ber_len);
	if (ret == ASN1_SUCCESS)
	  {
	    assert (str != NULL || str_len == 0);
	    assert (ber_len <= der_len);
	    free (str);
	  }
      }

    str = NULL;
    str_len = 0;
    int ret = asn1_decode_simple_ber (ASN1_ETYPE_OCTET_STRING, data, der_len,
				      &str, &str_len, NULL);
    if (ret == ASN1_SUCCESS)
      {
	assert (str != NULL || str_len == 0);
	free (str);
      }

    for (size_t i = 0; i < N_BOUNDARY_ETYPES; i++)
      {
	str = NULL;
	str_len = 0;
	ber_len = 0;
	ret = asn1_decode_simple_ber (kBoundaryEtypes[i], data, der_len,
				      &str, &str_len, &ber_len);
	if (ret == ASN1_SUCCESS)
	  {
	    assert (str != NULL || str_len == 0);
	    assert (ber_len <= der_len);
	    free (str);
	  }
      }
  }

  {
    std::vector<unsigned char> raw = fdp.ConsumeBytes<unsigned char> (
	fdp.ConsumeIntegralInRange<size_t> (1, 32));
    if (raw.empty ())
      raw.push_back (0xAA);

    for (size_t i = 0; i < N_STRUCTURED_ETYPES; i++)
      try_decode (kStructuredEtypes[i], raw, true);
  }

  {
    unsigned int idx = fdp.ConsumeIntegralInRange<unsigned int> (
	0, (unsigned int) N_NONSTRING_ETYPES - 1);
    unsigned int etype = kNonStringEtypes[idx];
    std::vector<unsigned char> content = fdp.ConsumeBytes<unsigned char> (
	fdp.ConsumeIntegralInRange<size_t> (0, 16));
    std::vector<unsigned char> buf = build_nested_ber (etype, content, 0,
						       false, false);
    try_decode (etype, buf, true);
    try_decode (etype, buf, false);

    std::vector<unsigned char> constructed_buf =
      build_nested_ber (etype, content, 1, false, false);
    try_decode (etype, constructed_buf, true);

    std::vector<unsigned char> constructed_indef_buf =
      build_nested_ber (etype, content, 1, true, false);
    try_decode (etype, constructed_indef_buf, true);
  }

  {
    unsigned int idx = fdp.ConsumeIntegralInRange<unsigned int> (
	0, (unsigned int) N_STRING_ETYPES - 1);
    unsigned int etype = kStringEtypes[idx];

    bool indefinite = fdp.ConsumeBool ();
    bool drop_eoc = fdp.ConsumeBool ();
    bool use_ber_len = fdp.ConsumeBool ();

    int nest_levels = fdp.ConsumeIntegralInRange<int> (0, 5);

    std::vector<unsigned char> content =
      fdp.ConsumeBytes<unsigned char> (
	  fdp.ConsumeIntegralInRange<size_t> (0, 32));

    std::vector<unsigned char> buf = build_nested_ber (etype, content,
						       nest_levels, indefinite,
						       drop_eoc);
    try_decode (etype, buf, use_ber_len);

    unsigned int other_etype = kStringEtypes[(idx + 1) % N_STRING_ETYPES];
    try_decode (other_etype, buf, use_ber_len);

    unsigned int indefinite_mask = fdp.ConsumeIntegral<unsigned int> ();
    int mixed_levels = fdp.ConsumeIntegralInRange<int> (0, 3);
    std::vector<unsigned char> mixed_buf =
      build_nested_ber_mixed (etype, content, mixed_levels, indefinite_mask,
			      drop_eoc);
    try_decode (etype, mixed_buf, use_ber_len);
  }

  {
    unsigned int idx = fdp.ConsumeIntegralInRange<unsigned int> (
	0, (unsigned int) N_STRING_ETYPES - 1);
    unsigned int etype = kStringEtypes[idx];

    unsigned int n_segments = fdp.ConsumeIntegralInRange<unsigned int> (2, 4);
    std::vector<std::vector<unsigned char>> segments;
    for (unsigned int i = 0; i < n_segments; i++)
      {
	size_t seg_len = fdp.ConsumeIntegralInRange<size_t> (0, 8);
	segments.push_back (fdp.ConsumeBytes<unsigned char> (seg_len));
      }

    bool indefinite = fdp.ConsumeBool ();
    bool drop_eoc = fdp.ConsumeBool ();
    bool corrupt_eoc = fdp.ConsumeBool ();
    bool use_ber_len = fdp.ConsumeBool ();

    std::vector<unsigned char> buf =
      build_multi_segment_wrapper (etype, segments, indefinite, drop_eoc,
				   corrupt_eoc);
    try_decode (etype, buf, use_ber_len);

    std::vector<unsigned char> outer;
    unsigned char tag = tag_for_etype (etype);
    outer.push_back ((unsigned char) (tag | 0x20));
    append_definite_length (outer, buf.size ());
    outer.insert (outer.end (), buf.begin (), buf.end ());
    try_decode (etype, outer, use_ber_len);
  }

  {
    unsigned int idx = fdp.ConsumeIntegralInRange<unsigned int> (
	0, (unsigned int) N_STRING_ETYPES - 1);
    unsigned int etype = kStringEtypes[idx];
    unsigned char tag = tag_for_etype (etype);

    {
      std::vector<unsigned char> buf_a;
      buf_a.push_back (tag);
      buf_a.push_back (0x80);
      std::vector<unsigned char> extra =
	fdp.ConsumeBytes<unsigned char> (fdp.ConsumeIntegralInRange<size_t> (0, 8));
      buf_a.insert (buf_a.end (), extra.begin (), extra.end ());
      try_decode (etype, buf_a, true);
      try_decode (etype, buf_a, false);
    }

    {
      std::vector<unsigned char> buf_b;
      buf_b.push_back ((unsigned char) (tag | 0x20));
      buf_b.push_back (0x84);
      buf_b.push_back (0xFF);
      buf_b.push_back (0xFF);
      buf_b.push_back (0xFF);
      buf_b.push_back (0xFF);
      try_decode (etype, buf_b, true);
    }

    {
      std::vector<unsigned char> buf_c;
      buf_c.push_back ((unsigned char) (tag | 0x20));
      buf_c.push_back (0x82);
      buf_c.push_back (0x7F);
      buf_c.push_back (0xFF);
      std::vector<unsigned char> truncated =
	fdp.ConsumeBytes<unsigned char> (fdp.ConsumeIntegralInRange<size_t> (0, 4));
      buf_c.insert (buf_c.end (), truncated.begin (), truncated.end ());
      try_decode (etype, buf_c, true);
    }

    {
      unsigned int ns_idx = fdp.ConsumeIntegralInRange<unsigned int> (
	  0, (unsigned int) N_NONSTRING_ETYPES - 1);
      unsigned int ns_etype = kNonStringEtypes[ns_idx];
      unsigned char ns_tag = tag_for_etype (ns_etype);

      std::vector<unsigned char> buf_d;
      buf_d.push_back (ns_tag);
      buf_d.push_back (0x82);
      buf_d.push_back (0x7F);
      buf_d.push_back (0xFF);
      std::vector<unsigned char> truncated =
	fdp.ConsumeBytes<unsigned char> (fdp.ConsumeIntegralInRange<size_t> (0, 4));
      buf_d.insert (buf_d.end (), truncated.begin (), truncated.end ());
      try_decode (ns_etype, buf_d, true);
    }
  }

  {
    static const unsigned char dummy[4] = { 0, 1, 2, 3 };

    for (size_t i = 0; i < N_STRING_ETYPES; i++)
      {
	unsigned char *str = NULL;
	unsigned int str_len = 0, ber_len = 0;
	int ret = asn1_decode_simple_ber (kStringEtypes[i], dummy, 0,
					  &str, &str_len, &ber_len);
	if (ret == ASN1_SUCCESS)
	  {
	    assert (str != NULL || str_len == 0);
	    free (str);
	  }
      }

    for (size_t i = 0; i < N_NONSTRING_ETYPES; i++)
      {
	unsigned char *str = NULL;
	unsigned int str_len = 0;
	int ret = asn1_decode_simple_ber (kNonStringEtypes[i], dummy, 0,
					  &str, &str_len, NULL);
	if (ret == ASN1_SUCCESS)
	  {
	    assert (str != NULL || str_len == 0);
	    free (str);
	  }
      }

    {
      unsigned char *str = NULL;
      unsigned int str_len = 0, ber_len = 0;
      int ret = asn1_decode_simple_ber (ASN1_ETYPE_OCTET_STRING, data, 0,
					&str, &str_len, &ber_len);
      if (ret == ASN1_SUCCESS)
	{
	  assert (str != NULL || str_len == 0);
	  free (str);
	}
    }
  }

  {
    static const unsigned char class_bits[] = { 0x40, 0x80, 0xC0 };
    unsigned int idx = fdp.ConsumeIntegralInRange<unsigned int> (
	0, (unsigned int) N_STRING_ETYPES - 1);
    unsigned int etype = kStringEtypes[idx];
    unsigned char tag = tag_for_etype (etype);
    std::vector<unsigned char> content = fdp.ConsumeBytes<unsigned char> (
	fdp.ConsumeIntegralInRange<size_t> (0, 8));

    for (size_t cbi = 0; cbi < sizeof (class_bits) / sizeof (class_bits[0]);
	 cbi++)
      {
	unsigned char cb = class_bits[cbi];
	for (int constructed = 0; constructed <= 1; constructed++)
	  {
	    std::vector<unsigned char> buf;
	    buf.push_back ((unsigned char) (tag | cb | (constructed ? 0x20 : 0)));
	    append_definite_length (buf, content.size ());
	    buf.insert (buf.end (), content.begin (), content.end ());
	    try_decode (etype, buf, true);
	    try_decode (etype, buf, false);
	  }
      }
  }

  {
    unsigned int idx = fdp.ConsumeIntegralInRange<unsigned int> (
	0, (unsigned int) N_STRING_ETYPES - 1);
    unsigned int etype = kStringEtypes[idx];
    unsigned char tagnum = tag_for_etype (etype);

    {
      std::vector<unsigned char> buf;
      buf.push_back (0x1F);
      buf.push_back (tagnum);
      append_definite_length (buf, 1);
      buf.push_back (0xAA);
      try_decode (etype, buf, true);
      try_decode (etype, buf, false);
    }

    {
      std::vector<unsigned char> buf;
      buf.push_back (0x1F);
      buf.push_back (0x80);
      buf.push_back (0x80);
      buf.push_back (tagnum);
      append_definite_length (buf, 1);
      buf.push_back (0xBB);
      try_decode (etype, buf, true);
    }

    {
      std::vector<unsigned char> inner;
      inner.push_back (tagnum | 0x00);
      std::vector<unsigned char> buf;
      buf.push_back (0x3F);
      buf.push_back (tagnum);
      std::vector<unsigned char> content = fdp.ConsumeBytes<unsigned char> (
	  fdp.ConsumeIntegralInRange<size_t> (0, 8));
      std::vector<unsigned char> innerTLV;
      innerTLV.push_back (tagnum);
      append_definite_length (innerTLV, content.size ());
      innerTLV.insert (innerTLV.end (), content.begin (), content.end ());
      append_definite_length (buf, innerTLV.size ());
      buf.insert (buf.end (), innerTLV.begin (), innerTLV.end ());
      try_decode (etype, buf, true);
    }
  }

  {
    unsigned int idx = fdp.ConsumeIntegralInRange<unsigned int> (
	0, (unsigned int) N_STRING_ETYPES - 1);
    unsigned int etype = kStringEtypes[idx];
    unsigned char tagnum = tag_for_etype (etype);

    {
      std::vector<unsigned char> buf;
      buf.push_back (0x1F);
      for (int i = 0; i < 6; i++)
	buf.push_back (0xFF);
      buf.push_back (tagnum);
      append_definite_length (buf, 1);
      buf.push_back (0xAA);
      try_decode (etype, buf, true);
      try_decode (etype, buf, false);
    }

    {
      std::vector<unsigned char> buf;
      buf.push_back (0x1F);
      buf.push_back (0x80);
      try_decode (etype, buf, true);
      try_decode (etype, buf, false);
    }

    {
      std::vector<unsigned char> buf;
      buf.push_back (0x3F);
      buf.push_back (tagnum);
      try_decode (etype, buf, true);
      try_decode (etype, buf, false);
    }
  }

  return 0;
}