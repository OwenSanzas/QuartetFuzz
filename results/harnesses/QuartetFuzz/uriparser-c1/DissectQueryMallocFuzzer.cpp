#include "uriparser/Uri.h"
#include "FuzzingUtils.h"
#include <fuzzer/FuzzedDataProvider.h>
#include <cstdint>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace {

struct HarnessMemCtx {
    bool failAlways;
};

void * harnessMalloc(UriMemoryManager * memory, size_t size) {
    HarnessMemCtx * ctx = static_cast<HarnessMemCtx *>(memory->userData);
    if (ctx->failAlways) {
        return NULL;
    }
    return malloc(size);
}

void * harnessCalloc(UriMemoryManager * memory, size_t nmemb, size_t size) {
    HarnessMemCtx * ctx = static_cast<HarnessMemCtx *>(memory->userData);
    if (ctx->failAlways) {
        return NULL;
    }
    return calloc(nmemb, size);
}

void * harnessRealloc(UriMemoryManager * memory, void * ptr, size_t size) {
    HarnessMemCtx * ctx = static_cast<HarnessMemCtx *>(memory->userData);
    if (ctx->failAlways) {
        return NULL;
    }
    return realloc(ptr, size);
}

void * harnessReallocarray(
        UriMemoryManager * memory, void * ptr, size_t nmemb, size_t size) {

    return uriEmulateReallocarray(memory, ptr, nmemb, size);
}

void harnessFree(UriMemoryManager * memory, void * ptr) {
    (void)memory;
    free(ptr);
}

UriBreakConversion PickBreakConversion(uint8_t v) {
    switch (v % 4) {
        case 0:
            return URI_BR_TO_LF;
        case 1:
            return URI_BR_TO_CRLF;
        case 2:
            return URI_BR_TO_CR;
        default:
            return URI_BR_DONT_TOUCH;
    }
}

void UseAndFreeQueryList(URI_TYPE(QueryList) * list, UriMemoryManager * memory,
        UriBool spaceToPlus, UriBool normalizeBreaks) {
    size_t totalKeyLen = 0;
    size_t totalValueLen = 0;
    for (URI_TYPE(QueryList) * node = list; node != NULL; node = node->next) {
        if (node->key != NULL) {
            totalKeyLen += URI_STRLEN(node->key);
        }
        if (node->value != NULL) {
            totalValueLen += URI_STRLEN(node->value);
        }
    }
    (void)totalKeyLen;
    (void)totalValueLen;

    if (list != NULL) {
        int charsRequired = 0;
        if (URI_FUNC(ComposeQueryCharsRequiredEx)(
                    list, &charsRequired, spaceToPlus, normalizeBreaks)
                == URI_SUCCESS) {
            std::vector<URI_CHAR> buf(static_cast<size_t>(charsRequired) + 1);
            int charsWritten = 0;
            URI_FUNC(ComposeQueryEx)(&buf[0], list, static_cast<int>(buf.size()),
                    &charsWritten, spaceToPlus, normalizeBreaks);
        }

        URI_CHAR * composed = NULL;
        if (URI_FUNC(ComposeQueryMallocExMm)(
                    &composed, list, spaceToPlus, normalizeBreaks, memory)
                        == URI_SUCCESS
                && composed != NULL) {
            if (memory == NULL) {
                free(composed);
            } else {
                memory->free(memory, composed);
            }
        }
    }

    if (memory == NULL) {
        URI_FUNC(FreeQueryList)(list);
    } else {
        URI_FUNC(FreeQueryListMm)(list, memory);
    }
}

}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t * data, size_t size) {
    if (size < 6) {
        return 0;
    }

    FuzzedDataProvider fdp(data, size);

    const uint8_t variantSelector = fdp.ConsumeIntegral<uint8_t>();
    const uint8_t plusToSpaceByte = fdp.ConsumeIntegral<uint8_t>();
    const uint8_t breakConvByte = fdp.ConsumeIntegral<uint8_t>();
    const uint8_t memSelector = fdp.ConsumeIntegral<uint8_t>();
    const uint8_t spaceToPlusByte = fdp.ConsumeIntegral<uint8_t>();
    const uint8_t normalizeBreaksByte = fdp.ConsumeIntegral<uint8_t>();

    const UriString query = consumeRemainingBytesAsString(fdp);
    const URI_CHAR * first = query.c_str();
    const URI_CHAR * afterLast = first + query.size();

    const UriBool spaceToPlus = (spaceToPlusByte & 1) ? URI_TRUE : URI_FALSE;
    const UriBool normalizeBreaks = (normalizeBreaksByte & 1) ? URI_TRUE : URI_FALSE;

    URI_TYPE(QueryList) * mainList = NULL;
    int mainItemCount = 0;
    int mainRes = URI_FUNC(DissectQueryMalloc)(&mainList, &mainItemCount, first, afterLast);
    if (mainRes == URI_SUCCESS) {
        UseAndFreeQueryList(mainList, NULL, spaceToPlus, normalizeBreaks);
    }

    const UriBool plusToSpace = (plusToSpaceByte & 1) ? URI_TRUE : URI_FALSE;
    const UriBreakConversion breakConversion = PickBreakConversion(breakConvByte);

    switch (variantSelector % 3) {
        case 0:

            break;

        case 1: {
            URI_TYPE(QueryList) * list = NULL;
            int itemCount = 0;
            int res = URI_FUNC(DissectQueryMallocEx)(
                    &list, &itemCount, first, afterLast, plusToSpace, breakConversion);
            if (res == URI_SUCCESS) {
                UseAndFreeQueryList(list, NULL, spaceToPlus, normalizeBreaks);
            }
            break;
        }

        case 2: {
            HarnessMemCtx ctx;
            ctx.failAlways = (memSelector & 1) != 0;
            UriMemoryManager customMemory;
            customMemory.malloc = harnessMalloc;
            customMemory.calloc = harnessCalloc;
            customMemory.realloc = harnessRealloc;
            customMemory.reallocarray = harnessReallocarray;
            customMemory.free = harnessFree;
            customMemory.userData = &ctx;

            URI_TYPE(QueryList) * list = NULL;
            int itemCount = 0;
            int res = URI_FUNC(DissectQueryMallocExMm)(&list, &itemCount, first, afterLast,
                    plusToSpace, breakConversion, &customMemory);
            if (res == URI_SUCCESS) {
                UseAndFreeQueryList(list, &customMemory, spaceToPlus, normalizeBreaks);
            }
            break;
        }
    }

    return 0;
}
