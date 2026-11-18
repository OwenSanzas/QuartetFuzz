#include "uriparser/Uri.h"
#include "FuzzingUtils.h"
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace {

struct FailingAllocatorState {
    uint32_t failEvery;
    uint32_t counter;
};

void * FailingMalloc(UriMemoryManager * manager, size_t size) {
    auto * st = static_cast<FailingAllocatorState *>(manager->userData);
    if (st->failEvery != 0 && (++st->counter % st->failEvery) == 0) {
        return nullptr;
    }
    return malloc(size);
}

void * FailingCalloc(UriMemoryManager * manager, size_t nmemb, size_t size) {
    auto * st = static_cast<FailingAllocatorState *>(manager->userData);
    if (st->failEvery != 0 && (++st->counter % st->failEvery) == 0) {
        return nullptr;
    }
    return calloc(nmemb, size);
}

void * FailingRealloc(UriMemoryManager *, void * ptr, size_t size) {
    return realloc(ptr, size);
}

void * FailingReallocarray(UriMemoryManager *, void * ptr, size_t nmemb, size_t size) {
    if (nmemb != 0 && size > static_cast<size_t>(-1) / nmemb) {
        return nullptr;
    }
    return realloc(ptr, nmemb * size);
}

void FailingFree(UriMemoryManager *, void * ptr) {
    free(ptr);
}

UriMemoryManager MakeFailingManager(FailingAllocatorState * state) {
    UriMemoryManager manager;
    manager.malloc = FailingMalloc;
    manager.calloc = FailingCalloc;
    manager.realloc = FailingRealloc;
    manager.reallocarray = FailingReallocarray;
    manager.free = FailingFree;
    manager.userData = state;
    return manager;
}

}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t * data, size_t size) {
    FuzzedDataProvider stream(data, size);

    enum Strategy {
        kNone = 0,
        kMakeOwner,
        kNormalize,
        kCopy,
        kMakeOwnerThenNormalize,

        kCopyThenNormalize,
        kStrategyCount
    };
    const int mode = stream.ConsumeIntegralInRange<int>(0, kStrategyCount - 1);

    const uint32_t failEvery = stream.ConsumeIntegralInRange<uint32_t>(0, 32);

    const UriString uri1 = consumeRemainingBytesAsString(stream);

    URI_TYPE(Uri) uri;
    memset((void *)&uri, 0, sizeof(uri));

    URI_TYPE(ParserState) state;
    state.uri = &uri;

    const int parseResult = URI_FUNC(ParseUri)(&state, uri1.c_str());

    FailingAllocatorState failState{failEvery, 0};
    UriMemoryManager failingManager = MakeFailingManager(&failState);

    if (parseResult == URI_SUCCESS) {
        switch (mode) {
            case kMakeOwner:

                URI_FUNC(MakeOwnerMm)(&uri, &failingManager);
                break;
            case kNormalize:

                URI_FUNC(NormalizeSyntaxExMm)(
                        &uri, static_cast<unsigned int>(-1), &failingManager);
                break;
            case kCopy: {

                URI_TYPE(Uri) uriCopy;
                memset((void *)&uriCopy, 0, sizeof(uriCopy));
                URI_FUNC(CopyUriMm)(&uriCopy, &uri, &failingManager);

                URI_FUNC(FreeUriMembers)(&uriCopy);
                break;
            }
            case kMakeOwnerThenNormalize:

                URI_FUNC(MakeOwnerMm)(&uri, &failingManager);
                URI_FUNC(NormalizeSyntaxExMm)(
                        &uri, static_cast<unsigned int>(-1), &failingManager);
                break;
            case kCopyThenNormalize: {

                URI_TYPE(Uri) uriCopy;
                memset((void *)&uriCopy, 0, sizeof(uriCopy));
                if (URI_FUNC(CopyUriMm)(&uriCopy, &uri, &failingManager) == URI_SUCCESS) {
                    URI_FUNC(NormalizeSyntaxExMm)(
                            &uriCopy, static_cast<unsigned int>(-1), &failingManager);
                }
                URI_FUNC(FreeUriMembers)(&uriCopy);
                break;
            }
            default:

                break;
        }
    }

    URI_FUNC(FreeUriMembers)(NULL);

    UriMemoryManager incompleteManager = failingManager;
    incompleteManager.malloc = NULL;

    URI_FUNC(FreeUriMembersMm)(&uri, &incompleteManager);

    URI_FUNC(FreeUriMembers)(&uri);

    return 0;
}