/* Gold harness replaced by an empty stub for baseline generation. */
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
#define QF_EXTERN extern "C"
#else
#define QF_EXTERN extern
#endif
/* Keep the target function linked (no call, no arguments). */
QF_EXTERN void qf_target_0(void) __asm__("_ZNK13sentencepiece22SentencePieceProcessor6EncodeENSt3__117basic_string_viewIcNS1_11char_traitsIcEEEEPNS1_6vectorINS1_12basic_stringIcS4_NS1_9allocatorIcEEEENS8_ISA_EEEE");
QF_EXTERN void qf_target_1(void) __asm__("_ZNK13sentencepiece22SentencePieceProcessor6EncodeENSt3__117basic_string_viewIcNS1_11char_traitsIcEEEEPNS1_6vectorIiNS1_9allocatorIiEEEE");
QF_EXTERN void qf_target_2(void) __asm__("_ZNK13sentencepiece22SentencePieceProcessor6EncodeENSt3__117basic_string_viewIcNS1_11char_traitsIcEEEEPNS_17SentencePieceTextE");
__attribute__((used)) static void (*const qf_keep[])(void) = {qf_target_0, qf_target_1, qf_target_2};
QF_EXTERN int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  (void)data;
  (void)size;
  return 0;
}
