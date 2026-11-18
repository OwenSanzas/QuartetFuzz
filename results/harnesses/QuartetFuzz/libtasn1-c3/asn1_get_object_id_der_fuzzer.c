#include <config.h>

#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "libtasn1.h"
#include "fuzzer.h"

#define MAX_STR 256

const asn1_static_node gnutls_asn1_tab[] = {
  {"GNUTLS", 536872976, NULL},
  {NULL, 1073741836, NULL},
  {"RSAPublicKey", 1610612741, NULL},
  {"modulus", 1073741827, NULL},
  {"publicExponent", 3, NULL},
  {"RSAPrivateKey", 1610612741, NULL},
  {"version", 1073741827, NULL},
  {"modulus", 1073741827, NULL},
  {"publicExponent", 1073741827, NULL},
  {"privateExponent", 1073741827, NULL},
  {"prime1", 1073741827, NULL},
  {"prime2", 1073741827, NULL},
  {"exponent1", 1073741827, NULL},
  {"exponent2", 1073741827, NULL},
  {"coefficient", 1073741827, NULL},
  {"otherPrimeInfos", 16386, "OtherPrimeInfos"},
  {"ProvableSeed", 1610612741, NULL},
  {"algorithm", 1073741836, NULL},
  {"seed", 7, NULL},
  {"OtherPrimeInfos", 1612709899, NULL},
  {"MAX", 1074266122, "1"},
  {NULL, 2, "OtherPrimeInfo"},
  {"OtherPrimeInfo", 1610612741, NULL},
  {"prime", 1073741827, NULL},
  {"exponent", 1073741827, NULL},
  {"coefficient", 3, NULL},
  {"AlgorithmIdentifier", 1610612741, NULL},
  {"algorithm", 1073741836, NULL},
  {"parameters", 541081613, NULL},
  {"algorithm", 1, NULL},
  {"DigestInfo", 1610612741, NULL},
  {"digestAlgorithm", 1073741826, "DigestAlgorithmIdentifier"},
  {"digest", 7, NULL},
  {"DigestAlgorithmIdentifier", 1073741826, "AlgorithmIdentifier"},
  {"DSAPublicKey", 1073741827, NULL},
  {"DSAParameters", 1610612741, NULL},
  {"p", 1073741827, NULL},
  {"q", 1073741827, NULL},
  {"g", 3, NULL},
  {"DSASignatureValue", 1610612741, NULL},
  {"r", 1073741827, NULL},
  {"s", 3, NULL},
  {"DSAPrivateKey", 1610612741, NULL},
  {"version", 1073741827, NULL},
  {"p", 1073741827, NULL},
  {"q", 1073741827, NULL},
  {"g", 1073741827, NULL},
  {"Y", 1073741827, NULL},
  {"priv", 3, NULL},
  {"DHParameter", 1610612741, NULL},
  {"prime", 1073741827, NULL},
  {"base", 1073741827, NULL},
  {"privateValueLength", 16387, NULL},
  {"ECParameters", 1610612754, NULL},
  {"namedCurve", 12, NULL},
  {"ECPrivateKey", 1610612741, NULL},
  {"Version", 1073741827, NULL},
  {"privateKey", 1073741831, NULL},
  {"parameters", 1610637314, "ECParameters"},
  {NULL, 2056, "0"},
  {"publicKey", 536895494, NULL},
  {NULL, 2056, "1"},
  {"PrincipalName", 1610612741, NULL},
  {"name-type", 1610620931, NULL},
  {NULL, 2056, "0"},
  {"name-string", 536879115, NULL},
  {NULL, 1073743880, "1"},
  {NULL, 27, NULL},
  {"KRB5PrincipalName", 1610612741, NULL},
  {"realm", 1610620955, NULL},
  {NULL, 2056, "0"},
  {"principalName", 536879106, "PrincipalName"},
  {NULL, 2056, "1"},
  {"RSAPSSParameters", 1610612741, NULL},
  {"hashAlgorithm", 1610637314, "AlgorithmIdentifier"},
  {NULL, 2056, "0"},
  {"maskGenAlgorithm", 1610637314, "AlgorithmIdentifier"},
  {NULL, 2056, "1"},
  {"saltLength", 1610653699, NULL},
  {NULL, 1073741833, "20"},
  {NULL, 2056, "2"},
  {"trailerField", 536911875, NULL},
  {NULL, 1073741833, "1"},
  {NULL, 2056, "3"},
  {"GOSTParameters", 1610612741, NULL},
  {"publicKeyParamSet", 1073741836, NULL},
  {"digestParamSet", 1073741836, NULL},
  {"encryptionParamSet", 16396, NULL},
  {"GOSTPrivateKey", 1073741831, NULL},
  {"GOSTPrivateKeyOld", 3, NULL},
  {NULL, 0, NULL}
};

int
LLVMFuzzerTestOneInput (const uint8_t *data, size_t size)
{
  if (size < 2)
    return 0;

  uint8_t selector = data[0];
  uint8_t str_size_byte = data[1];

  const unsigned char *der = data + 2;
  size_t der_len_sz = size - 2;
  if (der_len_sz > (size_t) INT_MAX)
    der_len_sz = (size_t) INT_MAX;
  int der_len = (int) der_len_sz;

  char str[MAX_STR];
  int str_size = (int) (str_size_byte % MAX_STR) + 1;
  int ret_len = 0;

  int ret = asn1_get_object_id_der (der, der_len, &ret_len, str, str_size);
  if (ret == ASN1_SUCCESS)
    {

      size_t slen = strnlen (str, (size_t) str_size);
      (void) slen;
    }

  {
    int len_len = 0;
    asn1_get_length_der (der, der_len, &len_len);
  }

  if (selector & 1)
    {
      char oidstr[128];
      size_t pos = 0;
      size_t i = 2;
      int arcs = 0;

      oidstr[0] = 0;
      while (i < size && pos < sizeof (oidstr) - 24 && arcs < 20)
	{
	  uint64_t val = data[i++];
	  if (i < size)
	    val = (val << 8) | data[i++];

	  int n = snprintf (oidstr + pos, sizeof (oidstr) - pos,
			     arcs == 0 ? "%llu" : ".%llu",
			     (unsigned long long) val);
	  if (n <= 0)
	    break;
	  pos += (size_t) n;
	  arcs++;
	}

      if (arcs >= 2)
	{
	  unsigned char enc[64];
	  int enc_len = sizeof (enc);
	  int r = asn1_object_id_der (oidstr, enc, &enc_len, 0);

	  if (r == ASN1_SUCCESS && enc_len > 1)
	    {
	      int ll = 0;
	      int content_len = asn1_get_length_der (enc + 1, enc_len - 1, &ll);

	      if (content_len > 0 && ll > 0)
		{
		  char str2[MAX_STR];
		  int ret_len2 = 0;

		  int r2 = asn1_get_object_id_der (enc + 1, enc_len - 1,
						    &ret_len2, str2,
						    sizeof (str2));
		  if (r2 == ASN1_SUCCESS)
		    {
		      size_t slen2 = strnlen (str2, sizeof (str2));
		      (void) slen2;
		    }
		}
	    }
	}
    }

  if (selector & 2)
    {
      static asn1_node definitions = NULL;
      static int definitions_tried = 0;

      if (!definitions_tried)
	{
	  definitions_tried = 1;
	  asn1_array2tree (gnutls_asn1_tab, &definitions, NULL);
	}

      if (definitions != NULL)
	{
	  asn1_node elem;

	  if (asn1_create_element (definitions, "GNUTLS.AlgorithmIdentifier",
				    &elem) == ASN1_SUCCESS)
	    {
	      char errdesc[ASN1_MAX_ERROR_DESCRIPTION_SIZE];
	      int dres = asn1_der_decoding (&elem, der, der_len, errdesc);

	      if (dres == ASN1_SUCCESS)
		{
		  char oidbuf[MAX_STR];
		  int oidlen = sizeof (oidbuf);

		  if (asn1_read_value (elem, "algorithm", oidbuf, &oidlen) ==
		      ASN1_SUCCESS)
		    {
		      size_t l = strnlen (oidbuf, sizeof (oidbuf));
		      (void) l;
		    }
		}

	      asn1_delete_structure (&elem);
	    }
	}
    }

  return 0;
}
