#include <config.h>
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "libtasn1.h"
#include "fuzzer.h"

int
LLVMFuzzerTestOneInput (const uint8_t *data, size_t size)
{
  if (size < 2)
    return 0;

  unsigned int etype = (unsigned int) (data[0] % 40);
  uint8_t mode = data[1];

  data += 2;
  size -= 2;

  {
    const unsigned char *str = NULL;
    unsigned int str_len = 0;
    int ret = asn1_decode_simple_der (etype, data, (unsigned int) size,
				       &str, &str_len);
    if (ret == ASN1_SUCCESS)
      {

	assert (str_len <= size);
	assert (str >= data && str + str_len <= data + size);
	unsigned char acc = 0;
	for (unsigned int i = 0; i < str_len; i++)
	  acc ^= str[i];
	(void) acc;

      }
  }

  {
    unsigned char *bstr = NULL;
    unsigned int bstr_len = 0;
    unsigned int ber_len = 0;
    unsigned int *ber_len_ptr = (mode & 0x1) ? &ber_len : NULL;

    int ret = asn1_decode_simple_ber (etype, data, (unsigned int) size,
				       &bstr, &bstr_len, ber_len_ptr);
    if (ret == ASN1_SUCCESS)
      {
	assert (bstr_len <= size || bstr == NULL);
	if (bstr != NULL)
	  {
	    unsigned char acc = 0;
	    for (unsigned int i = 0; i < bstr_len; i++)
	      acc ^= bstr[i];
	    (void) acc;
	  }
	free (bstr);
      }
  }

  if ((mode & 0x2) && size > 0 && size < (1u << 20))
    {
      unsigned char tl[ASN1_MAX_TL_SIZE];
      unsigned int tl_len = sizeof (tl);

      int eret = asn1_encode_simple_der (etype, data, (unsigned int) size,
					  tl, &tl_len);
      if (eret == ASN1_SUCCESS && tl_len <= sizeof (tl))
	{
	  size_t total_len = (size_t) tl_len + size;
	  unsigned char *buf = (unsigned char *) malloc (total_len);
	  if (buf != NULL)
	    {
	      memcpy (buf, tl, tl_len);
	      memcpy (buf + tl_len, data, size);

	      const unsigned char *str2 = NULL;
	      unsigned int str2_len = 0;
	      int dret = asn1_decode_simple_der (etype, buf,
						  (unsigned int) total_len,
						  &str2, &str2_len);
	      if (dret == ASN1_SUCCESS)
		{
		  assert (str2_len <= total_len);

		  if (str2_len == size)
		    {
		      int eq = (memcmp (str2, data, size) == 0);
		      (void) eq;
		    }
		}
	      free (buf);
	    }
	}
    }

  return 0;
}
