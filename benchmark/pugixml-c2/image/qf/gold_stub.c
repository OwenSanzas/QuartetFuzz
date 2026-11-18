/* Gold harness replaced by an empty stub for baseline generation. */
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
#define QF_EXTERN extern "C"
#else
#define QF_EXTERN extern
#endif
/* Keep the target function linked (no call, no arguments). */
QF_EXTERN void qf_target_0(void) __asm__("_ZN4pugi11xpath_queryC1EOS0_");
QF_EXTERN void qf_target_1(void) __asm__("_ZN4pugi11xpath_queryC1EPKcPNS_18xpath_variable_setE");
QF_EXTERN void qf_target_2(void) __asm__("_ZN4pugi11xpath_queryC1Ev");
QF_EXTERN void qf_target_3(void) __asm__("_ZN4pugi11xpath_queryC2EOS0_");
QF_EXTERN void qf_target_4(void) __asm__("_ZN4pugi11xpath_queryC2EPKcPNS_18xpath_variable_setE");
QF_EXTERN void qf_target_5(void) __asm__("_ZN4pugi11xpath_queryC2Ev");
__attribute__((used)) static void (*const qf_keep[])(void) = {qf_target_0, qf_target_1, qf_target_2, qf_target_3, qf_target_4, qf_target_5};
QF_EXTERN int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  (void)data;
  (void)size;
  return 0;
}
