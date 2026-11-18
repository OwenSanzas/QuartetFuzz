#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include <event2/util.h>

#ifdef __cplusplus
extern "C" {
#endif

const char *evutil_format_sockaddr_port_(const struct sockaddr *sa,
    char *out, size_t outlen);
#ifdef __cplusplus
}
#endif

static int parse_and_use(const char *str, struct sockaddr_storage *ss) {
	memset(ss, 0, sizeof(*ss));
	int outlen = (int)sizeof(*ss);
	int rv = evutil_parse_sockaddr_port(str, (struct sockaddr *)ss, &outlen);
	if (rv != 0)
		return 0;

	char cbuf[256];
	const char *cp = evutil_format_sockaddr_port_(
	    (struct sockaddr *)ss, cbuf, sizeof(cbuf));
	if (cp) {
		volatile char sink = 0;
		for (const char *p = cp; *p; ++p)
			sink ^= *p;
		(void)sink;
	}

	char nbuf[128];
	if (ss->ss_family == AF_INET) {
		struct sockaddr_in *sin = (struct sockaddr_in *)ss;
		evutil_inet_ntop(AF_INET, &sin->sin_addr, nbuf, sizeof(nbuf));
	} else if (ss->ss_family == AF_INET6) {
		struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *)ss;
		evutil_inet_ntop(AF_INET6, &sin6->sin6_addr, nbuf, sizeof(nbuf));
	}

	return 1;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
	if (size == 0 || size > 4096)
		return 0;

	uint8_t ctrl = data[size - 1];
	size_t payload_len = size - 1;

	char *str = (char *)malloc(payload_len + 1);
	if (!str)
		return 0;
	memcpy(str, data, payload_len);
	str[payload_len] = '\0';

	struct sockaddr_storage ss1;
	int ok1 = parse_and_use(str, &ss1);

	size_t split = payload_len ? (ctrl % payload_len) : 0;
	char *str2 = str + split;

	struct sockaddr_storage ss2;
	int ok2 = parse_and_use(str2, &ss2);

	if (ok1 && ok2) {

		int include_port = ctrl & 1;
		int cmp = evutil_sockaddr_cmp((struct sockaddr *)&ss1,
		    (struct sockaddr *)&ss2, include_port);
		volatile int sink = cmp;
		(void)sink;
	}

	struct in_addr v4;
	memset(&v4, 0, sizeof(v4));
	evutil_inet_pton(AF_INET, str, &v4);

	struct in6_addr v6;
	memset(&v6, 0, sizeof(v6));
	evutil_inet_pton(AF_INET6, str, &v6);

	struct in6_addr v6scope;
	memset(&v6scope, 0, sizeof(v6scope));
	unsigned idx = 0;
	int prv = evutil_inet_pton_scope(AF_INET6, str, &v6scope, &idx);
	if (prv == 1) {
		char zbuf[128];
		evutil_inet_ntop(AF_INET6, &v6scope, zbuf, sizeof(zbuf));
	}

	free(str);
	return 0;
}
