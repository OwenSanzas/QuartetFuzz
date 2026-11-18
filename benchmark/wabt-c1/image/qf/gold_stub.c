/* Gold harness replaced by an empty stub for baseline generation. */
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
#define QF_EXTERN extern "C"
#else
#define QF_EXTERN extern
#endif
/* Keep the target function linked (no call, no arguments). */
QF_EXTERN void qf_target_0(void) __asm__("_ZN4wabt12ReadBinaryIrEPKcNSt3__14spanIKhLm18446744073709551615EEERKNS_17ReadBinaryOptionsEPNS2_6vectorINS_5ErrorENS2_9allocatorISA_EEEEPNS_6ModuleE");
QF_EXTERN void qf_target_1(void) __asm__("_ZN4wabt12ReadBinaryIrEPKcPKhmRKNS_17ReadBinaryOptionsEPNSt3__16vectorINS_5ErrorENS7_9allocatorIS9_EEEEPNS_6ModuleE");
__attribute__((used)) static void (*const qf_keep[])(void) = {qf_target_0, qf_target_1};
QF_EXTERN int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  (void)data;
  (void)size;
  return 0;
}
