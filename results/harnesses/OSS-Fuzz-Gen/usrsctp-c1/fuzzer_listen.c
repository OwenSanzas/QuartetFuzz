#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <assert.h>
#include <time.h>
#include <usrsctp.h>
#include "../programs/programs_helper.h"

#define COMMON_HEADER_SIZE 12
#define MAX_PACKET_SIZE 65535

#define FUZZ_B_INJECT_INIT_ACK   (1 << 0)
#define FUZZ_B_INJECT_COOKIE_ACK (1 << 1)
#define FUZZ_B_SEND_DATA         (1 << 2)
#define FUZZ_B_SEND_STREAM_RESET (1 << 3)
#define FUZZ_B_INJECT_DATA       (1 << 4)
#define FUZZ_B_I_DATA_SUPPORT    (1 << 5)
#define FUZZ_B_SEND_DATA_FORCE   (1 << 6)
#define FUZZ_B_DISABLE_CRC       (1 << 7)

#define FUZZ2_B_AUTO_ASCONF         (1 << 0)
#define FUZZ2_B_AUTH_SUPPORTED      (1 << 1)
#define FUZZ2_B_ADD_IP_ADDRESS      (1 << 2)
#define FUZZ2_B_AUTH_KEY            (1 << 3)
#define FUZZ2_B_GRACEFUL_SHUTDOWN   (1 << 4)
#define FUZZ2_B_LISTENER_INIT       (1 << 5)
#define FUZZ2_B_BUNDLE_CHUNKS       (1 << 6)
#define FUZZ2_B_INJECT_SHUTDOWN_SEQ (1 << 7)

#define FUZZ3_SEL_MASK               0x0F
#define FUZZ3_SEL_NONE               0
#define FUZZ3_SEL_SACK               1
#define FUZZ3_SEL_NR_SACK            2
#define FUZZ3_SEL_HEARTBEAT_REQ      3
#define FUZZ3_SEL_HEARTBEAT_ACK      4
#define FUZZ3_SEL_ERROR              5
#define FUZZ3_SEL_ASCONF             6
#define FUZZ3_SEL_FORWARD_TSN        7
#define FUZZ3_SEL_RECONFIG           8
#define FUZZ3_SEL_AUTH               9
#define FUZZ3_SEL_ASCONF_ACK         10
#define FUZZ3_SEL_ABORT              11
#define FUZZ3_B_FRAGMENTED_DATA      (1 << 4)

#if !defined(SCTP_INTERLEAVING_SUPPORTED)
#define SCTP_INTERLEAVING_SUPPORTED 0x00001206
#endif

#ifndef SCTP_HEARTBEAT_INFO
#define SCTP_HEARTBEAT_INFO 0x0001
#endif
#ifndef SCTP_IPV4_ADDRESS
#define SCTP_IPV4_ADDRESS 0x0005
#endif
#ifndef SCTP_STATE_COOKIE_PARAM
#define SCTP_STATE_COOKIE_PARAM 0x0007
#endif
#ifndef SCTP_STR_RESET_OUT_REQUEST
#define SCTP_STR_RESET_OUT_REQUEST 0x000d
#endif
#ifndef SCTP_STR_RESET_IN_REQUEST
#define SCTP_STR_RESET_IN_REQUEST 0x000e
#endif
#ifndef SCTP_STR_RESET_ADD_OUT_STREAMS
#define SCTP_STR_RESET_ADD_OUT_STREAMS 0x0011
#endif
#ifndef SCTP_STR_RESET_ADD_IN_STREAMS
#define SCTP_STR_RESET_ADD_IN_STREAMS 0x0012
#endif
#ifndef SCTP_ADD_IP_ADDRESS
#define SCTP_ADD_IP_ADDRESS 0xc001
#endif
#ifndef SCTP_DEL_IP_ADDRESS
#define SCTP_DEL_IP_ADDRESS 0xc002
#endif
#ifndef SCTP_SET_PRIM_ADDR
#define SCTP_SET_PRIM_ADDR 0xc004
#endif

#define FUZZ_REMOTE_INITIAL_TSN 0xed6958ecUL

#if defined(__GNUC__) || defined(__clang__)
#define FUZZ_PACKED __attribute__((packed))
#else
#define FUZZ_PACKED
#endif

#define FUZZ_SCTP_ADDRMAX 16

struct fuzz_sctp_heartbeat_info_param {
	uint16_t param_type;
	uint16_t param_length;
	time_t   time_value_1;
	uint32_t time_value_2;
	uint32_t random_value1;
	uint32_t random_value2;
	uint8_t  addr_family;
	uint8_t  addr_len;
	uint8_t  padding[2];
	char     address[FUZZ_SCTP_ADDRMAX];
} FUZZ_PACKED;

struct fuzz_sctp_heartbeat_chunk {
	uint8_t  chunk_type;
	uint8_t  chunk_flags;
	uint16_t chunk_length;
	struct fuzz_sctp_heartbeat_info_param hb_info;
} FUZZ_PACKED;

static uint32_t assoc_vtag = 0;
static struct socket *g_listener_socket = NULL;

static uint8_t g_last_output[8192];
static size_t g_last_output_len = 0;

static uint8_t g_last_asconf_serial[4];
static int g_have_asconf_serial = 0;

static int
conn_output(void *addr, void *buf, size_t length, uint8_t tos, uint8_t set_df)
{
	struct sctp_init_chunk *init_chunk;
	const unsigned char *b = (const unsigned char *)buf;

	static const unsigned char client_init_prefix[8] =
	    { 0x13, 0x88, 0x13, 0x89, 0x00, 0x00, 0x00, 0x00 };

	(void)addr;
	(void)tos;
	(void)set_df;

	if (length > 0) {
		size_t tocopy = (length < sizeof(g_last_output)) ? length : sizeof(g_last_output);
		memcpy(g_last_output, buf, tocopy);
		g_last_output_len = tocopy;
	}

	if ((length >= (COMMON_HEADER_SIZE + 16)) &&
	    (memcmp(buf, client_init_prefix, sizeof(client_init_prefix)) == 0) &&
	    (b[COMMON_HEADER_SIZE] == SCTP_INITIATION)) {
		init_chunk = (struct sctp_init_chunk *)((char *)buf + COMMON_HEADER_SIZE);
		assoc_vtag = init_chunk->initiate_tag;
	}

	if ((length >= (COMMON_HEADER_SIZE + 8)) &&
	    (b[COMMON_HEADER_SIZE] == SCTP_ASCONF)) {
		memcpy(g_last_asconf_serial, b + COMMON_HEADER_SIZE + 4, 4);
		g_have_asconf_serial = 1;
	}

	return (0);
}

static void
handle_upcall(struct socket *sock, void *arg, int flgs)
{
	(void)arg;
	(void)flgs;
	int events = usrsctp_get_events(sock);

	while (events & SCTP_EVENT_READ) {
		char buf[4096];
		struct sockaddr_in addr;
		int flags = 0;
		socklen_t len = (socklen_t)sizeof(struct sockaddr_in);
		unsigned int infotype = 0;
		struct sctp_recvv_rn rn;
		socklen_t infolen = sizeof(struct sctp_recvv_rn);
		ssize_t n;

		memset(&rn, 0, sizeof(struct sctp_recvv_rn));
		n = usrsctp_recvv(sock, buf, sizeof(buf), (struct sockaddr *)&addr,
		                   &len, (void *)&rn, &infolen, &infotype, &flags);

		if (n <= 0) {
			break;
		}

		events = usrsctp_get_events(sock);
	}
}

static size_t
build_bundled_chunks(const uint8_t *in, size_t in_len, uint8_t *out, size_t out_cap)
{
	size_t out_off = 0;
	size_t in_off = 0;

	while ((in_off + 3 <= in_len) && (out_off + 4 <= out_cap)) {
		uint8_t type;
		uint8_t flags;
		uint8_t lenbyte;
		size_t avail;
		size_t body_len;
		uint16_t chunk_length;
		size_t pad;

		type = in[in_off++];
		flags = in[in_off++];
		lenbyte = in[in_off++];

		avail = in_len - in_off;
		body_len = (avail == 0) ? 0 : (size_t)(lenbyte % (avail > 255 ? 256 : (avail + 1)));
		if (body_len > avail) {
			body_len = avail;
		}
		if (out_off + 4 + body_len > out_cap) {
			if (out_off + 4 > out_cap) {
				break;
			}
			body_len = out_cap - out_off - 4;
		}

		chunk_length = (uint16_t)(4 + body_len);
		out[out_off++] = type;
		out[out_off++] = flags;
		out[out_off++] = (uint8_t)((chunk_length >> 8) & 0xff);
		out[out_off++] = (uint8_t)(chunk_length & 0xff);
		if (body_len > 0) {
			memcpy(out + out_off, in + in_off, body_len);
		}
		out_off += body_len;
		in_off += body_len;

		pad = (4 - (out_off % 4)) % 4;
		if (out_off + pad > out_cap) {
			break;
		}
		memset(out + out_off, 0, pad);
		out_off += pad;
	}

	return (out_off);
}

static size_t
build_tlv_params(const uint8_t *in, size_t in_len, uint8_t *out, size_t out_cap)
{
	size_t out_off = 0;
	size_t in_off = 0;

	while ((in_off + 3 <= in_len) && (out_off + 4 <= out_cap)) {
		uint16_t ptype;
		uint8_t lenbyte;
		size_t avail;
		size_t body_len;
		uint16_t plen;
		size_t pad;

		ptype = (uint16_t)(((uint16_t)in[in_off] << 8) | in[in_off + 1]);
		in_off += 2;
		lenbyte = in[in_off++];

		avail = in_len - in_off;
		body_len = (avail == 0) ? 0 : (size_t)(lenbyte % (avail > 255 ? 256 : (avail + 1)));
		if (body_len > avail) {
			body_len = avail;
		}
		if (out_off + 4 + body_len > out_cap) {
			if (out_off + 4 > out_cap) {
				break;
			}
			body_len = out_cap - out_off - 4;
		}

		plen = (uint16_t)(4 + body_len);
		out[out_off++] = (uint8_t)((ptype >> 8) & 0xff);
		out[out_off++] = (uint8_t)(ptype & 0xff);
		out[out_off++] = (uint8_t)((plen >> 8) & 0xff);
		out[out_off++] = (uint8_t)(plen & 0xff);
		if (body_len > 0) {
			memcpy(out + out_off, in + in_off, body_len);
		}
		out_off += body_len;
		in_off += body_len;

		pad = (4 - (out_off % 4)) % 4;
		if (out_off + pad > out_cap) {
			break;
		}
		memset(out + out_off, 0, pad);
		out_off += pad;
	}

	return (out_off);
}

static uint8_t
rd_u8(const uint8_t *in, size_t in_len, size_t *off)
{
	uint8_t v = 0;
	if (*off < in_len) {
		v = in[*off];
		(*off)++;
	}
	return v;
}

static uint16_t
rd_u16(const uint8_t *in, size_t in_len, size_t *off)
{
	uint16_t hi = rd_u8(in, in_len, off);
	uint16_t lo = rd_u8(in, in_len, off);
	return (uint16_t)((hi << 8) | lo);
}

static uint32_t
rd_u32(const uint8_t *in, size_t in_len, size_t *off)
{
	uint32_t b0 = rd_u8(in, in_len, off);
	uint32_t b1 = rd_u8(in, in_len, off);
	uint32_t b2 = rd_u8(in, in_len, off);
	uint32_t b3 = rd_u8(in, in_len, off);
	return (b0 << 24) | (b1 << 16) | (b2 << 8) | b3;
}

static void
rd_bytes(const uint8_t *in, size_t in_len, size_t *off, uint8_t *out, size_t want)
{
	size_t i;
	for (i = 0; i < want; i++) {
		out[i] = rd_u8(in, in_len, off);
	}
}

static size_t
append_u8(uint8_t *out, size_t out_cap, size_t pos, uint8_t v)
{
	if (pos + 1 > out_cap) {
		return pos;
	}
	out[pos] = v;
	return pos + 1;
}

static size_t
append_u16(uint8_t *out, size_t out_cap, size_t pos, uint16_t v)
{
	if (pos + 2 > out_cap) {
		return pos;
	}
	out[pos] = (uint8_t)((v >> 8) & 0xff);
	out[pos + 1] = (uint8_t)(v & 0xff);
	return pos + 2;
}

static size_t
append_u32(uint8_t *out, size_t out_cap, size_t pos, uint32_t v)
{
	if (pos + 4 > out_cap) {
		return pos;
	}
	out[pos] = (uint8_t)((v >> 24) & 0xff);
	out[pos + 1] = (uint8_t)((v >> 16) & 0xff);
	out[pos + 2] = (uint8_t)((v >> 8) & 0xff);
	out[pos + 3] = (uint8_t)(v & 0xff);
	return pos + 4;
}

static size_t
append_bytes(uint8_t *out, size_t out_cap, size_t pos, const uint8_t *src, size_t n)
{
	if (pos + n > out_cap) {
		return pos;
	}
	if (n > 0) {
		memcpy(out + pos, src, n);
	}
	return pos + n;
}

static size_t
pad4(uint8_t *out, size_t out_cap, size_t pos)
{
	size_t pad = (4 - (pos % 4)) % 4;
	size_t i;
	for (i = 0; i < pad; i++) {
		pos = append_u8(out, out_cap, pos, 0);
	}
	return pos;
}

static size_t
finish_chunk(uint8_t *out, size_t out_cap, size_t pos)
{
	uint16_t unpadded_len;

	if (pos < 4 || pos > 0xffff) {
		return pos;
	}
	unpadded_len = (uint16_t)pos;
	out[2] = (uint8_t)((unpadded_len >> 8) & 0xff);
	out[3] = (uint8_t)(unpadded_len & 0xff);
	return pad4(out, out_cap, pos);
}

static size_t
build_sack_chunk(const uint8_t *in, size_t in_len, uint8_t *out, size_t out_cap, int nr_sack)
{
	size_t off = 0;
	size_t pos = 0;
	uint32_t cum_tsn_ack;
	uint32_t a_rwnd;
	uint16_t num_gap;
	uint16_t num_dup;
	size_t i;

	if (out_cap < 4) {
		return 0;
	}
	pos = append_u8(out, out_cap, pos, (uint8_t)(nr_sack ? SCTP_NR_SELECTIVE_ACK : SCTP_SELECTIVE_ACK));
	pos = append_u8(out, out_cap, pos, 0);
	pos = append_u16(out, out_cap, pos, 0);

	cum_tsn_ack = rd_u32(in, in_len, &off);
	a_rwnd = rd_u32(in, in_len, &off);
	num_gap = (uint16_t)(rd_u8(in, in_len, &off) % 5);
	num_dup = (uint16_t)(rd_u8(in, in_len, &off) % 5);

	pos = append_u32(out, out_cap, pos, cum_tsn_ack);
	pos = append_u32(out, out_cap, pos, a_rwnd);
	pos = append_u16(out, out_cap, pos, num_gap);
	pos = append_u16(out, out_cap, pos, num_dup);

	for (i = 0; i < num_gap; i++) {
		pos = append_u16(out, out_cap, pos, rd_u16(in, in_len, &off));
		pos = append_u16(out, out_cap, pos, rd_u16(in, in_len, &off));
	}
	for (i = 0; i < num_dup; i++) {
		pos = append_u32(out, out_cap, pos, rd_u32(in, in_len, &off));
	}

	return finish_chunk(out, out_cap, pos);
}

static size_t
build_heartbeat_chunk(const uint8_t *in, size_t in_len, uint8_t *out, size_t out_cap, int is_ack)
{
	size_t off = 0;

	if (is_ack) {

		struct fuzz_sctp_heartbeat_chunk hb;
		void *addr1 = (void *)1;

		if (out_cap < sizeof(hb)) {
			return 0;
		}
		memset(&hb, 0, sizeof(hb));
		hb.chunk_type = SCTP_HEARTBEAT_ACK;
		hb.chunk_flags = 0;
		hb.chunk_length = htons((uint16_t)sizeof(hb));
		hb.hb_info.param_type = htons(SCTP_HEARTBEAT_INFO);
		hb.hb_info.param_length = htons((uint16_t)sizeof(hb.hb_info));
		hb.hb_info.time_value_1 = (time_t)rd_u32(in, in_len, &off);
		hb.hb_info.time_value_2 = rd_u32(in, in_len, &off);
		hb.hb_info.random_value1 = rd_u32(in, in_len, &off);
		hb.hb_info.random_value2 = rd_u32(in, in_len, &off);
		hb.hb_info.addr_family = AF_CONN;
		hb.hb_info.addr_len = (uint8_t)sizeof(struct sockaddr_conn);
		memcpy(hb.hb_info.address, &addr1, sizeof(addr1));

		memcpy(out, &hb, sizeof(hb));
		return sizeof(hb);
	} else {
		size_t pos = 0;
		uint8_t infolen;
		uint8_t infobuf[64];

		if (out_cap < 4) {
			return 0;
		}
		pos = append_u8(out, out_cap, pos, SCTP_HEARTBEAT_REQUEST);
		pos = append_u8(out, out_cap, pos, 0);
		pos = append_u16(out, out_cap, pos, 0);

		infolen = (uint8_t)(rd_u8(in, in_len, &off) % (uint8_t)(sizeof(infobuf) + 1));
		rd_bytes(in, in_len, &off, infobuf, infolen);

		pos = append_u16(out, out_cap, pos, SCTP_HEARTBEAT_INFO);
		pos = append_u16(out, out_cap, pos, (uint16_t)(4 + infolen));
		pos = append_bytes(out, out_cap, pos, infobuf, infolen);

		return finish_chunk(out, out_cap, pos);
	}
}

static size_t
build_error_chunk(const uint8_t *in, size_t in_len, uint8_t *out, size_t out_cap)
{
	size_t pos = 0;
	size_t body_len;

	if (out_cap < 4) {
		return 0;
	}
	pos = append_u8(out, out_cap, pos, SCTP_OPERATION_ERROR);
	pos = append_u8(out, out_cap, pos, 0);
	pos = append_u16(out, out_cap, pos, 0);

	body_len = build_tlv_params(in, in_len, out + pos, out_cap - pos);
	pos += body_len;

	return finish_chunk(out, out_cap, pos);
}

static size_t
build_asconf_chunk(const uint8_t *in, size_t in_len, uint8_t *out, size_t out_cap)
{
	size_t off = 0;
	size_t pos = 0;
	uint32_t serial;
	uint32_t addr_val;
	uint16_t ptype;
	uint32_t corr_id;
	static const uint16_t asconf_ptypes[3] = { SCTP_ADD_IP_ADDRESS, SCTP_DEL_IP_ADDRESS, SCTP_SET_PRIM_ADDR };

	if (out_cap < 4) {
		return 0;
	}
	pos = append_u8(out, out_cap, pos, SCTP_ASCONF);
	pos = append_u8(out, out_cap, pos, 0);
	pos = append_u16(out, out_cap, pos, 0);

	serial = rd_u32(in, in_len, &off);
	pos = append_u32(out, out_cap, pos, serial);

	pos = append_u16(out, out_cap, pos, SCTP_IPV4_ADDRESS);
	pos = append_u16(out, out_cap, pos, 8);
	addr_val = rd_u32(in, in_len, &off);
	pos = append_u32(out, out_cap, pos, addr_val);

	ptype = asconf_ptypes[rd_u8(in, in_len, &off) % 3];
	corr_id = rd_u32(in, in_len, &off);

	pos = append_u16(out, out_cap, pos, ptype);
	pos = append_u16(out, out_cap, pos, 4 + 4 + 8);
	pos = append_u32(out, out_cap, pos, corr_id);
	pos = append_u16(out, out_cap, pos, SCTP_IPV4_ADDRESS);
	pos = append_u16(out, out_cap, pos, 8);
	pos = append_u32(out, out_cap, pos, rd_u32(in, in_len, &off));

	return finish_chunk(out, out_cap, pos);
}

static size_t
build_asconf_ack_chunk(const uint8_t *in, size_t in_len, uint8_t *out, size_t out_cap,
                        const uint8_t *captured_serial, int have_serial)
{
	size_t off = 0;
	size_t pos = 0;
	size_t body_len;

	if (out_cap < 8) {
		return 0;
	}
	pos = append_u8(out, out_cap, pos, SCTP_ASCONF_ACK);
	pos = append_u8(out, out_cap, pos, 0);
	pos = append_u16(out, out_cap, pos, 0);

	if (have_serial) {
		pos = append_bytes(out, out_cap, pos, captured_serial, 4);
	} else {
		pos = append_u32(out, out_cap, pos, rd_u32(in, in_len, &off));
	}

	body_len = build_tlv_params(in + off, (in_len > off) ? (in_len - off) : 0,
	                             out + pos, out_cap - pos);
	pos += body_len;

	return finish_chunk(out, out_cap, pos);
}

static size_t
build_abort_chunk(const uint8_t *in, size_t in_len, uint8_t *out, size_t out_cap, uint8_t flags)
{
	size_t pos = 0;
	size_t body_len;

	if (out_cap < 4) {
		return 0;
	}
	pos = append_u8(out, out_cap, pos, SCTP_ABORT_ASSOCIATION);
	pos = append_u8(out, out_cap, pos, flags);
	pos = append_u16(out, out_cap, pos, 0);

	body_len = build_tlv_params(in, in_len, out + pos, out_cap - pos);
	pos += body_len;

	return finish_chunk(out, out_cap, pos);
}

static size_t
build_forward_tsn_chunk(const uint8_t *in, size_t in_len, uint8_t *out, size_t out_cap)
{
	size_t off = 0;
	size_t pos = 0;
	uint32_t new_cum_tsn;
	uint8_t n;
	uint8_t i;

	if (out_cap < 4) {
		return 0;
	}
	pos = append_u8(out, out_cap, pos, SCTP_FORWARD_CUM_TSN);
	pos = append_u8(out, out_cap, pos, 0);
	pos = append_u16(out, out_cap, pos, 0);

	new_cum_tsn = rd_u32(in, in_len, &off);
	pos = append_u32(out, out_cap, pos, new_cum_tsn);

	n = (uint8_t)(rd_u8(in, in_len, &off) % 5);
	for (i = 0; i < n; i++) {
		pos = append_u16(out, out_cap, pos, rd_u16(in, in_len, &off));
		pos = append_u16(out, out_cap, pos, rd_u16(in, in_len, &off));
	}

	return finish_chunk(out, out_cap, pos);
}

static size_t
build_reconfig_chunk(const uint8_t *in, size_t in_len, uint8_t *out, size_t out_cap)
{
	size_t off = 0;
	size_t pos = 0;
	size_t param_start;
	uint16_t sel;
	uint32_t req_seq;
	uint32_t resp_seq;
	uint32_t last_tsn;
	uint8_t n;
	uint8_t i;
	static const uint16_t sub_types[4] = {
		SCTP_STR_RESET_OUT_REQUEST, SCTP_STR_RESET_IN_REQUEST,
		SCTP_STR_RESET_ADD_OUT_STREAMS, SCTP_STR_RESET_ADD_IN_STREAMS
	};

	if (out_cap < 4) {
		return 0;
	}
	pos = append_u8(out, out_cap, pos, SCTP_STREAM_RESET);
	pos = append_u8(out, out_cap, pos, 0);
	pos = append_u16(out, out_cap, pos, 0);

	sel = sub_types[rd_u8(in, in_len, &off) % 4];
	param_start = pos;
	pos = append_u16(out, out_cap, pos, sel);
	pos = append_u16(out, out_cap, pos, 0);

	req_seq = rd_u32(in, in_len, &off);
	pos = append_u32(out, out_cap, pos, req_seq);

	if ((sel == SCTP_STR_RESET_OUT_REQUEST) || (sel == SCTP_STR_RESET_IN_REQUEST)) {
		resp_seq = rd_u32(in, in_len, &off);
		last_tsn = rd_u32(in, in_len, &off);
		pos = append_u32(out, out_cap, pos, resp_seq);
		pos = append_u32(out, out_cap, pos, last_tsn);
		if (sel == SCTP_STR_RESET_OUT_REQUEST) {
			n = (uint8_t)(rd_u8(in, in_len, &off) % 5);
			for (i = 0; i < n; i++) {
				pos = append_u16(out, out_cap, pos, rd_u16(in, in_len, &off));
			}
		}
	} else {

		pos = append_u16(out, out_cap, pos, 0);
		pos = append_u16(out, out_cap, pos, rd_u16(in, in_len, &off));
	}

	if ((pos >= param_start + 4) && (pos - param_start <= 0xffff) &&
	    (param_start + 4 <= out_cap)) {
		uint16_t sub_len = (uint16_t)(pos - param_start);
		out[param_start + 2] = (uint8_t)((sub_len >> 8) & 0xff);
		out[param_start + 3] = (uint8_t)(sub_len & 0xff);
	}

	return finish_chunk(out, out_cap, pos);
}

static size_t
build_auth_chunk(const uint8_t *in, size_t in_len, uint8_t *out, size_t out_cap)
{
	size_t off = 0;
	size_t pos = 0;
	uint16_t key_id;
	uint16_t hmac_id;
	uint8_t mac[32];
	uint8_t maclen;
	uint8_t bias;

	if (out_cap < 4) {
		return 0;
	}
	pos = append_u8(out, out_cap, pos, SCTP_AUTHENTICATION);
	pos = append_u8(out, out_cap, pos, 0);
	pos = append_u16(out, out_cap, pos, 0);

	bias = rd_u8(in, in_len, &off);
	if (bias & 0x01) {
		key_id = 1;
		hmac_id = SCTP_AUTH_HMAC_ID_SHA1;
	} else {
		key_id = rd_u16(in, in_len, &off);
		hmac_id = rd_u16(in, in_len, &off);
	}
	pos = append_u16(out, out_cap, pos, key_id);
	pos = append_u16(out, out_cap, pos, hmac_id);

	maclen = (uint8_t)(rd_u8(in, in_len, &off) % (uint8_t)(sizeof(mac) + 1));
	rd_bytes(in, in_len, &off, mac, maclen);
	pos = append_bytes(out, out_cap, pos, mac, maclen);

	return finish_chunk(out, out_cap, pos);
}

static size_t
build_data_chunk(uint32_t tsn, uint16_t stream, uint16_t ssn, uint32_t ppid, uint8_t flags,
                  const uint8_t *payload, size_t payload_len, uint8_t *out, size_t out_cap)
{
	size_t pos = 0;

	if (out_cap < 4) {
		return 0;
	}
	pos = append_u8(out, out_cap, pos, SCTP_DATA);
	pos = append_u8(out, out_cap, pos, flags);
	pos = append_u16(out, out_cap, pos, 0);
	pos = append_u32(out, out_cap, pos, tsn);
	pos = append_u16(out, out_cap, pos, stream);
	pos = append_u16(out, out_cap, pos, ssn);
	pos = append_u32(out, out_cap, pos, ppid);
	pos = append_bytes(out, out_cap, pos, payload, payload_len);

	return finish_chunk(out, out_cap, pos);
}

static int
find_param(const uint8_t *buf, size_t buf_len, size_t start_off, uint16_t want_type,
           size_t *val_off, size_t *val_len)
{
	size_t off = start_off;

	while (off + 4 <= buf_len) {
		uint16_t ptype = (uint16_t)(((uint16_t)buf[off] << 8) | buf[off + 1]);
		uint16_t plen = (uint16_t)(((uint16_t)buf[off + 2] << 8) | buf[off + 3]);
		size_t padded;

		if ((plen < 4) || (off + plen > buf_len)) {
			break;
		}
		if (ptype == want_type) {
			*val_off = off + 4;
			*val_len = plen - 4;
			return (1);
		}
		padded = plen + ((4 - (plen % 4)) % 4);
		if (padded == 0) {
			break;
		}
		off += padded;
	}
	return (0);
}

static int
initialize_fuzzer(void)
{
	struct sockaddr_conn lconn;

	usrsctp_init_nothreads(0, conn_output, NULL);
	usrsctp_enable_crc32c_offload();
	usrsctp_register_address((void *)1);

	usrsctp_register_address((void *)2);

	usrsctp_sysctl_set_sctp_nrsack_enable(1);

	g_listener_socket = usrsctp_socket(AF_CONN, SOCK_STREAM, IPPROTO_SCTP, NULL, NULL, 0, 0);
	if (g_listener_socket != NULL) {
		usrsctp_set_non_blocking(g_listener_socket, 1);

		memset(&lconn, 0, sizeof(lconn));
		lconn.sconn_family = AF_CONN;
#ifdef HAVE_SCONN_LEN
		lconn.sconn_len = sizeof(struct sockaddr_conn);
#endif
		lconn.sconn_port = htons(5002);
		lconn.sconn_addr = (void *)1;

		(void)usrsctp_bind(g_listener_socket, (struct sockaddr *)&lconn, sizeof(lconn));
		(void)usrsctp_listen(g_listener_socket, 1);
	}

	return (1);
}

int
LLVMFuzzerTestOneInput(const uint8_t *data, size_t data_size)
{
	static int initialized;
	char *fuzz_packet_buffer;
	struct sockaddr_conn sconn;
	struct socket *socket_client;
	struct linger so_linger;
	struct sctp_event event;
	struct sctp_assoc_value assoc_val;
	struct sctp_initmsg initmsg;
	struct sctp_common_header *common_header;
	size_t i;
	int optval;
	int result;
	uint8_t ctrl;
	uint8_t ctrl2;
	uint8_t ctrl3;

	uint16_t event_types[] = {
		SCTP_ASSOC_CHANGE,
		SCTP_PEER_ADDR_CHANGE,
		SCTP_REMOTE_ERROR,
		SCTP_SEND_FAILED,
		SCTP_SHUTDOWN_EVENT,
		SCTP_ADAPTATION_INDICATION,
		SCTP_PARTIAL_DELIVERY_EVENT,
		SCTP_AUTHENTICATION_EVENT,
		SCTP_STREAM_RESET_EVENT,
		SCTP_SENDER_DRY_EVENT,
		SCTP_ASSOC_RESET_EVENT,
		SCTP_STREAM_CHANGE_EVENT,
	};

	char fuzz_init_ack[] = "\x13\x89\x13\x88\x49\xa4\xac\xb2\x00\x00\x00\x00\x02\x00\x01\xb4" \
		"\x2b\xe8\x47\x40\x00\x1c\x71\xc7\xff\xff\xff\xff\xed\x69\x58\xec" \
		"\xc0\x06\x00\x08\x00\x00\x07\xc4\x80\x00\x00\x04\xc0\x00\x00\x04" \
		"\x80\x08\x00\x0b\xc0\xc2\x0f\xc1\x80\x82\x40\x00\x80\x02\x00\x24" \
		"\x40\x39\xcf\x32\xd6\x60\xcf\xfa\x3f\x2f\xa9\x52\xed\x2b\xf2\xe6" \
		"\x2f\xb7\x81\x96\xf8\xda\xe9\xa0\x62\x01\x79\xe1\x0d\x5f\x38\xaa" \
		"\x80\x04\x00\x08\x00\x03\x00\x01\x80\x03\x00\x06\x80\xc1\x00\x00" \
		"\x00\x07\x01\x50\x4b\x41\x4d\x45\x2d\x42\x53\x44\x20\x31\x2e\x31" \
		"\x00\x00\x00\x00\x64\xdb\x63\x00\x00\x00\x00\x00\xc9\x76\x03\x00" \
		"\x00\x00\x00\x00\x60\xea\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00" \
		"\xb2\xac\xa4\x49\x2b\xe8\x47\x40\xd4\xc9\x79\x52\x00\x00\x00\x00" \
		"\x00\x00\x00\x00\x00\x00\x00\x00\x05\x00\x00\x00\xd4\xc9\x79\x53" \
		"\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x05\x00\x00\x00" \
		"\x00\x00\x00\x00\x5a\x76\x13\x89\x01\x00\x00\x00\x00\x00\x00\x00" \
		"\x00\x00\x00\x00\x01\x00\x00\x62\x49\xa4\xac\xb2\x00\x1c\x71\xc7" \
		"\x00\x01\xff\xff\x82\xe6\xc8\x44\x80\x00\x00\x04\xc0\x00\x00\x04" \
		"\x80\x08\x00\x0b\xc0\xc2\x0f\xc1\x80\x82\x40\x00\x80\x02\x00\x24" \
		"\xb6\xbb\xb5\x7f\xbb\x4b\x0e\xb5\x42\xf6\x75\x18\x4f\x79\x0f\x24" \
		"\x1c\x44\x0b\xd6\x62\xa9\x84\xe7\x2c\x3c\x7f\xad\x1b\x67\x81\x57" \
		"\x80\x04\x00\x08\x00\x03\x00\x01\x80\x03\x00\x06\x80\xc1\x00\x00" \
		"\x00\x0c\x00\x06\x00\x05\x00\x00\x02\x00\x01\xb4\x2b\xe8\x47\x40" \
		"\x00\x1c\x71\xc7\x00\x01\xff\xff\xed\x69\x58\xec\xc0\x06\x00\x08" \
		"\x00\x00\x07\xc4\x80\x00\x00\x04\xc0\x00\x00\x04\x80\x08\x00\x0b" \
		"\xc0\xc2\x0f\xc1\x80\x82\x40\x00\x80\x02\x00\x24\x40\x39\xcf\x32" \
		"\xd6\x60\xcf\xfa\x3f\x2f\xa9\x52\xed\x2b\xf2\xe6\x2f\xb7\x81\x96" \
		"\xf8\xda\xe9\xa0\x62\x01\x79\xe1\x0d\x5f\x38\xaa\x80\x04\x00\x08" \
		"\x00\x03\x00\x01\x80\x03\x00\x06\x80\xc1\x00\x00\x81\xe1\x1e\x81" \
		"\xea\x41\xeb\xf0\x12\xd9\x74\xbe\x13\xfd\x4b\x6c\x5c\xa2\x8f\x00";

	char fuzz_cookie_ack[] =
	    "\x13\x89\x13\x88\x54\xc2\x7c\x46\x00\x00\x00\x00\x0b\x00\x00\x04";

	char fuzz_i_data[] = "\x13\x89\x13\x88\x07\x01\x6c\xd3\x00\x00\x00\x00\x40\x03" \
		"\x00\xdc\x2d\x2b\x46\xd4\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00" \
		"\x00\x27\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41" \
		"\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41" \
		"\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41" \
		"\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41" \
		"\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41" \
		"\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41" \
		"\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41" \
		"\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41" \
		"\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41" \
		"\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41" \
		"\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41" \
		"\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41" \
		"\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41";

	char fuzz_data[] = "\x13\x89\x13\x88\x27\xc4\xbf\xdf\x00\x00\x00\x00\x00\x03" \
		"\x00\xd8\x79\x64\xb7\xc1\x00\x00\x00\x00\x00\x00\x00\x27\x41\x41" \
		"\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41" \
		"\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41" \
		"\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41" \
		"\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41" \
		"\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41" \
		"\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41" \
		"\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41" \
		"\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41" \
		"\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41" \
		"\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41" \
		"\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41" \
		"\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41\x41" \
		"\x41\x41\x41\x41\x41\x41";

	const char fuzz_common_header[COMMON_HEADER_SIZE] =
	    "\x13\x89\x13\x88\x54\xc2\x7c\x46\x00\x00\x00\x00";

	if (data_size < 7 || data_size > MAX_PACKET_SIZE) {
		return (0);
	}

	if (!initialized) {
		initialized = initialize_fuzzer();
	}

	ctrl = data[0];
	ctrl2 = data[1];
	ctrl3 = data[2];
	assoc_vtag = 0;
	g_last_output_len = 0;
	g_have_asconf_serial = 0;

	if (ctrl & FUZZ_B_DISABLE_CRC) {
		usrsctp_disable_crc32c_offload();
	} else {
		usrsctp_enable_crc32c_offload();
	}

	socket_client = usrsctp_socket(AF_CONN, SOCK_STREAM, IPPROTO_SCTP, NULL, NULL, 0, 0);
	if (socket_client == NULL) {
		return (0);
	}

	usrsctp_set_non_blocking(socket_client, 1);

	memset(&initmsg, 1, sizeof(struct sctp_initmsg));
	usrsctp_setsockopt(socket_client, IPPROTO_SCTP, SCTP_INITMSG, &initmsg, sizeof(struct sctp_initmsg));

	so_linger.l_onoff = 1;
	so_linger.l_linger = 0;
	usrsctp_setsockopt(socket_client, SOL_SOCKET, SO_LINGER, &so_linger, sizeof(struct linger));

	memset(&event, 0, sizeof(event));
	event.se_on = 1;
	for (i = 0; i < sizeof(event_types) / sizeof(event_types[0]); i++) {
		event.se_type = event_types[i];
		usrsctp_setsockopt(socket_client, IPPROTO_SCTP, SCTP_EVENT, &event, sizeof(event));
	}

	optval = 1;
	usrsctp_setsockopt(socket_client, IPPROTO_SCTP, SCTP_RECVRCVINFO, &optval, sizeof(optval));

	optval = 1;
	usrsctp_setsockopt(socket_client, IPPROTO_SCTP, SCTP_RECVNXTINFO, &optval, sizeof(optval));

	assoc_val.assoc_id = SCTP_ALL_ASSOC;
	assoc_val.assoc_value = SCTP_ENABLE_RESET_STREAM_REQ | SCTP_ENABLE_RESET_ASSOC_REQ | SCTP_ENABLE_CHANGE_ASSOC_REQ;
	usrsctp_setsockopt(socket_client, IPPROTO_SCTP, SCTP_ENABLE_STREAM_RESET, &assoc_val, sizeof(struct sctp_assoc_value));

	if (ctrl & FUZZ_B_I_DATA_SUPPORT) {
		optval = 2;
		usrsctp_setsockopt(socket_client, IPPROTO_SCTP, SCTP_FRAGMENT_INTERLEAVE, &optval, sizeof(optval));

		memset(&assoc_val, 0, sizeof(assoc_val));
		assoc_val.assoc_value = 1;
		usrsctp_setsockopt(socket_client, IPPROTO_SCTP, SCTP_INTERLEAVING_SUPPORTED, &assoc_val, sizeof(assoc_val));
	}

	if (ctrl2 & FUZZ2_B_AUTO_ASCONF) {
		optval = 1;
		usrsctp_setsockopt(socket_client, IPPROTO_SCTP, SCTP_AUTO_ASCONF, &optval, sizeof(optval));
	}

	if (ctrl2 & FUZZ2_B_AUTH_SUPPORTED) {
		memset(&assoc_val, 0, sizeof(assoc_val));
		assoc_val.assoc_value = 1;
		usrsctp_setsockopt(socket_client, IPPROTO_SCTP, SCTP_AUTH_SUPPORTED, &assoc_val, sizeof(assoc_val));
	}

	if (ctrl2 & FUZZ2_B_AUTH_KEY) {
		uint8_t authkey_buf[sizeof(struct sctp_authkey) + 16];
		struct sctp_authkey *authkey = (struct sctp_authkey *)authkey_buf;
		struct sctp_authkeyid authkeyid;

		memset(authkey_buf, 0, sizeof(authkey_buf));
		authkey->sca_assoc_id = 0;
		authkey->sca_keynumber = 1;
		authkey->sca_keylength = 16;
		memset(authkey_buf + sizeof(struct sctp_authkey), 0x42, 16);
		usrsctp_setsockopt(socket_client, IPPROTO_SCTP, SCTP_AUTH_KEY, authkey_buf, sizeof(authkey_buf));

		memset(&authkeyid, 0, sizeof(authkeyid));
		authkeyid.scact_keynumber = 1;
		usrsctp_setsockopt(socket_client, IPPROTO_SCTP, SCTP_AUTH_ACTIVE_KEY, &authkeyid, sizeof(authkeyid));
	}

	optval = 1;
	usrsctp_setsockopt(socket_client, IPPROTO_SCTP, SCTP_REUSE_PORT, &optval, sizeof(optval));

	memset(&sconn, 0, sizeof(struct sockaddr_conn));
	sconn.sconn_family = AF_CONN;
#ifdef HAVE_SCONN_LEN
	sconn.sconn_len = sizeof(struct sockaddr_conn);
#endif
	sconn.sconn_port = htons(5000);
	sconn.sconn_addr = (void *)1;

	result = usrsctp_bind(socket_client, (struct sockaddr *)&sconn, sizeof(struct sockaddr_conn));
	(void)result;

	optval = 1;
	usrsctp_setsockopt(socket_client, IPPROTO_SCTP, SCTP_NODELAY, &optval, sizeof(optval));

	usrsctp_set_upcall(socket_client, handle_upcall, NULL);

	memset(&sconn, 0, sizeof(struct sockaddr_conn));
	sconn.sconn_family = AF_CONN;
#ifdef HAVE_SCONN_LEN
	sconn.sconn_len = sizeof(struct sockaddr_conn);
#endif
	sconn.sconn_port = htons(5001);
	sconn.sconn_addr = (void *)1;

	result = usrsctp_connect(socket_client, (struct sockaddr *)&sconn, sizeof(struct sockaddr_conn));
	(void)result;

	if (ctrl2 & FUZZ2_B_ADD_IP_ADDRESS) {
		struct sockaddr_conn extra_conn;

		memset(&extra_conn, 0, sizeof(extra_conn));
		extra_conn.sconn_family = AF_CONN;
#ifdef HAVE_SCONN_LEN
		extra_conn.sconn_len = sizeof(struct sockaddr_conn);
#endif
		extra_conn.sconn_port = htons(5000);
		extra_conn.sconn_addr = (void *)2;

		(void)usrsctp_bindx(socket_client, (struct sockaddr *)&extra_conn, 1, SCTP_BINDX_ADD_ADDR);
	}

	if (ctrl & FUZZ_B_INJECT_INIT_ACK) {
		common_header = (struct sctp_common_header *)fuzz_init_ack;
		common_header->verification_tag = assoc_vtag;
		usrsctp_conninput((void *)1, fuzz_init_ack, sizeof(fuzz_init_ack) - 1, 0);
	}

	if (ctrl & FUZZ_B_INJECT_COOKIE_ACK) {
		common_header = (struct sctp_common_header *)fuzz_cookie_ack;
		common_header->verification_tag = assoc_vtag;
		usrsctp_conninput((void *)1, fuzz_cookie_ack, sizeof(fuzz_cookie_ack) - 1, 0);
	}

	if ((ctrl & FUZZ_B_INJECT_INIT_ACK) &&
	    (ctrl & FUZZ_B_INJECT_COOKIE_ACK) &&
	    (ctrl & FUZZ_B_SEND_DATA)) {
		const char *sendbuffer = "fuzzing usrsctp_conninput";
		usrsctp_sendv(socket_client, sendbuffer, strlen(sendbuffer), NULL, 0, NULL, 0, SCTP_SENDV_NOINFO, 0);
	}

	if ((ctrl & FUZZ_B_INJECT_INIT_ACK) &&
	    (ctrl & FUZZ_B_INJECT_COOKIE_ACK) &&
	    (ctrl & FUZZ_B_SEND_STREAM_RESET)) {
		struct sctp_reset_streams srs;
		memset(&srs, 0, sizeof(struct sctp_reset_streams));
		srs.srs_flags = SCTP_STREAM_RESET_INCOMING | SCTP_STREAM_RESET_OUTGOING;
		usrsctp_setsockopt(socket_client, IPPROTO_SCTP, SCTP_RESET_STREAMS, &srs, sizeof(struct sctp_reset_streams));
	}

	if ((ctrl & FUZZ_B_INJECT_INIT_ACK) &&
	    (ctrl & FUZZ_B_INJECT_COOKIE_ACK) &&
	    (ctrl & FUZZ_B_INJECT_DATA)) {
		if (ctrl & FUZZ_B_I_DATA_SUPPORT) {
			common_header = (struct sctp_common_header *)fuzz_i_data;
			common_header->verification_tag = assoc_vtag;
			usrsctp_conninput((void *)1, fuzz_i_data, sizeof(fuzz_i_data) - 1, 0);
		} else {
			common_header = (struct sctp_common_header *)fuzz_data;
			common_header->verification_tag = assoc_vtag;
			usrsctp_conninput((void *)1, fuzz_data, sizeof(fuzz_data) - 1, 0);
		}
	}

	if ((ctrl & FUZZ_B_I_DATA_SUPPORT) && (ctrl & FUZZ_B_SEND_DATA_FORCE)) {
		const char *sendbuffer = "fuzzing usrsctp_conninput again";
		usrsctp_sendv(socket_client, sendbuffer, strlen(sendbuffer), NULL, 0, NULL, 0, SCTP_SENDV_NOINFO, 0);
	}

	if ((ctrl & FUZZ_B_INJECT_INIT_ACK) && (ctrl & FUZZ_B_INJECT_COOKIE_ACK)) {
		uint8_t extra_sel = (uint8_t)(ctrl3 & FUZZ3_SEL_MASK);

		if (extra_sel != FUZZ3_SEL_NONE) {
			uint8_t chunk_buf[512];
			size_t chunk_len = 0;
			size_t avail3 = (data_size > 3) ? (data_size - 3) : 0;
			const uint8_t *in3 = data + 3;

			switch (extra_sel) {
			case FUZZ3_SEL_SACK:
				chunk_len = build_sack_chunk(in3, avail3, chunk_buf, sizeof(chunk_buf), 0);
				break;
			case FUZZ3_SEL_NR_SACK:
				chunk_len = build_sack_chunk(in3, avail3, chunk_buf, sizeof(chunk_buf), 1);
				break;
			case FUZZ3_SEL_HEARTBEAT_REQ:
				chunk_len = build_heartbeat_chunk(in3, avail3, chunk_buf, sizeof(chunk_buf), 0);
				break;
			case FUZZ3_SEL_HEARTBEAT_ACK:
				chunk_len = build_heartbeat_chunk(in3, avail3, chunk_buf, sizeof(chunk_buf), 1);
				break;
			case FUZZ3_SEL_ERROR:
				chunk_len = build_error_chunk(in3, avail3, chunk_buf, sizeof(chunk_buf));
				break;
			case FUZZ3_SEL_ASCONF:
				chunk_len = build_asconf_chunk(in3, avail3, chunk_buf, sizeof(chunk_buf));
				break;
			case FUZZ3_SEL_ASCONF_ACK:
				chunk_len = build_asconf_ack_chunk(in3, avail3, chunk_buf, sizeof(chunk_buf),
				                                    g_last_asconf_serial, g_have_asconf_serial);
				break;
			case FUZZ3_SEL_FORWARD_TSN:
				chunk_len = build_forward_tsn_chunk(in3, avail3, chunk_buf, sizeof(chunk_buf));
				break;
			case FUZZ3_SEL_RECONFIG:
				chunk_len = build_reconfig_chunk(in3, avail3, chunk_buf, sizeof(chunk_buf));
				break;
			case FUZZ3_SEL_AUTH:
				chunk_len = build_auth_chunk(in3, avail3, chunk_buf, sizeof(chunk_buf));
				break;
			case FUZZ3_SEL_ABORT:
				chunk_len = build_abort_chunk(in3, avail3, chunk_buf, sizeof(chunk_buf),
				                               (uint8_t)((ctrl3 >> 5) & 0x01));
				break;
			default:
				chunk_len = 0;
				break;
			}

			if (chunk_len >= 4 && chunk_len <= sizeof(chunk_buf)) {
				uint8_t pkt[COMMON_HEADER_SIZE + 512];
				struct sctp_common_header *ch2;
				size_t pkt_len = COMMON_HEADER_SIZE + chunk_len;

				memcpy(pkt, fuzz_common_header, COMMON_HEADER_SIZE);
				memcpy(pkt + COMMON_HEADER_SIZE, chunk_buf, chunk_len);
				ch2 = (struct sctp_common_header *)pkt;
				ch2->verification_tag = assoc_vtag;

				usrsctp_conninput((void *)1, pkt, pkt_len, 0);
			}
		}

		if (ctrl3 & FUZZ3_B_FRAGMENTED_DATA) {
			size_t avail3 = (data_size > 3) ? (data_size - 3) : 0;
			const uint8_t *in3 = data + 3;
			size_t half = avail3 / 2;
			uint8_t chunk_buf2[300];
			uint8_t pkt2[COMMON_HEADER_SIZE + 300];
			struct sctp_common_header *ch4;
			size_t clen;
			uint32_t tsn_delta = (uint32_t)((ctrl3 >> 6) & 0x03);
			uint32_t tsn_base = (uint32_t)(FUZZ_REMOTE_INITIAL_TSN + tsn_delta);
			uint16_t frag_stream = (uint16_t)((avail3 > 0) ? (in3[0] % 10) : 0);

			if (half > 200) {
				half = 200;
			}

			clen = build_data_chunk(tsn_base, frag_stream, 0, 0, 0x02  , in3, half,
			                         chunk_buf2, sizeof(chunk_buf2));
			if ((clen >= 4) && (clen <= sizeof(chunk_buf2))) {
				memcpy(pkt2, fuzz_common_header, COMMON_HEADER_SIZE);
				memcpy(pkt2 + COMMON_HEADER_SIZE, chunk_buf2, clen);
				ch4 = (struct sctp_common_header *)pkt2;
				ch4->verification_tag = assoc_vtag;
				usrsctp_conninput((void *)1, pkt2, COMMON_HEADER_SIZE + clen, 0);
			}

			clen = build_data_chunk(tsn_base + 1, frag_stream, 0, 0, 0x01  ,
			                         in3 + half, (avail3 > half) ? (avail3 - half) : 0,
			                         chunk_buf2, sizeof(chunk_buf2));
			if ((clen >= 4) && (clen <= sizeof(chunk_buf2))) {
				memcpy(pkt2, fuzz_common_header, COMMON_HEADER_SIZE);
				memcpy(pkt2 + COMMON_HEADER_SIZE, chunk_buf2, clen);
				ch4 = (struct sctp_common_header *)pkt2;
				ch4->verification_tag = assoc_vtag;
				usrsctp_conninput((void *)1, pkt2, COMMON_HEADER_SIZE + clen, 0);
			}
		}
	}

	if ((ctrl & FUZZ_B_INJECT_INIT_ACK) && (ctrl & FUZZ_B_INJECT_COOKIE_ACK) &&
	    (ctrl2 & FUZZ2_B_GRACEFUL_SHUTDOWN)) {
		(void)usrsctp_shutdown(socket_client, SHUT_RDWR);
	}

	if ((ctrl & FUZZ_B_INJECT_INIT_ACK) && (ctrl & FUZZ_B_INJECT_COOKIE_ACK) &&
	    (ctrl2 & FUZZ2_B_INJECT_SHUTDOWN_SEQ)) {
		uint8_t shutdown_pkt[COMMON_HEADER_SIZE + 8];
		uint8_t shutdown_ack_pkt[COMMON_HEADER_SIZE + 4];
		uint8_t shutdown_complete_pkt[COMMON_HEADER_SIZE + 4];
		struct sctp_common_header *ch;

		memcpy(shutdown_pkt, fuzz_common_header, COMMON_HEADER_SIZE);
		shutdown_pkt[COMMON_HEADER_SIZE + 0] = SCTP_SHUTDOWN;
		shutdown_pkt[COMMON_HEADER_SIZE + 1] = 0;
		shutdown_pkt[COMMON_HEADER_SIZE + 2] = 0;
		shutdown_pkt[COMMON_HEADER_SIZE + 3] = 8;
		memset(shutdown_pkt + COMMON_HEADER_SIZE + 4, 0, 4);
		ch = (struct sctp_common_header *)shutdown_pkt;
		ch->verification_tag = assoc_vtag;
		usrsctp_conninput((void *)1, shutdown_pkt, sizeof(shutdown_pkt), 0);

		memcpy(shutdown_ack_pkt, fuzz_common_header, COMMON_HEADER_SIZE);
		shutdown_ack_pkt[COMMON_HEADER_SIZE + 0] = SCTP_SHUTDOWN_ACK;
		shutdown_ack_pkt[COMMON_HEADER_SIZE + 1] = 0;
		shutdown_ack_pkt[COMMON_HEADER_SIZE + 2] = 0;
		shutdown_ack_pkt[COMMON_HEADER_SIZE + 3] = 4;
		ch = (struct sctp_common_header *)shutdown_ack_pkt;
		ch->verification_tag = assoc_vtag;
		usrsctp_conninput((void *)1, shutdown_ack_pkt, sizeof(shutdown_ack_pkt), 0);

		memcpy(shutdown_complete_pkt, fuzz_common_header, COMMON_HEADER_SIZE);
		shutdown_complete_pkt[COMMON_HEADER_SIZE + 0] = SCTP_SHUTDOWN_COMPLETE;
		shutdown_complete_pkt[COMMON_HEADER_SIZE + 1] = 0;
		shutdown_complete_pkt[COMMON_HEADER_SIZE + 2] = 0;
		shutdown_complete_pkt[COMMON_HEADER_SIZE + 3] = 4;
		ch = (struct sctp_common_header *)shutdown_complete_pkt;
		ch->verification_tag = assoc_vtag;
		usrsctp_conninput((void *)1, shutdown_complete_pkt, sizeof(shutdown_complete_pkt), 0);
	}

	if ((ctrl2 & FUZZ2_B_LISTENER_INIT) && (g_listener_socket != NULL)) {
		size_t avail_in = (data_size > 3) ? (data_size - 3) : 0;
		size_t alloc_size = COMMON_HEADER_SIZE + 20 + avail_in + 64;
		uint8_t *init_pkt = (uint8_t *)malloc(alloc_size);

		if (init_pkt != NULL) {
			size_t params_len;
			size_t pkt_len;
			uint16_t chunk_len;
			struct sctp_common_header *ch;
			struct socket *accepted;

			params_len = build_tlv_params(data + 3, avail_in, init_pkt + COMMON_HEADER_SIZE + 20,
			                               alloc_size - COMMON_HEADER_SIZE - 20);

			memset(init_pkt, 0, COMMON_HEADER_SIZE);

			init_pkt[0] = 0x13; init_pkt[1] = 0x89;
			init_pkt[2] = 0x13; init_pkt[3] = 0x8a;

			init_pkt[COMMON_HEADER_SIZE + 0] = SCTP_INITIATION;
			init_pkt[COMMON_HEADER_SIZE + 1] = 0;
			chunk_len = (uint16_t)(4 + 16 + params_len);
			init_pkt[COMMON_HEADER_SIZE + 2] = (uint8_t)((chunk_len >> 8) & 0xff);
			init_pkt[COMMON_HEADER_SIZE + 3] = (uint8_t)(chunk_len & 0xff);

			init_pkt[COMMON_HEADER_SIZE + 4] = 0x11;
			init_pkt[COMMON_HEADER_SIZE + 5] = 0x22;
			init_pkt[COMMON_HEADER_SIZE + 6] = 0x33;
			init_pkt[COMMON_HEADER_SIZE + 7] = 0x44;

			init_pkt[COMMON_HEADER_SIZE + 8] = 0x00;
			init_pkt[COMMON_HEADER_SIZE + 9] = 0x01;
			init_pkt[COMMON_HEADER_SIZE + 10] = 0x00;
			init_pkt[COMMON_HEADER_SIZE + 11] = 0x00;

			init_pkt[COMMON_HEADER_SIZE + 12] = 0x00;
			init_pkt[COMMON_HEADER_SIZE + 13] = 0x0a;

			init_pkt[COMMON_HEADER_SIZE + 14] = 0x00;
			init_pkt[COMMON_HEADER_SIZE + 15] = 0x0a;

			init_pkt[COMMON_HEADER_SIZE + 16] = 0x00;
			init_pkt[COMMON_HEADER_SIZE + 17] = 0x00;
			init_pkt[COMMON_HEADER_SIZE + 18] = 0x00;
			init_pkt[COMMON_HEADER_SIZE + 19] = 0x01;

			pkt_len = COMMON_HEADER_SIZE + 20 + params_len;

			ch = (struct sctp_common_header *)init_pkt;
			ch->verification_tag = 0;

			g_last_output_len = 0;
			usrsctp_conninput((void *)1, init_pkt, pkt_len, 0);

			if ((g_last_output_len >= (COMMON_HEADER_SIZE + 20)) &&
			    (g_last_output[0] == init_pkt[2]) && (g_last_output[1] == init_pkt[3]) &&
			    (g_last_output[COMMON_HEADER_SIZE] == SCTP_INITIATION_ACK)) {
				uint16_t resp_chunk_len = (uint16_t)(((uint16_t)g_last_output[COMMON_HEADER_SIZE + 2] << 8) |
				                                      g_last_output[COMMON_HEADER_SIZE + 3]);
				size_t chunk_end = COMMON_HEADER_SIZE + resp_chunk_len;
				size_t params_start = COMMON_HEADER_SIZE + 4 + 16;
				size_t val_off = 0;
				size_t val_len = 0;

				if ((chunk_end <= g_last_output_len) && (chunk_end >= params_start) &&
				    find_param(g_last_output, chunk_end, params_start, SCTP_STATE_COOKIE_PARAM,
				               &val_off, &val_len) &&
				    (val_len > 0) && (val_len <= 4096)) {
					size_t cookie_chunk_cap = 4 + val_len + 4;
					uint8_t *cookie_chunk = (uint8_t *)malloc(cookie_chunk_cap);

					if (cookie_chunk != NULL) {
						size_t pos = 0;

						pos = append_u8(cookie_chunk, cookie_chunk_cap, pos, SCTP_COOKIE_ECHO);
						pos = append_u8(cookie_chunk, cookie_chunk_cap, pos, 0);
						pos = append_u16(cookie_chunk, cookie_chunk_cap, pos, 0);
						pos = append_bytes(cookie_chunk, cookie_chunk_cap, pos,
						                    g_last_output + val_off, val_len);
						pos = finish_chunk(cookie_chunk, cookie_chunk_cap, pos);

						if (pos >= 4) {
							size_t full_pkt_len = COMMON_HEADER_SIZE + pos;
							uint8_t *full_pkt = (uint8_t *)malloc(full_pkt_len);

							if (full_pkt != NULL) {
								struct sctp_common_header *ch3;
								struct sctp_init_chunk *init_ack_chunk;

								memcpy(full_pkt, init_pkt, COMMON_HEADER_SIZE);
								memcpy(full_pkt + COMMON_HEADER_SIZE, cookie_chunk, pos);

								ch3 = (struct sctp_common_header *)full_pkt;
								init_ack_chunk = (struct sctp_init_chunk *)
								    (g_last_output + COMMON_HEADER_SIZE);
								ch3->verification_tag = init_ack_chunk->initiate_tag;

								usrsctp_conninput((void *)1, full_pkt, full_pkt_len, 0);

								free(full_pkt);
							}
						}
						free(cookie_chunk);
					}
				}
			}

			accepted = usrsctp_accept(g_listener_socket, NULL, NULL);
			if (accepted != NULL) {
				usrsctp_close(accepted);
			}

			free(init_pkt);
		}
	}

	{
		size_t avail_in = (data_size > 3) ? (data_size - 3) : 0;
		size_t alloc_size = COMMON_HEADER_SIZE + avail_in + 64;

		fuzz_packet_buffer = (char *)malloc(alloc_size > 0 ? alloc_size : 1);
		if (fuzz_packet_buffer != NULL) {
			size_t payload_len;
			size_t packet_len;

			memcpy(fuzz_packet_buffer, fuzz_common_header, COMMON_HEADER_SIZE);

			if (ctrl2 & FUZZ2_B_BUNDLE_CHUNKS) {
				payload_len = build_bundled_chunks(data + 3, avail_in,
				                                    (uint8_t *)(fuzz_packet_buffer + COMMON_HEADER_SIZE),
				                                    alloc_size - COMMON_HEADER_SIZE);
			} else {
				payload_len = avail_in;
				if (payload_len > alloc_size - COMMON_HEADER_SIZE) {
					payload_len = alloc_size - COMMON_HEADER_SIZE;
				}
				if (payload_len > 0) {
					memcpy(fuzz_packet_buffer + COMMON_HEADER_SIZE, data + 3, payload_len);
				}
			}

			packet_len = COMMON_HEADER_SIZE + payload_len;

			common_header = (struct sctp_common_header *)fuzz_packet_buffer;
			common_header->verification_tag = assoc_vtag;

			usrsctp_conninput((void *)1, fuzz_packet_buffer, packet_len, 0);

			free(fuzz_packet_buffer);
		}
	}

	{
		uint16_t t1 = 100;
		uint16_t t2 = 500;
		uint16_t t3 = 2000;

		if (data_size >= 6) {
			t1 = (uint16_t)(((uint16_t)data[data_size - 6] << 8) | data[data_size - 5]);
			t2 = (uint16_t)(((uint16_t)data[data_size - 4] << 8) | data[data_size - 3]);
			t3 = (uint16_t)(((uint16_t)data[data_size - 2] << 8) | data[data_size - 1]);
		}

		usrsctp_handle_timers(t1);
		usrsctp_handle_timers(t2);
		usrsctp_handle_timers(t3);
	}

	usrsctp_close(socket_client);

	return (0);
}