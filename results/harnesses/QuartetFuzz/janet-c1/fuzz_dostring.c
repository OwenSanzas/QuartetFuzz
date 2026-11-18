#include <stdint.h>
#include <string.h>
#include <janet.h>

int __lsan_is_turned_off(void) {
    return 1;
}

static void drain_and_use(JanetParser *parser, JanetTable *env, const uint8_t *where, int use_wrapped) {
    while (janet_parser_has_more(parser)) {
        Janet form;
        if (use_wrapped) {

            Janet wrapped = janet_parser_produce_wrapped(parser);
            if (janet_checktype(wrapped, JANET_TUPLE)) {
                const Janet *tup = janet_unwrap_tuple(wrapped);
                form = tup[0];
            } else {
                form = wrapped;
            }
        } else {
            form = janet_parser_produce(parser);
        }

        JanetString formstr = janet_to_string(form);
        (void) formstr;

        JanetCompileResult cres = janet_compile(form, env, where);
        if (cres.status == JANET_COMPILE_OK) {
            JanetFunction *f = janet_thunk(cres.funcdef);
            JanetFiber *fiber = janet_fiber(f, 64, 0, NULL);
            if (fiber != NULL) {
                fiber->env = env;
                Janet ret;
                JanetSignal status = janet_continue(fiber, janet_wrap_nil(), &ret);
                if (status != JANET_SIGNAL_OK && status != JANET_SIGNAL_EVENT) {
                    JanetString errstr = janet_to_string(ret);
                    (void) errstr;
                }
            }
        } else {
            JanetString errstr = janet_to_string(janet_wrap_string(cres.error));
            (void) errstr;
        }
    }
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size < 1) return 0;

    uint8_t selector = data[0];
    int use_wrapped = selector & 1;
    int allow_recovery = (selector & 2) != 0;
    const uint8_t *src = data + 1;
    size_t len = size - 1;

    janet_init();
    JanetTable *env = janet_core_env(NULL);
    const uint8_t *where = janet_cstring("fuzz");

    JanetParser parser;
    janet_parser_init(&parser);

    JanetTryState tstate;
    if (janet_try(&tstate) == JANET_SIGNAL_OK) {
        size_t i = 0;
        while (i < len) {
            enum JanetParserStatus st = janet_parser_status(&parser);
            if (st == JANET_PARSE_DEAD) {
                break;
            }
            if (st == JANET_PARSE_ERROR) {

                if (!allow_recovery) {
                    break;
                }

                (void) janet_parser_error(&parser);
                allow_recovery = 0;
                continue;
            }
            janet_parser_consume(&parser, src[i++]);
            drain_and_use(&parser, env, where, use_wrapped);
        }

        enum JanetParserStatus st = janet_parser_status(&parser);
        if (st == JANET_PARSE_ROOT || st == JANET_PARSE_PENDING) {
            janet_parser_eof(&parser);
            drain_and_use(&parser, env, where, use_wrapped);
        }
        if (janet_parser_status(&parser) == JANET_PARSE_ERROR) {
            (void) janet_parser_error(&parser);
        }
    }
    janet_restore(&tstate);

    janet_parser_deinit(&parser);

    janet_deinit();

    return 0;
}
