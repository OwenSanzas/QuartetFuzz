#include "uriparser/Uri.h"
#include "FuzzingUtils.h"
#include <cstddef>
#include <cstdint>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t * data, size_t size) {
    FuzzedDataProvider fdp(data, size);

    const uint8_t ctrl = fdp.ConsumeIntegralInRange<uint8_t>(0, 15);
    const bool useNullDest      = (ctrl == 0);
    const bool useNullItemCount = (ctrl == 1);

    const UriString query = consumeRemainingBytesAsString(fdp);

    const URI_CHAR * first = query.c_str();
    const URI_CHAR * afterLast = first + query.size();

    URI_TYPE(QueryList) * queryList = nullptr;
    int itemCount = 0;

    URI_TYPE(QueryList) ** destPtr = useNullDest ? nullptr : &queryList;
    int * itemCountPtr = useNullItemCount ? nullptr : &itemCount;

    int result = URI_FUNC(DissectQueryMalloc)(destPtr, itemCountPtr, first, afterLast);

    if (result == URI_SUCCESS && !useNullDest && queryList != nullptr) {
        URI_FUNC(FreeQueryList)(queryList);
    }

    return 0;
}