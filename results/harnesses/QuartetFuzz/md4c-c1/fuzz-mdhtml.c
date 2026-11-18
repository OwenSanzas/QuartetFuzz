#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "md4c.h"
#include "md4c-html.h"

struct membuffer {
    char* data;
    size_t size;
    size_t alloc;
};

static void
membuf_init(struct membuffer* buf)
{
    buf->data = NULL;
    buf->size = 0;
    buf->alloc = 0;
}

static void
membuf_fini(struct membuffer* buf)
{
    free(buf->data);
    buf->data = NULL;
    buf->size = 0;
    buf->alloc = 0;
}

static void
membuf_append(struct membuffer* buf, const char* text, size_t size)
{
    if(size == 0)
        return;

    if(buf->size + size > buf->alloc) {
        size_t new_alloc = (buf->alloc > 0) ? buf->alloc : 256;
        while(new_alloc < buf->size + size)
            new_alloc *= 2;
        char* new_data = (char*) realloc(buf->data, new_alloc);
        if(new_data == NULL) {

            return;
        }
        buf->data = new_data;
        buf->alloc = new_alloc;
    }

    memcpy(buf->data + buf->size, text, size);
    buf->size += size;
}

static void
process_output(const MD_CHAR* text, MD_SIZE size, void* userdata)
{
    membuf_append((struct membuffer*) userdata, text, (size_t) size);
}

int
LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{

    if(size < 8)
        return 0;

    unsigned parser_flags;
    unsigned renderer_flags;
    memcpy(&parser_flags, data, 4);
    memcpy(&renderer_flags, data + 4, 4);

    const uint8_t* md_input = data + 8;
    size_t md_input_size = size - 8;

    struct membuffer out;
    membuf_init(&out);

    int ret = md_html((const MD_CHAR*) md_input, (MD_SIZE) md_input_size,
                       process_output, (void*) &out,
                       parser_flags, renderer_flags);

    if(ret == 0) {

        volatile size_t checksum = 0;
        for(size_t i = 0; i < out.size; i++)
            checksum += (unsigned char) out.data[i];
        (void) checksum;
    }

    membuf_fini(&out);

    return 0;
}
