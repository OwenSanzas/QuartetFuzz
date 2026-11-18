#include <config.h>

#include <stdlib.h>
#include <string.h>

#include "libtasn1.h"
#include "fuzzer.h"

#define MAXDATASIZE 512

static int
build_matching_length (int der_len, unsigned char *lenbuf)
{
  int len_len = 1;
  int iter;

  for (iter = 0; iter < 4; iter++)
    {
      int content_len = der_len - len_len;
      int new_len_len;

      if (content_len < 0)
	content_len = 0;

      asn1_length_der ((unsigned long) content_len, NULL, &new_len_len);
      if (new_len_len == len_len)
	break;
      len_len = new_len_len;
    }

  {
    int content_len = der_len - len_len;
    if (content_len < 0)
      content_len = 0;
    asn1_length_der ((unsigned long) content_len, lenbuf, &len_len);
  }

  return len_len;
}

int
LLVMFuzzerTestOneInput (const uint8_t *data, size_t size)
{
  int ret_len = 0;

  if (size < 3 || size > MAXDATASIZE)
    return 0;

  unsigned char ctrl = data[0];
  unsigned char use_null = data[1];
  unsigned char recipe = data[2];
  const unsigned char *in = data + 3;
  int in_len = (int) (size - 3);

  int str_size;
  if (ctrl & 0x80)
    {
      int scale = in_len > 0 ? (in_len * 4 + 8) : 64;
      str_size = (int) ((ctrl & 0x7f) % scale);
    }
  else
    {
      str_size = ctrl % 64;
    }

  unsigned char *der = (unsigned char *) malloc ((size_t) MAXDATASIZE + 8);
  if (der == NULL)
    return 0;
  int der_len;

  if (in_len > 0)
    memcpy (der, in, (size_t) in_len);
  der_len = in_len;

  switch (recipe % 7)
    {
    case 0:

      break;

    case 1:
      if (der_len >= 1)
	der[0] = 0x80;
      break;

    case 2:

      if (der_len >= 1)
	{
	  unsigned char lenbuf[ASN1_MAX_LENGTH_SIZE];
	  int lenbytes = build_matching_length (der_len, lenbuf);
	  if (lenbytes <= der_len)
	    {
	      memmove (der + lenbytes, der + 1,
		       (size_t) (der_len - lenbytes > 0
				 ? der_len - lenbytes : 0));
	      memcpy (der, lenbuf, (size_t) lenbytes);
	    }
	}
      break;

    case 3:

      if (der_len >= 7)
	{
	  der[0] = 0x85;
	  der[1] = 0xFF;
	  der[2] = 0xFF;
	  der[3] = 0xFF;
	  der[4] = 0xFF;

	}
      break;

    case 4:

      if (der_len >= 12)
	{
	  unsigned char lenbuf[ASN1_MAX_LENGTH_SIZE];
	  int lenbytes = build_matching_length (der_len, lenbuf);
	  if (lenbytes <= der_len)
	    {
	      memmove (der + lenbytes, der + 1,
		       (size_t) (der_len - lenbytes));
	      memcpy (der, lenbuf, (size_t) lenbytes);
	      for (int i = 0; i < 10 && lenbytes + i < der_len; i++)
		der[lenbytes + i] = 0xFF;
	    }
	}
      break;

    case 5:

      if (der_len >= 4)
	{
	  unsigned char lenbuf[ASN1_MAX_LENGTH_SIZE];
	  int lenbytes = build_matching_length (der_len, lenbuf);
	  if (lenbytes <= der_len)
	    {
	      memmove (der + lenbytes, der + 1,
		       (size_t) (der_len - lenbytes));
	      memcpy (der, lenbuf, (size_t) lenbytes);
	      der[lenbytes] = 0x80;
	      if (lenbytes + 2 < der_len)
		{
		  der[lenbytes + 1] = 0x01;
		  der[lenbytes + 2] = 0x80;
		}
	    }
	}
      break;

    case 6:

      if (der_len >= 13)
	{
	  unsigned char lenbuf[ASN1_MAX_LENGTH_SIZE];
	  int lenbytes = build_matching_length (der_len, lenbuf);
	  if (lenbytes <= der_len && lenbytes + 10 <= der_len)
	    {
	      memmove (der + lenbytes, der + 1,
		       (size_t) (der_len - lenbytes));
	      memcpy (der, lenbuf, (size_t) lenbytes);

	      der[lenbytes + 0] = 0x81;

	      for (int i = 1; i < 9; i++)
		der[lenbytes + i] = 0xFF;

	      der[lenbytes + 9] = 0x7F;
	    }
	}
      break;
    }

  char *str = NULL;
  if ((use_null % 8) != 0)
    {

      str = (char *) calloc (str_size > 0 ? (size_t) str_size : 1, 1);
      if (str == NULL)
	{
	  free (der);
	  return 0;
	}
    }

  asn1_get_object_id_der (der, der_len, &ret_len, str, str_size);

  free (str);
  free (der);
  return 0;
}