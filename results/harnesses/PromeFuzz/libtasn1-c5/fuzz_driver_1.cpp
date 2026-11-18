#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <libtasn1.h>

static const char *asn1_module_definition =
    "TestModule DEFINITIONS IMPLICIT TAGS ::=\n"
    "BEGIN\n"
    "TestSequence ::= SEQUENCE {\n"
    "    value1 INTEGER,\n"
    "    value2 OCTET STRING,\n"
    "    value3 BOOLEAN OPTIONAL\n"
    "}\n"
    "END\n";

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size)
{
  static asn1_node definitions = nullptr;
  static bool initialized = false;

  if (!initialized)
    {
      initialized = true;

      FILE *fp = fopen ("./dummy_file", "w");
      if (fp)
        {
          fputs (asn1_module_definition, fp);
          fclose (fp);
        }

      char error_desc[ASN1_MAX_ERROR_DESCRIPTION_SIZE];
      int res = asn1_parser2tree ("./dummy_file", &definitions, error_desc);
      if (res != ASN1_SUCCESS)
        {
          definitions = nullptr;
        }
    }

  if (!definitions)
    {
      return 0;
    }

  asn1_node element = nullptr;
  int res = asn1_create_element (definitions, "TestModule.TestSequence", &element);
  if (res != ASN1_SUCCESS || element == nullptr)
    {
      return 0;
    }

  char error_desc[ASN1_MAX_ERROR_DESCRIPTION_SIZE];
  memset (error_desc, 0, sizeof (error_desc));

  asn1_der_decoding (&element, Data, static_cast<int> (Size), error_desc);

  asn1_delete_structure (&element);

  return 0;
}