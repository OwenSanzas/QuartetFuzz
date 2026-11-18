#include <config.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "libtasn1.h"
#include "fuzzer.h"

#define HEADER_SIZE 5
#define MAX_DER_LEN (1u << 20)
#define MAX_VAL_LEN 4096u
#define MAX_STREAM_ITERS 64

static unsigned int
read_u32 (const uint8_t *p)
{
  return ((unsigned int) p[0]) |
    ((unsigned int) p[1] << 8) |
    ((unsigned int) p[2] << 16) | ((unsigned int) p[3] << 24);
}

int
LLVMFuzzerTestOneInput (const uint8_t *data, size_t size)
{
  if (size < HEADER_SIZE)
    return 0;

  unsigned int etype = read_u32 (data);
  uint8_t mode = data[4];

  const unsigned char *der = data + HEADER_SIZE;
  size_t der_size = size - HEADER_SIZE;
  if (der_size > MAX_DER_LEN)
    der_size = MAX_DER_LEN;
  unsigned int der_len = (unsigned int) der_size;

  unsigned char *str = NULL;
  unsigned int str_len = 0;
  unsigned int ber_len = 0;
  unsigned int *ber_len_ptr = (mode & 1) ? NULL : &ber_len;

  int ret = asn1_decode_simple_ber (etype, der, der_len, &str, &str_len,
				     ber_len_ptr);
  if (ret == ASN1_SUCCESS)
    {

      volatile unsigned char sink = 0;
      for (unsigned int i = 0; i < str_len; i++)
	sink ^= str[i];
      (void) sink;
      free (str);
      str = NULL;
    }

  {
    unsigned int cursor = 0;
    for (int iter = 0; iter < MAX_STREAM_ITERS && cursor < der_len; iter++)
      {
	unsigned char *sptmp = NULL;
	unsigned int svlen = 0;
	unsigned int sber_len = 0;

	int sres = asn1_decode_simple_ber (etype, der + cursor,
					    der_len - cursor, &sptmp,
					    &svlen, &sber_len);
	if (sres != ASN1_SUCCESS)
	  break;

	volatile unsigned char ssink = 0;
	for (unsigned int i = 0; i < svlen; i++)
	  ssink ^= sptmp[i];
	(void) ssink;
	free (sptmp);

	if (sber_len == 0 || sber_len > der_len - cursor)
	  break;

	cursor += sber_len;
      }
  }

  const unsigned char *str2 = NULL;
  unsigned int str2_len = 0;
  int ret2 = asn1_decode_simple_der (etype, der, der_len, &str2, &str2_len);
  if (ret2 == ASN1_SUCCESS)
    {
      volatile unsigned char sink2 = 0;
      for (unsigned int i = 0; i < str2_len; i++)
	sink2 ^= str2[i];
      (void) sink2;
    }

  if (der_size > 0)
    {
      unsigned char tl[ASN1_MAX_TL_SIZE];
      unsigned int tl_len = sizeof (tl);
      unsigned int val_len = (unsigned int) der_size;
      if (val_len > MAX_VAL_LEN)
	val_len = MAX_VAL_LEN;

      int eret = asn1_encode_simple_der (etype, der, val_len, tl, &tl_len);
      if (eret == ASN1_SUCCESS)
	{
	  size_t total = (size_t) tl_len + (size_t) val_len;
	  unsigned char *buf = (unsigned char *) malloc (total);
	  if (buf != NULL)
	    {
	      memcpy (buf, tl, tl_len);
	      memcpy (buf + tl_len, der, val_len);

	      unsigned char *rstr = NULL;
	      unsigned int rstr_len = 0;
	      unsigned int rber_len = 0;
	      int rret = asn1_decode_simple_ber (etype, buf,
						  (unsigned int) total,
						  &rstr, &rstr_len,
						  &rber_len);
	      if (rret == ASN1_SUCCESS)
		{
		  volatile unsigned char sink3 = 0;
		  for (unsigned int i = 0; i < rstr_len; i++)
		    sink3 ^= rstr[i];
		  (void) sink3;
		  free (rstr);
		  rstr = NULL;
		}

	      const unsigned char *rstr2 = NULL;
	      unsigned int rstr2_len = 0;
	      int rret2 = asn1_decode_simple_der (etype, buf,
						   (unsigned int) total,
						   &rstr2, &rstr2_len);
	      (void) rret2;
	      (void) rstr2;
	      (void) rstr2_len;

	      free (buf);
	    }
	}
    }

  return 0;
}
