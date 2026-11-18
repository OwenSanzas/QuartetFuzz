#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <usrsctp.h>

#define FUZZ_B_INJECT_INIT_ACK        (1 << 0)
#define FUZZ_B_INJECT_COOKIE_ACK      (1 << 1)
#define FUZZ_B_SEND_DATA              (1 << 2)
#define FUZZ_B_SEND_STREAM_RESET      (1 << 3)
#define FUZZ_B_INJECT_DATA            (1 << 4)
#define FUZZ_B_I_DATA_SUPPORT         (1 << 5)
#define FUZZ_B_SEND_DATA_FORCE        (1 << 6)
#define FUZZ_B_SERVER_PATH            (1 << 7)

#define BUFFER_SIZE 4096
#define COMMON_HEADER_SIZE 12

#if !defined(SCTP_INTERLEAVING_SUPPORTED)
#define SCTP_INTERLEAVING_SUPPORTED 0x00001206
#endif

static uint32_t assoc_vtag = 0;

static int
conn_output(void *addr, void *buf, size_t length, uint8_t tos, uint8_t set_df)
{
	struct sctp_init_chunk {
		uint8_t chunk_type;
		uint8_t chunk_flags;
		uint16_t chunk_length;
		uint32_t initiate_tag;
	} *init_chunk;
	(void)addr;
	(void)tos;
	(void)set_df;

	if (length >= (COMMON_HEADER_SIZE + 16)) {
		init_chunk = (struct sctp_init_chunk *)((char *)buf + COMMON_HEADER_SIZE);
		if (init_chunk->chunk_type == 1) {
			assoc_vtag = init_chunk->initiate_tag;
		}
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
		struct sctp_recvv_rn rn;
		ssize_t n;
		struct sockaddr_in addr;
		char buf[BUFFER_SIZE];
		int flags = 0;
		socklen_t len = (socklen_t)sizeof(addr);
		unsigned int infotype = 0;
		socklen_t infolen = sizeof(rn);
		memset(&rn, 0, sizeof(rn));

		n = usrsctp_recvv(sock, buf, sizeof(buf), (struct sockaddr *)&addr,
		                   &len, (void *)&rn, &infolen, &infotype, &flags);

		if (n <= 0) {
			break;
		}
		events = usrsctp_get_events(sock);
	}
}

static int
initialize_fuzzer(void)
{
	usrsctp_init(0, conn_output, NULL);
	usrsctp_enable_crc32c_offload();
	usrsctp_register_address((void *)1);
	usrsctp_sysctl_set_sctp_pktdrop_enable(1);
	usrsctp_sysctl_set_sctp_nrsack_enable(1);
	return (1);
}

int
LLVMFuzzerTestOneInput(const uint8_t *data, size_t data_size)
{
	static int initialized;
	char *fuzz_packet_buffer;
	struct sockaddr_conn sconn;
	struct socket *socket_client;
	struct socket *socket_accepted;
	struct linger so_linger;
	struct sctp_event event;
	unsigned long i;
	struct sctp_common_header *common_header;
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
		SCTP_SEND_FAILED_EVENT
	};
	int optval;
	int result;
	struct sctp_initmsg initmsg;
	struct sctp_assoc_value assoc_val;

	static const unsigned char fuzz_init_ack[] = {
		0x13,0x89,0x13,0x88,0x49,0xa4,0xac,0xb2,0x00,0x00,0x00,0x00,0x02,0x00,0x01,0xb4,
		0x2b,0xe8,0x47,0x40,0x00,0x1c,0x71,0xc7,0xff,0xff,0xff,0xff,0xed,0x69,0x58,0xec,
		0xc0,0x06,0x00,0x08,0x00,0x00,0x07,0xc4,0x80,0x00,0x00,0x04,0xc0,0x00,0x00,0x04,
		0x80,0x08,0x00,0x0b,0xc0,0xc2,0x0f,0xc1,0x80,0x82,0x40,0x00,0x80,0x02,0x00,0x24,
		0x40,0x39,0xcf,0x32,0xd6,0x60,0xcf,0xfa,0x3f,0x2f,0xa9,0x52,0xed,0x2b,0xf2,0xe6,
		0x2f,0xb7,0x81,0x96,0xf8,0xda,0xe9,0xa0,0x62,0x01,0x79,0xe1,0x0d,0x5f,0x38,0xaa,
		0x80,0x04,0x00,0x08,0x00,0x03,0x00,0x01,0x80,0x03,0x00,0x06,0x80,0xc1,0x00,0x00,
		0x00,0x07,0x01,0x50,0x4b,0x41,0x4d,0x45,0x2d,0x42,0x53,0x44,0x20,0x31,0x2e,0x31,
		0x00,0x00,0x00,0x00,0x64,0xdb,0x63,0x00,0x00,0x00,0x00,0x00,0xc9,0x76,0x03,0x00,
		0x00,0x00,0x00,0x00,0x60,0xea,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
		0xb2,0xac,0xa4,0x49,0x2b,0xe8,0x47,0x40,0xd4,0xc9,0x79,0x52,0x00,0x00,0x00,0x00,
		0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x05,0x00,0x00,0x00,0xd4,0xc9,0x79,0x53,
		0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x05,0x00,0x00,0x00,
		0x00,0x00,0x00,0x00,0x5a,0x76,0x13,0x89,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
		0x00,0x00,0x00,0x00,0x01,0x00,0x00,0x62,0x49,0xa4,0xac,0xb2,0x00,0x1c,0x71,0xc7,
		0x00,0x01,0xff,0xff,0x82,0xe6,0xc8,0x44,0x80,0x00,0x00,0x04,0xc0,0x00,0x00,0x04,
		0x80,0x08,0x00,0x0b,0xc0,0xc2,0x0f,0xc1,0x80,0x82,0x40,0x00,0x80,0x02,0x00,0x24,
		0xb6,0xbb,0xb5,0x7f,0xbb,0x4b,0x0e,0xb5,0x42,0xf6,0x75,0x18,0x4f,0x79,0x0f,0x24,
		0x1c,0x44,0x0b,0xd6,0x62,0xa9,0x84,0xe7,0x2c,0x3c,0x7f,0xad,0x1b,0x67,0x81,0x57,
		0x80,0x04,0x00,0x08,0x00,0x03,0x00,0x01,0x80,0x03,0x00,0x06,0x80,0xc1,0x00,0x00,
		0x00,0x0c,0x00,0x06,0x00,0x05,0x00,0x00,0x02,0x00,0x01,0xb4,0x2b,0xe8,0x47,0x40,
		0x00,0x1c,0x71,0xc7,0x00,0x01,0xff,0xff,0xed,0x69,0x58,0xec,0xc0,0x06,0x00,0x08,
		0x00,0x00,0x07,0xc4,0x80,0x00,0x00,0x04,0xc0,0x00,0x00,0x04,0x80,0x08,0x00,0x0b,
		0xc0,0xc2,0x0f,0xc1,0x80,0x82,0x40,0x00,0x80,0x02,0x00,0x24,0x40,0x39,0xcf,0x32,
		0xd6,0x60,0xcf,0xfa,0x3f,0x2f,0xa9,0x52,0xed,0x2b,0xf2,0xe6,0x2f,0xb7,0x81,0x96,
		0xf8,0xda,0xe9,0xa0,0x62,0x01,0x79,0xe1,0x0d,0x5f,0x38,0xaa,0x80,0x04,0x00,0x08,
		0x00,0x03,0x00,0x01,0x80,0x03,0x00,0x06,0x80,0xc1,0x00,0x00,0x81,0xe1,0x1e,0x81,
		0xea,0x41,0xeb,0xf0,0x12,0xd9,0x74,0xbe,0x13,0xfd,0x4b,0x6c,0x5c,0xa2,0x8f,0x00
	};

	static const unsigned char fuzz_cookie_ack[] = {
		0x13,0x89,0x13,0x88,0x54,0xc2,0x7c,0x46,0x00,0x00,0x00,0x00,0x0b,0x00,0x00,0x04
	};

	static const unsigned char fuzz_common_header[] = {
		0x13,0x89,0x13,0x88,0x54,0xc2,0x7c,0x46,0x00,0x00,0x00,0x00
	};

	unsigned char control;
	uint8_t ecn_bits;
	size_t body_off;

	if (data_size < 1) {
		return (0);
	}
	control = data[0];

	if (data_size >= 2) {
		ecn_bits = (uint8_t)data[1];
		body_off = 2;
	} else {
		ecn_bits = 0;
		body_off = 1;
	}

	if (!initialized) {
		initialized = initialize_fuzzer();
	}

	if (data_size < 5 || data_size > 65535) {

		return (0);
	}

	socket_client = usrsctp_socket(AF_CONN, SOCK_STREAM, IPPROTO_SCTP, NULL, NULL, 0, 0);
	if (socket_client == NULL) {
		return (0);
	}

	usrsctp_set_non_blocking(socket_client, 1);

	memset(&initmsg, 1, sizeof(initmsg));
	usrsctp_setsockopt(socket_client, IPPROTO_SCTP, SCTP_INITMSG, &initmsg, sizeof(initmsg));

	so_linger.l_onoff = 1;
	so_linger.l_linger = 0;
	usrsctp_setsockopt(socket_client, SOL_SOCKET, SO_LINGER, &so_linger, sizeof(so_linger));

	memset(&event, 0, sizeof(event));
	event.se_on = 1;
	for (i = 0; i < (sizeof(event_types) / sizeof(uint16_t)); i++) {
		event.se_type = event_types[i];
		usrsctp_setsockopt(socket_client, IPPROTO_SCTP, SCTP_EVENT, &event, sizeof(event));
	}

	optval = 1;
	usrsctp_setsockopt(socket_client, IPPROTO_SCTP, SCTP_RECVRCVINFO, &optval, sizeof(optval));

	optval = 1;
	usrsctp_setsockopt(socket_client, IPPROTO_SCTP, SCTP_RECVNXTINFO, &optval, sizeof(optval));

	memset(&assoc_val, 0, sizeof(assoc_val));
	assoc_val.assoc_id = SCTP_ALL_ASSOC;
	assoc_val.assoc_value = SCTP_ENABLE_RESET_STREAM_REQ | SCTP_ENABLE_RESET_ASSOC_REQ | SCTP_ENABLE_CHANGE_ASSOC_REQ;
	usrsctp_setsockopt(socket_client, IPPROTO_SCTP, SCTP_ENABLE_STREAM_RESET, &assoc_val, sizeof(assoc_val));

	if (control & FUZZ_B_I_DATA_SUPPORT) {
		optval = 2;
		usrsctp_setsockopt(socket_client, IPPROTO_SCTP, SCTP_FRAGMENT_INTERLEAVE, &optval, sizeof(optval));

		memset(&assoc_val, 0, sizeof(assoc_val));
		assoc_val.assoc_value = 1;
		usrsctp_setsockopt(socket_client, IPPROTO_SCTP, SCTP_INTERLEAVING_SUPPORTED, &assoc_val, sizeof(assoc_val));
	}

	optval = 1;
	usrsctp_setsockopt(socket_client, IPPROTO_SCTP, SCTP_REUSE_PORT, &optval, sizeof(optval));

	memset(&sconn, 0, sizeof(sconn));
	sconn.sconn_family = AF_CONN;
	sconn.sconn_port = htons(5000);
	sconn.sconn_addr = (void *)1;

	result = usrsctp_bind(socket_client, (struct sockaddr *)&sconn, sizeof(sconn));
	if (result != 0) {
		usrsctp_close(socket_client);
		return (0);
	}

	optval = 1;
	usrsctp_setsockopt(socket_client, IPPROTO_SCTP, SCTP_NODELAY, &optval, sizeof(optval));

	usrsctp_set_upcall(socket_client, handle_upcall, NULL);

	socket_accepted = NULL;

	if (control & FUZZ_B_SERVER_PATH) {
		usrsctp_listen(socket_client, 1);
	} else {
		memset(&sconn, 0, sizeof(sconn));
		sconn.sconn_family = AF_CONN;
		sconn.sconn_port = htons(5001);
		sconn.sconn_addr = (void *)1;

		usrsctp_connect(socket_client, (struct sockaddr *)&sconn, sizeof(sconn));

		if (control & FUZZ_B_INJECT_INIT_ACK) {
			unsigned char pkt[sizeof(fuzz_init_ack)];
			memcpy(pkt, fuzz_init_ack, sizeof(pkt));
			common_header = (struct sctp_common_header *)pkt;
			common_header->verification_tag = assoc_vtag;
			usrsctp_conninput((void *)1, pkt, sizeof(pkt), ecn_bits);
		}

		if (control & FUZZ_B_INJECT_COOKIE_ACK) {
			unsigned char pkt[sizeof(fuzz_cookie_ack)];
			memcpy(pkt, fuzz_cookie_ack, sizeof(pkt));
			common_header = (struct sctp_common_header *)pkt;
			common_header->verification_tag = assoc_vtag;
			usrsctp_conninput((void *)1, pkt, sizeof(pkt), ecn_bits);
		}

		if ((control & FUZZ_B_INJECT_INIT_ACK) &&
		    (control & FUZZ_B_INJECT_COOKIE_ACK) &&
		    (control & FUZZ_B_SEND_DATA)) {
			const char *sendbuffer = "Geologie ist keine richtige Wissenschaft!";
			usrsctp_sendv(socket_client, sendbuffer, strlen(sendbuffer), NULL, 0, NULL, 0, SCTP_SENDV_NOINFO, 0);
		}

		if ((control & FUZZ_B_INJECT_INIT_ACK) &&
		    (control & FUZZ_B_INJECT_COOKIE_ACK) &&
		    (control & FUZZ_B_SEND_STREAM_RESET)) {
			struct sctp_reset_streams srs;
			memset(&srs, 0, sizeof(srs));
			srs.srs_flags = SCTP_STREAM_RESET_INCOMING | SCTP_STREAM_RESET_OUTGOING;
			usrsctp_setsockopt(socket_client, IPPROTO_SCTP, SCTP_RESET_STREAMS, &srs, sizeof(srs));
		}

		if ((control & FUZZ_B_I_DATA_SUPPORT) && (control & FUZZ_B_SEND_DATA_FORCE)) {
			const char *sendbuffer = "Geologie ist keine richtige Wissenschaft!";
			usrsctp_sendv(socket_client, sendbuffer, strlen(sendbuffer), NULL, 0, NULL, 0, SCTP_SENDV_NOINFO, 0);
		}
	}

	fuzz_packet_buffer = malloc(data_size - body_off + COMMON_HEADER_SIZE);
	if (fuzz_packet_buffer != NULL) {
		memcpy(fuzz_packet_buffer, fuzz_common_header, COMMON_HEADER_SIZE);
		memcpy(fuzz_packet_buffer + COMMON_HEADER_SIZE, data + body_off, data_size - body_off);

		common_header = (struct sctp_common_header *)fuzz_packet_buffer;
		if (!(control & FUZZ_B_SERVER_PATH)) {
			common_header->verification_tag = assoc_vtag;
		}

		usrsctp_conninput((void *)1, fuzz_packet_buffer, data_size - body_off + COMMON_HEADER_SIZE, ecn_bits);

		free(fuzz_packet_buffer);
	}

	usrsctp_conninput((void *)1, data, data_size, ecn_bits);

	if (control & FUZZ_B_SERVER_PATH) {

		socket_accepted = usrsctp_accept(socket_client, NULL, NULL);
		if (socket_accepted != NULL) {
			handle_upcall(socket_accepted, NULL, 0);
			usrsctp_close(socket_accepted);
		}
	}

	usrsctp_close(socket_client);

	return (0);
}
