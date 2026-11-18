#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <cstdint>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <fstream>

#include <uriparser/Uri.h>

static void safeFreeUri(UriUriA *uri) {
    if (uri) {
        uriFreeUriMembersA(uri);
    }
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size) {
    if (Size == 0) {
        return 0;
    }

    std::string input(reinterpret_cast<const char *>(Data), Size);

    {
        std::ofstream ofs("./dummy_file", std::ios::binary);
        if (ofs) {
            ofs.write(reinterpret_cast<const char *>(Data), Size);
        }
    }

    UriUriA uri;
    std::memset(&uri, 0, sizeof(uri));
    UriParserStateA state;
    state.uri = &uri;

    int parseResult = uriParseUriA(&state, input.c_str());

    if (parseResult == URI_SUCCESS) {

        int charsRequired = 0;
        if (uriToStringCharsRequiredA(&uri, &charsRequired) == URI_SUCCESS) {
            if (charsRequired >= 0) {
                std::string buffer;
                buffer.resize(static_cast<size_t>(charsRequired) + 1);
                int written = 0;
                uriToStringA(&buffer[0], &uri, static_cast<int>(buffer.size()), &written);
            }
        }

        UriUriA normUri;
        std::memset(&normUri, 0, sizeof(normUri));
        UriParserStateA normState;
        normState.uri = &normUri;
        if (uriParseUriA(&normState, input.c_str()) == URI_SUCCESS) {
            unsigned int normMask = uriNormalizeSyntaxMaskRequiredA(&normUri);
            uriNormalizeSyntaxExA(&normUri, normMask);

            int normCharsRequired = 0;
            if (uriToStringCharsRequiredA(&normUri, &normCharsRequired) == URI_SUCCESS) {
                if (normCharsRequired >= 0) {
                    std::string normBuffer;
                    normBuffer.resize(static_cast<size_t>(normCharsRequired) + 1);
                    int normWritten = 0;
                    uriToStringA(&normBuffer[0], &normUri, static_cast<int>(normBuffer.size()), &normWritten);
                }
            }
        }
        safeFreeUri(&normUri);

        UriUriA baseUri;
        std::memset(&baseUri, 0, sizeof(baseUri));
        UriParserStateA baseState;
        baseState.uri = &baseUri;
        const char *baseStr = "http://example.com/base/path/";
        if (uriParseUriA(&baseState, baseStr) == URI_SUCCESS) {
            UriUriA absUri;
            std::memset(&absUri, 0, sizeof(absUri));
            if (uriAddBaseUriA(&absUri, &uri, &baseUri) == URI_SUCCESS) {
                int absCharsRequired = 0;
                if (uriToStringCharsRequiredA(&absUri, &absCharsRequired) == URI_SUCCESS) {
                    if (absCharsRequired >= 0) {
                        std::string absBuffer;
                        absBuffer.resize(static_cast<size_t>(absCharsRequired) + 1);
                        int absWritten = 0;
                        uriToStringA(&absBuffer[0], &absUri, static_cast<int>(absBuffer.size()), &absWritten);
                    }
                }

                UriUriA relUri;
                std::memset(&relUri, 0, sizeof(relUri));
                if (uriRemoveBaseUriA(&relUri, &absUri, &baseUri, URI_FALSE) == URI_SUCCESS) {
                    int relCharsRequired = 0;
                    if (uriToStringCharsRequiredA(&relUri, &relCharsRequired) == URI_SUCCESS) {
                        if (relCharsRequired >= 0) {
                            std::string relBuffer;
                            relBuffer.resize(static_cast<size_t>(relCharsRequired) + 1);
                            int relWritten = 0;
                            uriToStringA(&relBuffer[0], &relUri, static_cast<int>(relBuffer.size()), &relWritten);
                        }
                    }
                }
                safeFreeUri(&relUri);
            }
            safeFreeUri(&absUri);
        }
        safeFreeUri(&baseUri);

        UriUriA cmpUri;
        std::memset(&cmpUri, 0, sizeof(cmpUri));
        UriParserStateA cmpState;
        cmpState.uri = &cmpUri;
        if (uriParseUriA(&cmpState, input.c_str()) == URI_SUCCESS) {
            uriEqualsUriA(&uri, &cmpUri);
        }
        safeFreeUri(&cmpUri);
    }

    safeFreeUri(&uri);

    {
        size_t fileNameBufferLen = input.length() + 1;
        if (fileNameBufferLen < 1) {
            fileNameBufferLen = 1;
        }
        std::string unixFileName;
        unixFileName.resize(fileNameBufferLen);
        uriUriStringToUnixFilenameA(input.c_str(), &unixFileName[0]);
    }

    {
        size_t fileNameBufferLen = input.length() + 1;
        if (fileNameBufferLen < 1) {
            fileNameBufferLen = 1;
        }
        std::string winFileName;
        winFileName.resize(fileNameBufferLen);
        uriUriStringToWindowsFilenameA(input.c_str(), &winFileName[0]);
    }

    {
        size_t uriBufferLen = (3 * input.length()) + 8;
        if (uriBufferLen < 8) {
            uriBufferLen = 8;
        }
        std::string uriFromFile;
        uriFromFile.resize(uriBufferLen);
        uriUnixFilenameToUriStringA(input.c_str(), &uriFromFile[0]);
    }

    {
        size_t uriBufferLen = (3 * input.length()) + 8;
        if (uriBufferLen < 8) {
            uriBufferLen = 8;
        }
        std::string uriFromFile;
        uriFromFile.resize(uriBufferLen);
        uriWindowsFilenameToUriStringA(input.c_str(), &uriFromFile[0]);
    }

    {
        UriQueryListA *queryList = nullptr;
        int itemCount = 0;
        const char *errorPos = nullptr;
        if (uriDissectQueryMallocA(&queryList, &itemCount, input.c_str(),
                                   input.c_str() + input.length()) == URI_SUCCESS) {
            if (queryList != nullptr) {
                int charsRequired = 0;
                if (uriComposeQueryCharsRequiredA(queryList, &charsRequired) == URI_SUCCESS) {
                    if (charsRequired >= 0) {
                        std::string composedQuery;
                        composedQuery.resize(static_cast<size_t>(charsRequired) + 1);
                        int written = 0;
                        uriComposeQueryA(&composedQuery[0], queryList,
                                          static_cast<int>(composedQuery.size()), &written);
                    }
                }
                uriFreeQueryListA(queryList);
            }
        }
    }

    return 0;
}