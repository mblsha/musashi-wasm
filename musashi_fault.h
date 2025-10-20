#ifndef MUSASHI_FAULT_H_
#define MUSASHI_FAULT_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum musashi_fault_kind {
  MUSASHI_FAULT_KIND_NONE = 0,
  MUSASHI_FAULT_KIND_BUS_ERROR = 1,
  MUSASHI_FAULT_KIND_ADDRESS_ERROR = 2,
  MUSASHI_FAULT_KIND_ILLEGAL_INSTRUCTION = 3,
  MUSASHI_FAULT_KIND_TRAP = 4,
  MUSASHI_FAULT_KIND_PRIVILEGE_VIOLATION = 5,
  MUSASHI_FAULT_KIND_UNKNOWN = 255
} musashi_fault_kind_t;

typedef struct musashi_fault_record {
  uint32_t active;
  uint32_t kind;
  uint32_t vector;
  uint32_t address;
  uint32_t size;
  uint32_t pc;
  uint32_t ppc;
  uint32_t sp;
  uint32_t sr;
  uint32_t opcode;
  uint32_t extra;
} musashi_fault_record_t;

void m68k_fault_clear(void);
musashi_fault_record_t* m68k_fault_record_ptr(void);
void m68k_fault_capture(musashi_fault_kind_t kind,
                        uint32_t vector,
                        uint32_t address,
                        uint32_t size,
                        uint32_t extra);

#ifdef __cplusplus
}
#endif

#endif  // MUSASHI_FAULT_H_
