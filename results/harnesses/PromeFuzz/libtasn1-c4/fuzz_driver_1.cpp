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
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <climits>
#include <libtasn1.h>

static asn1_node definitions = nullptr;
static bool init_attempted = false;

static void init_definitions()
{
    init_attempted = true;

    const char *asn1_def =
        "PKIX1 DEFINITIONS IMPLICIT TAGS ::=\n"
        "BEGIN\n"
        "Test ::= SEQUENCE {\n"
        "  value INTEGER,\n"
        "  name OCTET STRING,\n"
        "  flag BOOLEAN OPTIONAL\n"
        "}\n"
        "END\n";

    FILE *fp = fopen("./dummy_file", "w");
    if (fp) {
        fwrite(asn1_def, 1, strlen(asn1_def), fp);
        fclose(fp);
    } else {
        return;
    }

    char errorDescription[ASN1_MAX_ERROR_DESCRIPTION_SIZE];
    int res = asn1_parser2tree("./dummy_file", &definitions, errorDescription);
    if (res != ASN1_SUCCESS) {
        definitions = nullptr;
    }
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size)
{
    if (!init_attempted) {
        init_definitions();
    }

    if (!definitions) {
        return 0;
    }

    if (Size > static_cast<size_t>(INT_MAX)) {
        return 0;
    }

    asn1_node element = nullptr;
    int res = asn1_create_element(definitions, "PKIX1.Test", &element);
    if (res != ASN1_SUCCESS || element == nullptr) {
        return 0;
    }

    char errorDescription[ASN1_MAX_ERROR_DESCRIPTION_SIZE];
    memset(errorDescription, 0, sizeof(errorDescription));

    asn1_der_decoding(&element, Data, static_cast<int>(Size), errorDescription);

    asn1_delete_structure(&element);

    return 0;
}