#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <gnutls/gnutls.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

typedef struct {
    const uint8_t *data;
    size_t size;
} fuzz_transport_t;

static ssize_t fuzz_pull(gnutls_transport_ptr_t ptr, void *buf, size_t len)
{
    fuzz_transport_t *t = (fuzz_transport_t *)ptr;

    if (t->size == 0) {
        errno = EAGAIN;
        return -1;
    }

    size_t n = (len < t->size) ? len : t->size;
    memcpy(buf, t->data, n);
    t->data += n;
    t->size -= n;

    return (ssize_t)n;
}

static ssize_t fuzz_push(gnutls_transport_ptr_t ptr, const void *buf, size_t len)
{
    (void)ptr;
    (void)buf;

    return (ssize_t)len;
}

static int fuzz_pull_timeout(gnutls_transport_ptr_t ptr, unsigned int ms)
{
    (void)ms;
    fuzz_transport_t *t = (fuzz_transport_t *)ptr;
    return (t->size > 0) ? 1 : 0;
}

static gnutls_anon_server_credentials_t anon_cred = NULL;
static gnutls_dh_params_t dh_params_g = NULL;
static int gnutls_initialized = 0;

static void fuzz_global_init(void)
{
    gnutls_global_init();

    if (gnutls_anon_allocate_server_credentials(&anon_cred) < 0) {
        anon_cred = NULL;
        return;
    }

    if (gnutls_dh_params_init(&dh_params_g) < 0) {
        dh_params_g = NULL;
        return;
    }

    if (gnutls_dh_params_generate2(dh_params_g, 1024) < 0) {
        gnutls_dh_params_deinit(dh_params_g);
        dh_params_g = NULL;
        return;
    }

    gnutls_anon_set_server_dh_params(anon_cred, dh_params_g);

    gnutls_initialized = 1;
}

int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size)
{
    if (!gnutls_initialized) {
        fuzz_global_init();
    }

    if (!anon_cred) {

        return 0;
    }

    gnutls_session_t session;
    if (gnutls_init(&session, GNUTLS_SERVER) != GNUTLS_E_SUCCESS) {
        return 0;
    }

    const char *err_pos = NULL;
    if (gnutls_priority_set_direct(session,
                                   "NORMAL:+ANON-ECDH:+ANON-DH",
                                   &err_pos) != GNUTLS_E_SUCCESS) {
        gnutls_deinit(session);
        return 0;
    }

    if (gnutls_credentials_set(session, GNUTLS_CRD_ANON, anon_cred) != GNUTLS_E_SUCCESS) {
        gnutls_deinit(session);
        return 0;
    }

    fuzz_transport_t transport;
    transport.data = Data;
    transport.size = Size;

    gnutls_transport_set_ptr(session, (gnutls_transport_ptr_t)&transport);
    gnutls_transport_set_pull_function(session, fuzz_pull);
    gnutls_transport_set_push_function(session, fuzz_push);
    gnutls_transport_set_pull_timeout_function(session, fuzz_pull_timeout);

    gnutls_handshake_set_timeout(session, 0);

    int ret;
    do {
        ret = gnutls_handshake(session);
    } while (ret < 0 && gnutls_error_is_fatal(ret) == 0);

    gnutls_bye(session, GNUTLS_SHUT_WR);

    gnutls_deinit(session);

    return 0;
}