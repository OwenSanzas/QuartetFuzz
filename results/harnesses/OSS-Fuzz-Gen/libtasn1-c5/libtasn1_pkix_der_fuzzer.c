#include <config.h>

#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "libtasn1.h"
#include "fuzzer.h"

static const asn1_static_node test_asn1_tab[] = {
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

static const char *const test_elements[] = {
  "GNUTLS.RSAPublicKey",
  "GNUTLS.RSAPrivateKey",
  "GNUTLS.ProvableSeed",
  "GNUTLS.OtherPrimeInfos",
  "GNUTLS.OtherPrimeInfo",
  "GNUTLS.AlgorithmIdentifier",
  "GNUTLS.DigestInfo",
  "GNUTLS.DigestAlgorithmIdentifier",
  "GNUTLS.DSAPublicKey",
  "GNUTLS.DSAParameters",
  "GNUTLS.DSASignatureValue",
  "GNUTLS.DSAPrivateKey",
  "GNUTLS.DHParameter",
  "GNUTLS.ECParameters",
  "GNUTLS.ECPrivateKey",
  "GNUTLS.PrincipalName",
  "GNUTLS.KRB5PrincipalName",
  "GNUTLS.RSAPSSParameters",
  "GNUTLS.GOSTParameters",
  "GNUTLS.GOSTPrivateKey",
  "GNUTLS.GOSTPrivateKeyOld",
};

static const char extra_asn1_module[] =
  "EXTRA { }\n"
  "DEFINITIONS EXPLICIT TAGS ::=\n"
  "BEGIN\n"
  "ExtraEnum ::= ENUMERATED { zero(0), one(1), two(2) }\n"
  "ExtraChoice ::= CHOICE {\n"
  "   asInt     INTEGER,\n"
  "   asBool    BOOLEAN,\n"
  "   asOctets  OCTET STRING\n"
  "}\n"
  "ExtraBig ::= SEQUENCE {\n"
  "   flag        BOOLEAN DEFAULT TRUE,\n"
  "   placeholder NULL OPTIONAL,\n"
  "   kind        ExtraEnum,\n"
  "   gt          GeneralizedTime,\n"
  "   ut          UTCTime,\n"
  "   bag         SET OF INTEGER,\n"
  "   grp         SET {\n"
  "       a INTEGER,\n"
  "       b OCTET STRING OPTIONAL,\n"
  "       c BOOLEAN OPTIONAL,\n"
  "       d IA5String OPTIONAL\n"
  "   },\n"
  "   ia5         IA5String,\n"
  "   printable   PrintableString,\n"
  "   utf8        UTF8String,\n"
  "   bmp         BMPString,\n"
  "   visible     VisibleString,\n"
  "   numeric     NumericString,\n"
  "   teletex     TeletexString,\n"
  "   universal   UniversalString,\n"
  "   tagged      [0] IMPLICIT INTEGER OPTIONAL,\n"
  "   taggedAny   [2] EXPLICIT ANY OPTIONAL\n"
  "}\n"
  "END\n";

static const uint8_t *g_mod_data;
static size_t g_mod_size;

#if defined HAVE_DLFCN_H && defined HAVE_FMEMOPEN
# include <dlfcn.h>
# ifdef RTLD_NEXT

FILE *
fopen (const char *pathname, const char *mode)
{
  FILE *(*libc_fopen) (const char *, const char *) =
    (FILE * (*)(const char *, const char *)) dlsym (RTLD_NEXT, "fopen");

  if (!strcmp (pathname, "extra.asn"))
    return fmemopen ((void *) g_mod_data, g_mod_size, mode);

  return libc_fopen (pathname, mode);
}
# endif
#endif

static const char *const extra_elements[] = {
  "EXTRA.ExtraBig",
  "EXTRA.ExtraEnum",
  "EXTRA.ExtraChoice",
};

static void
try_decode (const uint8_t *data, size_t size,
            asn1_node test_definitions, asn1_node extra_definitions)
{
  asn1_node element = NULL;
  asn1_node element2 = NULL;
  char error_desc[ASN1_MAX_ERROR_DESCRIPTION_SIZE];
  int res;
  size_t idx;
  const uint8_t *der_data;
  int der_len;
  unsigned int flags = 0;
  const size_t n1 = sizeof (test_elements) / sizeof (test_elements[0]);
  const size_t n2 = sizeof (extra_elements) / sizeof (extra_elements[0]);
  const size_t name_count = n1 + n2;

  if (size < 1)
    {
      idx = 0;
      der_data = data;
      der_len = 0;
    }
  else
    {
      idx = (size_t) data[0] % name_count;
      der_data = data + 1;
      der_len = (int) (size - 1);
    }

  if (idx < n1)
    res = asn1_create_element (test_definitions, test_elements[idx],
                                &element);
  else
    res = asn1_create_element (extra_definitions,
                                extra_elements[idx - n1], &element);

  if (res == ASN1_SUCCESS && element != NULL)
    {
      memset (error_desc, 0, sizeof (error_desc));

      (void) asn1_der_decoding (&element, der_data, der_len, error_desc);

      asn1_delete_structure (&element);
    }

  if (der_len > 0)
    flags = ((unsigned int) der_data[der_len - 1]) &
            (ASN1_DECODE_FLAG_ALLOW_PADDING |
             ASN1_DECODE_FLAG_STRICT_DER |
             ASN1_DECODE_FLAG_ALLOW_INCORRECT_TIME);

  if (idx < n1)
    res = asn1_create_element (test_definitions, test_elements[idx],
                                &element2);
  else
    res = asn1_create_element (extra_definitions,
                                extra_elements[idx - n1], &element2);

  if (res == ASN1_SUCCESS && element2 != NULL)
    {
      int max_len = der_len;

      memset (error_desc, 0, sizeof (error_desc));

      (void) asn1_der_decoding2 (&element2, der_data, &max_len, flags,
                                 error_desc);

      asn1_delete_structure (&element2);
    }
}

static const uint8_t seed_extrabig_full[] = {
  0x15,
  0x30, 0x5E,
  0x01, 0x01, 0xFF,
  0x05, 0x00,
  0x0A, 0x01, 0x00,
  0x18, 0x0F, '2', '0', '2', '4', '0', '1', '0', '1',
  '0', '0', '0', '0', '0', '0', 'Z',
  0x17, 0x0D, '2', '4', '0', '1', '0', '1',
  '0', '0', '0', '0', '0', '0', 'Z',
  0x31, 0x03, 0x02, 0x01, 0x01,
  0x31, 0x0C,
    0x02, 0x01, 0x01,
    0x04, 0x01, 0x41,
    0x01, 0x01, 0x00,
    0x16, 0x01, 0x61,
  0x16, 0x01, 0x61,
  0x13, 0x01, 0x61,
  0x0C, 0x01, 0x61,
  0x1E, 0x02, 0x00, 0x61,
  0x1A, 0x01, 0x61,
  0x12, 0x01, 0x31,
  0x14, 0x01, 0x61,
  0x1C, 0x04, 0x00, 0x00, 0x00, 0x61,
  0x80, 0x01, 0x05,
  0xA2, 0x02, 0x05, 0x00
};

static const uint8_t seed_extrabig_minimal[] = {
  0x15,
  0x30, 0x49,
  0x0A, 0x01, 0x00,
  0x18, 0x0F, '2', '0', '2', '4', '0', '1', '0', '1',
  '0', '0', '0', '0', '0', '0', 'Z',
  0x17, 0x0D, '2', '4', '0', '1', '0', '1',
  '0', '0', '0', '0', '0', '0', 'Z',
  0x31, 0x03, 0x02, 0x01, 0x01,
  0x31, 0x03, 0x02, 0x01, 0x01,
  0x16, 0x01, 0x61,
  0x13, 0x01, 0x61,
  0x0C, 0x01, 0x61,
  0x1E, 0x02, 0x00, 0x61,
  0x1A, 0x01, 0x61,
  0x12, 0x01, 0x31,
  0x14, 0x01, 0x61,
  0x1C, 0x04, 0x00, 0x00, 0x00, 0x61,
};

static const uint8_t seed_extrabig_set_reordered[] = {
  0x15,
  0x30, 0x5B,
  0x01, 0x01, 0xFF,
  0x05, 0x00,
  0x0A, 0x01, 0x00,
  0x18, 0x0F, '2', '0', '2', '4', '0', '1', '0', '1',
  '0', '0', '0', '0', '0', '0', 'Z',
  0x17, 0x0D, '2', '4', '0', '1', '0', '1',
  '0', '0', '0', '0', '0', '0', 'Z',
  0x31, 0x03, 0x02, 0x01, 0x01,
  0x31, 0x09,
    0x16, 0x01, 0x61,
    0x01, 0x01, 0x00,
    0x02, 0x01, 0x01,
  0x16, 0x01, 0x61,
  0x13, 0x01, 0x61,
  0x0C, 0x01, 0x61,
  0x1E, 0x02, 0x00, 0x61,
  0x1A, 0x01, 0x61,
  0x12, 0x01, 0x31,
  0x14, 0x01, 0x61,
  0x1C, 0x04, 0x00, 0x00, 0x00, 0x61,
  0x80, 0x01, 0x05,
  0xA2, 0x02, 0x05, 0x00
};

static const uint8_t seed_choice_int[] =
  { 0x17, 0x02, 0x01, 0x2A };
static const uint8_t seed_choice_bool[] =
  { 0x17, 0x01, 0x01, 0xFF };
static const uint8_t seed_choice_octets[] =
  { 0x17, 0x04, 0x02, 0x68, 0x69 };

static const uint8_t seed_enum[] = { 0x16, 0x0A, 0x01, 0x01 };

static const uint8_t seed_ber_octet[] = {
  0x06,
  0x30, 0x11,
    0x30, 0x05, 0x06, 0x01, 0x2A, 0x05, 0x00,
    0x24, 0x80,

      0x04, 0x01, 0x41,
      0x04, 0x01, 0x42,
      0x00, 0x00
};

struct replay_seed
{
  const uint8_t *data;
  size_t size;
};

static const struct replay_seed replay_seeds[] = {
  { seed_extrabig_full, sizeof (seed_extrabig_full) },
  { seed_extrabig_minimal, sizeof (seed_extrabig_minimal) },
  { seed_extrabig_set_reordered, sizeof (seed_extrabig_set_reordered) },
  { seed_choice_int, sizeof (seed_choice_int) },
  { seed_choice_bool, sizeof (seed_choice_bool) },
  { seed_choice_octets, sizeof (seed_choice_octets) },
  { seed_enum, sizeof (seed_enum) },
  { seed_ber_octet, sizeof (seed_ber_octet) },
};

static const uint8_t seed_time_invalid_strict[] = {
  0x30, 0x49,
  0x0A, 0x01, 0x00,
  0x18, 0x0F, 'X', '0', '2', '4', '0', '1', '0', '1',
  '0', '0', '0', '0', '0', '0', 'Z',
  0x17, 0x0D, '2', '4', '0', '1', '0', '1',
  '0', '0', '0', '0', '0', '0', 'Z',
  0x31, 0x03, 0x02, 0x01, 0x01,
  0x31, 0x03, 0x02, 0x01, 0x01,
  0x16, 0x01, 0x61,
  0x13, 0x01, 0x61,
  0x0C, 0x01, 0x61,
  0x1E, 0x02, 0x00, 0x61,
  0x1A, 0x01, 0x61,
  0x12, 0x01, 0x31,
  0x14, 0x01, 0x61,
  0x1C, 0x04, 0x00, 0x00, 0x00, 0x61,
};

static void
replay_strict_time_check (asn1_node extra_definitions)
{
  asn1_node element = NULL;
  char error_desc[ASN1_MAX_ERROR_DESCRIPTION_SIZE];
  int res;
  int max_len;

  res = asn1_create_element (extra_definitions, "EXTRA.ExtraBig", &element);
  if (res != ASN1_SUCCESS || element == NULL)
    return;

  memset (error_desc, 0, sizeof (error_desc));
  max_len = (int) sizeof (seed_time_invalid_strict);

  (void) asn1_der_decoding2 (&element, seed_time_invalid_strict, &max_len,
                             ASN1_DECODE_FLAG_STRICT_DER, error_desc);

  asn1_delete_structure (&element);
}

int
LLVMFuzzerTestOneInput (const uint8_t *data, size_t size)
{
  static asn1_node test_definitions = NULL;
  static asn1_node extra_definitions = NULL;
  static int initialized = 0;
  int res;
  char error_desc[ASN1_MAX_ERROR_DESCRIPTION_SIZE];

  if (size > 100000)
    return 0;

  if (!initialized)
    {
      initialized = 1;

      res = asn1_array2tree (test_asn1_tab, &test_definitions, NULL);

      assert (res == ASN1_SUCCESS);

      g_mod_data = (const uint8_t *) extra_asn1_module;
      g_mod_size = sizeof (extra_asn1_module) - 1;
      memset (error_desc, 0, sizeof (error_desc));
      res = asn1_parser2tree ("extra.asn", &extra_definitions, error_desc);

      assert (res == ASN1_SUCCESS);

      if (test_definitions != NULL && extra_definitions != NULL)
        {
          size_t i;
          const size_t n = sizeof (replay_seeds) / sizeof (replay_seeds[0]);

          for (i = 0; i < n; i++)
            try_decode (replay_seeds[i].data, replay_seeds[i].size,
                        test_definitions, extra_definitions);

          replay_strict_time_check (extra_definitions);
        }
    }

  if (test_definitions == NULL || extra_definitions == NULL)
    return 0;

  try_decode (data, size, test_definitions, extra_definitions);

  return 0;
}