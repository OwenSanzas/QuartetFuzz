#include "uriparser/Uri.h"
#include "FuzzingUtils.h"
#include <fuzzer/FuzzedDataProvider.h>
#include <cstdint>
#include <cstring>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t * data, size_t size) {
    FuzzedDataProvider stream(data, size);

    bool domainRelative = stream.ConsumeBool();
    bool useExVariant = stream.ConsumeBool();
    bool doMakeOwner = stream.ConsumeBool();
    bool doNormalize = stream.ConsumeBool();
    bool doCopy = stream.ConsumeBool();
    unsigned int normalizeMask = stream.ConsumeIntegral<unsigned int>();

    const UriString uriText1 = consumeRandomLengthString(stream);
    const UriString uriText2 = consumeRemainingBytesAsString(stream);

    URI_TYPE(Uri) uri1;
    memset((void *)&uri1, 0, sizeof(uri1));
    URI_TYPE(ParserState) state1;
    state1.uri = &uri1;
    int res1 = URI_FUNC(ParseUri)(&state1, uriText1.c_str());

    if (res1 == URI_SUCCESS) {

        URI_CHAR buf[1024 * 8] = {0};
        int written = 0;
        URI_FUNC(ToString)(buf, &uri1, sizeof(buf) / sizeof(buf[0]), &written);
    }

    URI_TYPE(Uri) uri2;
    memset((void *)&uri2, 0, sizeof(uri2));
    int res2;
    if (useExVariant) {
        const URI_CHAR * first = uriText2.c_str();
        const URI_CHAR * afterLast = first + uriText2.size();
        res2 = URI_FUNC(ParseSingleUriEx)(&uri2, first, afterLast, nullptr);
    } else {
        res2 = URI_FUNC(ParseSingleUri)(&uri2, uriText2.c_str(), nullptr);
    }

    if ((res1 == URI_SUCCESS) && (res2 == URI_SUCCESS)) {

        URI_FUNC(EqualsUri)(&uri1, &uri2);

        if (doNormalize) {
            unsigned int requiredMask = 0;
            URI_FUNC(NormalizeSyntaxMaskRequiredEx)(&uri1, &requiredMask);
            if (normalizeMask != 0) {
                URI_FUNC(NormalizeSyntaxEx)(&uri1, normalizeMask);
            } else {
                URI_FUNC(NormalizeSyntax)(&uri1);
            }
        }

        if (doMakeOwner) {
            URI_FUNC(MakeOwner)(&uri2);
        }

        if (doCopy) {
            URI_TYPE(Uri) copyUri;
            int copyRes = URI_FUNC(CopyUri)(&copyUri, &uri2);
            if (copyRes == URI_SUCCESS) {
                URI_CHAR buf2[1024 * 4] = {0};
                int written2 = 0;
                URI_FUNC(ToString)(buf2, &copyUri, sizeof(buf2) / sizeof(buf2[0]), &written2);
            }
            URI_FUNC(FreeUriMembers)(&copyUri);
        }

        URI_TYPE(Uri) absUri;
        if (URI_FUNC(AddBaseUri)(&absUri, &uri2, &uri1) == URI_SUCCESS) {
            URI_CHAR buf3[1024 * 4] = {0};
            int written3 = 0;
            URI_FUNC(ToString)(buf3, &absUri, sizeof(buf3) / sizeof(buf3[0]), &written3);
        }
        URI_FUNC(FreeUriMembers)(&absUri);

        URI_TYPE(Uri) relUri;
        if (URI_FUNC(RemoveBaseUri)(&relUri, &uri1, &uri2, domainRelative ? URI_TRUE : URI_FALSE)
                == URI_SUCCESS) {
            URI_CHAR buf4[1024 * 4] = {0};
            int written4 = 0;
            URI_FUNC(ToString)(buf4, &relUri, sizeof(buf4) / sizeof(buf4[0]), &written4);
        }
        URI_FUNC(FreeUriMembers)(&relUri);
    }

    URI_FUNC(FreeUriMembers)(&uri1);
    URI_FUNC(FreeUriMembers)(&uri2);

    return 0;
}
