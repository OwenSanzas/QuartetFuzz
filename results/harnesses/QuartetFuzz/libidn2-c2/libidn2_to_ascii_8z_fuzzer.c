#include <config.h>

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "idn2.h"

#ifdef __cplusplus
extern "C"
#endif
int
LLVMFuzzerTestOneInput (const uint8_t *data, size_t size)
{
  if (size < 1 || size > 2048)
    return 0;

  int flags = (int) data[0];

  const uint8_t *strdata = data + 1;
  size_t strsize = size - 1;

  char *domain = (char *) malloc (strsize + 1);
  assert (domain != NULL);
  memcpy (domain, strdata, strsize);
  domain[strsize] = '\0';

  char *out = NULL;
  int rc = idn2_to_ascii_8z (domain, &out, flags);
  if (rc == IDN2_OK)
    {
      if (out)
	{

	  char *uout = NULL;
	  if (idn2_to_unicode_8z8z (out, &uout, 0) == IDN2_OK)
	    {
	      if (uout)
		idn2_free (uout);
	    }

	  size_t outlen = strlen (out);
	  unsigned long checksum = 0;
	  for (size_t i = 0; i < outlen; i++)
	    checksum += (unsigned char) out[i];
	  if (checksum == (unsigned long) -1)
	    abort ();

	  idn2_free (out);
	}
    }

  idn2_lookup_u8 ((const uint8_t *) domain, NULL, flags);

  char *lout = NULL;
  if (idn2_to_ascii_lz (domain, &lout, flags) == IDN2_OK)
    {
      if (lout)
	idn2_free (lout);
    }

  size_t u32len = strsize / sizeof (uint32_t);
  uint32_t *u32 = (uint32_t *) malloc ((u32len + 1) * sizeof (uint32_t));
  assert (u32 != NULL);
  memcpy (u32, strdata, u32len * sizeof (uint32_t));
  u32[u32len] = 0;
  char *out4 = NULL;
  if (idn2_to_ascii_4z (u32, &out4, flags) == IDN2_OK)
    {
      if (out4)
	idn2_free (out4);
    }
  free (u32);

  uint8_t *reg_out = NULL;
  if (idn2_register_u8 ((const uint8_t *) domain, NULL, &reg_out, flags)
      == IDN2_OK)
    {
      if (reg_out)
	idn2_free (reg_out);
    }

  size_t alabel_len = strsize + 4;
  uint8_t *alabel = (uint8_t *) malloc (alabel_len + 1);
  assert (alabel != NULL);
  memcpy (alabel, "xn--", 4);
  memcpy (alabel + 4, strdata, strsize);
  alabel[alabel_len] = '\0';

  uint8_t *reg_out2 = NULL;
  if (idn2_register_u8 (NULL, alabel, &reg_out2, flags) == IDN2_OK)
    {
      if (reg_out2)
	idn2_free (reg_out2);
    }

  uint8_t *reg_out3 = NULL;
  if (idn2_register_u8 ((const uint8_t *) domain, alabel, &reg_out3, flags)
      == IDN2_OK)
    {
      if (reg_out3)
	idn2_free (reg_out3);
    }

  free (alabel);

  free (domain);
  return 0;
}
