#include <assert.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>

#include <gnutls/gnutls.h>

#include "certs.h"
#include "mem.h"
#include "fuzzer.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	IGNORE_CERTS;

	int res;
	gnutls_session_t session;
	gnutls_certificate_credentials_t rawpk_cred;
	struct mem_st memdata;
	uint8_t selector;
	const uint8_t *payload;
	size_t payload_size;
	const char *prio;

	if (size < 1)
		return 0;

	selector = data[0];
	payload = data + 1;
	payload_size = size - 1;

	res = gnutls_init(&session, GNUTLS_SERVER | GNUTLS_ENABLE_RAWPK);
	if (res < 0)
		return 0;

	res = gnutls_certificate_allocate_credentials(&rawpk_cred);
	if (res < 0) {
		gnutls_deinit(session);
		return 0;
	}

	res = gnutls_certificate_set_rawpk_key_mem(
		rawpk_cred, &rawpk_public_key1, &rawpk_private_key1,
		GNUTLS_X509_FMT_PEM, NULL, 0, NULL, 0, 0);
	if (res < 0) {
		gnutls_certificate_free_credentials(rawpk_cred);
		gnutls_deinit(session);
		return 0;
	}

	gnutls_certificate_set_known_dh_params(rawpk_cred,
					       GNUTLS_SEC_PARAM_MEDIUM);

	res = gnutls_credentials_set(session, GNUTLS_CRD_CERTIFICATE,
				     rawpk_cred);
	if (res < 0) {
		gnutls_certificate_free_credentials(rawpk_cred);
		gnutls_deinit(session);
		return 0;
	}

	switch (selector & 0x3) {
	case 0:
		prio = "NORMAL:" VERS_STR
		       ":-CTYPE-ALL:+CTYPE-CLI-RAWPK:+CTYPE-SRV-RAWPK";
		break;
	case 1:
		prio = "NORMAL:-VERS-TLS-ALL:+VERS-TLS1.3"
		       ":-CTYPE-ALL:+CTYPE-CLI-RAWPK:+CTYPE-SRV-RAWPK";
		break;
	case 2:
		prio = "NORMAL:-VERS-TLS-ALL:+VERS-TLS1.2"
		       ":-CTYPE-ALL:+CTYPE-CLI-RAWPK:+CTYPE-SRV-RAWPK";
		break;
	default:
		prio = "NORMAL:" VERS_STR;
		break;
	}

	res = gnutls_priority_set_direct(session, prio, NULL);
	if (res < 0) {
		gnutls_certificate_free_credentials(rawpk_cred);
		gnutls_deinit(session);
		return 0;
	}

	memdata.data = payload;
	memdata.size = payload_size;

	gnutls_transport_set_push_function(session, mem_push);
	gnutls_transport_set_pull_function(session, mem_pull);
	gnutls_transport_set_pull_timeout_function(session, mem_pull_timeout);
	gnutls_transport_set_ptr(session, &memdata);

	do {
		res = gnutls_handshake(session);
	} while (res < 0 && gnutls_error_is_fatal(res) == 0);

	if (res >= 0) {

		for (;;) {
			char buf[4096];
			int rret;

			rret = gnutls_record_recv(session, buf, sizeof(buf));
			if (rret <= 0)
				break;

			do {
				res = gnutls_record_send(session, buf, rret);
			} while (res == GNUTLS_E_AGAIN ||
				 res == GNUTLS_E_INTERRUPTED);
			if (res < 0)
				break;
		}

		if (selector & 0x4) {
			int rres = gnutls_rehandshake(session);

			if (rres >= 0) {
				do {
					rres = gnutls_handshake(session);
				} while (rres < 0 &&
					 gnutls_error_is_fatal(rres) == 0);
			}
		} else if (selector & 0x8) {
			gnutls_session_key_update(session, GNUTLS_KU_PEER);
		}

		gnutls_bye(session, GNUTLS_SHUT_WR);
	}

	gnutls_deinit(session);
	gnutls_certificate_free_credentials(rawpk_cred);
	return 0;
}
