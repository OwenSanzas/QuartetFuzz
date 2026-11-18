#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <cstdint>
#include <cstddef>
#include <uriparser/Uri.h>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

static void fuzzParseAndManipulate(const std::string &input) {
    UriParserStateA state;
    UriUriA uri;
    std::memset(&uri, 0, sizeof(uri));
    state.uri = &uri;

    std::vector<char> buffer(input.begin(), input.end());
    buffer.push_back('\0');

    int parseResult = uriParseUriA(&state, buffer.data());
    if (parseResult != URI_SUCCESS) {
        uriFreeUriMembersA(&uri);
        return;
    }

    int charsRequired = 0;
    if (uriToStringCharsRequiredA(&uri, &charsRequired) == URI_SUCCESS && charsRequired >= 0) {
        std::vector<char> toStringBuf(charsRequired + 1, '\0');
        int charsWritten = 0;
        uriToStringA(toStringBuf.data(), &uri, charsRequired + 1, &charsWritten);
    }

    unsigned int dirtyMask = uriNormalizeSyntaxMaskRequiredA(&uri);
    if (dirtyMask != 0) {
        uriNormalizeSyntaxExA(&uri, dirtyMask);
    }

    if (uri.query.first != nullptr && uri.query.afterLast != nullptr &&
        uri.query.afterLast >= uri.query.first) {
        UriQueryListA *queryList = nullptr;
        int itemCount = 0;
        int dissectResult = uriDissectQueryMallocA(&queryList, &itemCount,
                                                    uri.query.first,
                                                    uri.query.afterLast);
        if (dissectResult == URI_SUCCESS && queryList != nullptr) {
            int charsRequiredQuery = 0;
            if (uriComposeQueryCharsRequiredA(queryList, &charsRequiredQuery) == URI_SUCCESS &&
                charsRequiredQuery >= 0) {
                std::vector<char> composeBuf(charsRequiredQuery + 1, '\0');
                int charsWrittenQuery = 0;
                uriComposeQueryA(composeBuf.data(), queryList, charsRequiredQuery + 1,
                                  &charsWrittenQuery);
            }
            uriFreeQueryListA(queryList);
        }
    }

    {
        const char *baseUriStr = "http://example.com/base/path/";
        UriUriA baseUri;
        std::memset(&baseUri, 0, sizeof(baseUri));
        UriParserStateA baseState;
        baseState.uri = &baseUri;
        if (uriParseUriA(&baseState, baseUriStr) == URI_SUCCESS) {
            UriUriA absoluteDest;
            std::memset(&absoluteDest, 0, sizeof(absoluteDest));
            if (uriAddBaseUriA(&absoluteDest, &uri, &baseUri) == URI_SUCCESS) {
                int charsRequiredAbs = 0;
                if (uriToStringCharsRequiredA(&absoluteDest, &charsRequiredAbs) == URI_SUCCESS &&
                    charsRequiredAbs >= 0) {
                    std::vector<char> absBuf(charsRequiredAbs + 1, '\0');
                    int charsWrittenAbs = 0;
                    uriToStringA(absBuf.data(), &absoluteDest, charsRequiredAbs + 1,
                                  &charsWrittenAbs);
                }
            }
            uriFreeUriMembersA(&absoluteDest);

            UriUriA relativeDest;
            std::memset(&relativeDest, 0, sizeof(relativeDest));
            if (uriRemoveBaseUriA(&relativeDest, &uri, &baseUri, URI_FALSE) == URI_SUCCESS) {
                int charsRequiredRel = 0;
                if (uriToStringCharsRequiredA(&relativeDest, &charsRequiredRel) == URI_SUCCESS &&
                    charsRequiredRel >= 0) {
                    std::vector<char> relBuf(charsRequiredRel + 1, '\0');
                    int charsWrittenRel = 0;
                    uriToStringA(relBuf.data(), &relativeDest, charsRequiredRel + 1,
                                  &charsWrittenRel);
                }
            }
            uriFreeUriMembersA(&relativeDest);
        }
        uriFreeUriMembersA(&baseUri);
    }

    uriEqualsUriA(&uri, &uri);

    uriFreeUriMembersA(&uri);
}

static void fuzzFileUriConversion(const std::string &input) {

    std::vector<char> filenameBuf(input.begin(), input.end());
    filenameBuf.push_back('\0');

    std::vector<char> uriStr(input.size() * 3 + 10, '\0');
    if (uriUnixFilenameToUriStringA(filenameBuf.data(), uriStr.data()) == URI_SUCCESS) {
        std::vector<char> filenameOut(uriStr.size() + 10, '\0');
        uriUriStringToUnixFilenameA(uriStr.data(), filenameOut.data());
    }

    std::vector<char> winUriStr(input.size() * 3 + 10, '\0');
    if (uriWindowsFilenameToUriStringA(filenameBuf.data(), winUriStr.data()) == URI_SUCCESS) {
        std::vector<char> filenameOutWin(winUriStr.size() + 10, '\0');
        uriUriStringToWindowsFilenameA(winUriStr.data(), filenameOutWin.data());
    }
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size) {
    if (Data == nullptr || Size == 0) {
        return 0;
    }

    std::string input(reinterpret_cast<const char *>(Data), Size);

    fuzzParseAndManipulate(input);

    if (Size < 4096) {
        fuzzFileUriConversion(input);
    }

    return 0;
}