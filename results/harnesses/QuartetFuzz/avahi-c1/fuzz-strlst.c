#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

#include "avahi-common/strlst.h"
#include "avahi-common/malloc.h"

static void walk_and_pair(AvahiStringList *l, uint8_t selector) {
    AvahiStringList *cur;

    for (cur = l; cur; cur = avahi_string_list_get_next(cur)) {
        uint8_t *t = avahi_string_list_get_text(cur);
        size_t s = avahi_string_list_get_size(cur);
        size_t i;
        unsigned checksum = 0;
        if (t) {
            for (i = 0; i < s; i++)
                checksum += t[i];
        }
        (void) checksum;

        {
            char *k = NULL, *v = NULL;
            size_t vsize = 0;
            char **kp = (selector & 1) ? &k : NULL;
            char **vp = (selector & 2) ? &v : NULL;
            size_t *sp = (selector & 4) ? &vsize : NULL;

            if (avahi_string_list_get_pair(cur, kp, vp, sp) == 0) {
                if (k)
                    avahi_free(k);
                if (v)
                    avahi_free(v);
            }
        }

        selector = (uint8_t) (selector + 1);
    }
}

#ifdef __cplusplus
extern "C"
#endif
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    AvahiStringList *list = NULL;
    AvahiStringList *list2 = NULL;
    AvahiStringList *copy = NULL;
    char *text = NULL;
    void *buf = NULL;
    uint8_t selector = size > 0 ? data[0] : 0;

    if (avahi_string_list_parse(data, size, &list) < 0)
        return 0;

    {
        size_t needed = avahi_string_list_serialize(list, NULL, 0);
        if (needed > 0) {
            buf = malloc(needed);
            if (buf) {
                size_t used = avahi_string_list_serialize(list, buf, needed);
                if (avahi_string_list_parse(buf, used, &list2) == 0) {

                    avahi_string_list_equal(list, list2);
                }
                free(buf);
                buf = NULL;
            }
        }
    }

    text = avahi_string_list_to_string(list);
    if (text)
        avahi_free(text);
    text = NULL;

    copy = avahi_string_list_copy(list);
    avahi_string_list_equal(list, copy);
    if (copy) {
        char *copy_text = avahi_string_list_to_string(copy);
        if (copy_text)
            avahi_free(copy_text);
    }

    avahi_string_list_length(list);

    walk_and_pair(list, selector);

    if (size > 0) {
        size_t klen = size < 32 ? size : 32;
        char key[33];
        memcpy(key, data, klen);
        key[klen] = '\0';

        AvahiStringList *found = avahi_string_list_find(list, key);
        if (found) {
            char *k = NULL, *v = NULL;
            size_t vsize = 0;
            if (avahi_string_list_get_pair(found, &k, &v, &vsize) == 0) {
                if (k)
                    avahi_free(k);
                if (v)
                    avahi_free(v);
            }
        }
    }

    if (copy)
        avahi_string_list_free(copy);
    if (list2)
        avahi_string_list_free(list2);
    if (list)
        avahi_string_list_free(list);

    return 0;
}
