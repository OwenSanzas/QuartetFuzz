/* Gold harness replaced by an empty stub for baseline generation. */
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
#define QF_EXTERN extern "C"
#else
#define QF_EXTERN extern
#endif
/* Keep the target function linked (no call, no arguments). */
QF_EXTERN void qf_target_0(void) __asm__("_ZN5woff217ConvertWOFF2ToTTFEPKhmPNS_8WOFF2OutE");
QF_EXTERN void qf_target_1(void) __asm__("_ZN5woff217ConvertWOFF2ToTTFEPhmPKhm");
__attribute__((used)) static void (*const qf_keep[])(void) = {qf_target_0, qf_target_1};
QF_EXTERN int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  (void)data;
  (void)size;
  return 0;
}
