#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <avahi-common/strlst.h>

int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size) {
    AvahiStringList *list = NULL;
    int ret;

    ret = avahi_string_list_parse(Data, Size, &list);
    if (ret == 0 && list != NULL) {
        avahi_string_list_free(list);
        list = NULL;
    }

    if (Size > 1) {
        AvahiStringList *list2 = NULL;
        ret = avahi_string_list_parse(Data, Size - 1, &list2);
        if (ret == 0 && list2 != NULL) {
            avahi_string_list_free(list2);
            list2 = NULL;
        }
    }

    if (Size > 0) {
        uint8_t *copy = (uint8_t *)malloc(Size);
        if (copy) {
            memcpy(copy, Data, Size);
            AvahiStringList *list3 = NULL;
            ret = avahi_string_list_parse(copy, Size, &list3);
            if (ret == 0 && list3 != NULL) {
                avahi_string_list_free(list3);
                list3 = NULL;
            }
            free(copy);
        }
    }

    {
        uint8_t dummy = 0;
        AvahiStringList *list4 = NULL;
        ret = avahi_string_list_parse(&dummy, 0, &list4);
        if (ret == 0 && list4 != NULL) {
            avahi_string_list_free(list4);
            list4 = NULL;
        }
    }

    return 0;
}