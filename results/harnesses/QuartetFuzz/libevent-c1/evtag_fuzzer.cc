#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <vector>

#include <event2/buffer.h>
#include <event2/tag.h>
#include <event2/util.h>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
	if (size < 1)
		return 0;

	uint8_t selector = data[0];
	const uint8_t *payload = data + 1;
	size_t payload_size = size - 1;

	struct evbuffer *src = evbuffer_new();
	struct evbuffer *dst = evbuffer_new();
	if (!src || !dst) {
		if (src) evbuffer_free(src);
		if (dst) evbuffer_free(dst);
		return 0;
	}

	if ((selector & 0x08) && payload_size >= 4) {

		ev_uint32_t mtag;
		memcpy(&mtag, payload, 4);
		const uint8_t *mpayload = payload + 4;
		size_t mpayload_size = payload_size - 4;

		switch ((selector >> 4) % 4) {
		case 0:
			evtag_marshal(src, mtag, mpayload, (ev_uint32_t)mpayload_size);
			break;
		case 1: {
			struct evbuffer *tmp = evbuffer_new();
			if (tmp) {
				evbuffer_add(tmp, mpayload, mpayload_size);
				evtag_marshal_buffer(src, mtag, tmp);
				evbuffer_free(tmp);
			}
			break;
		}
		case 2: {
			std::vector<char> str(mpayload, mpayload + mpayload_size);
			str.push_back('\0');
			evtag_marshal_string(src, mtag, str.data());
			break;
		}
		case 3: {
			struct timeval tv;
			memset(&tv, 0, sizeof(tv));
			if (mpayload_size >= 8) {
				int32_t sec, usec;
				memcpy(&sec, mpayload, 4);
				memcpy(&usec, mpayload + 4, 4);
				tv.tv_sec = sec;
				tv.tv_usec = usec;
			}
			evtag_marshal_timeval(src, mtag, &tv);
			break;
		}
		}
	} else {

		evbuffer_add(src, payload, payload_size);
	}

	size_t src_len = evbuffer_get_length(src);
	std::vector<unsigned char> src_copy(src_len);
	if (src_len > 0)
		memcpy(src_copy.data(), evbuffer_pullup(src, src_len), src_len);

	ev_uint32_t tag = 0;
	int len = evtag_unmarshal(src, &tag, dst);
	if (len >= 0) {

		size_t dst_len = evbuffer_get_length(dst);
		if (dst_len > 0) {
			unsigned char *p =
			    (unsigned char *)evbuffer_pullup(dst, dst_len);
			if (p) {
				volatile unsigned char sum = 0;
				for (size_t i = 0; i < dst_len; i++)
					sum ^= p[i];
				(void)sum;
			}
		}

		switch ((selector >> 6) & 0x3) {
		case 0: {
			ev_uint32_t v = 0;
			evtag_unmarshal_int(dst, tag, &v);
			break;
		}
		case 1: {
			ev_uint64_t v = 0;
			evtag_unmarshal_int64(dst, tag, &v);
			break;
		}
		case 2: {
			char *s = NULL;
			if (evtag_unmarshal_string(dst, tag, &s) == 0 &&
			    s != NULL) {
				volatile size_t l = strlen(s);
				(void)l;
				free(s);
			}
			break;
		}
		case 3: {
			struct timeval tv;
			memset(&tv, 0, sizeof(tv));
			evtag_unmarshal_timeval(dst, tag, &tv);
			break;
		}
		}
	}
	evbuffer_free(dst);

	struct evbuffer *src2 = evbuffer_new();
	if (src2) {
		if (!src_copy.empty())
			evbuffer_add(src2, src_copy.data(), src_copy.size());
		ev_uint32_t need_tag = tag;

		switch (selector % 5) {
		case 0: {
			ev_uint32_t pint = 0;
			evtag_unmarshal_int(src2, need_tag, &pint);
			break;
		}
		case 1: {
			ev_uint64_t pint64 = 0;
			evtag_unmarshal_int64(src2, need_tag, &pint64);
			break;
		}
		case 2: {
			char fixedbuf[64];
			evtag_unmarshal_fixed(src2, need_tag, fixedbuf,
			    sizeof(fixedbuf));
			break;
		}
		case 3: {
			char *pstring = NULL;
			if (evtag_unmarshal_string(src2, need_tag, &pstring) ==
				0 &&
			    pstring != NULL) {
				volatile size_t l = strlen(pstring);
				(void)l;
				free(pstring);
			}
			break;
		}
		case 4: {
			struct timeval tv;
			memset(&tv, 0, sizeof(tv));
			evtag_unmarshal_timeval(src2, need_tag, &tv);
			break;
		}
		}
		evbuffer_free(src2);
	}

	evbuffer_free(src);
	return 0;
}
