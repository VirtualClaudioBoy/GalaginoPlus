#ifndef M6502_H
#define M6502_H

#include <stdint.h>

// Place hot functions in IRAM on ESP32 to avoid flash cache misses
#if defined(ESP32) || defined(ESP_PLATFORM)
#  include "esp_attr.h"
#  define M6502_IRAM IRAM_ATTR
#else
#  define M6502_IRAM
#endif

#define M6502_FC 0x01
#define M6502_FZ 0x02
#define M6502_FI 0x04
#define M6502_FD 0x08
#define M6502_FB 0x10
#define M6502_FU 0x20
#define M6502_FV 0x40
#define M6502_FN 0x80

typedef struct m6502_s {
    uint16_t pc;
    uint8_t  a, x, y, sp, p;
    uint8_t (*read) (struct m6502_s *cpu, uint16_t addr);
    void    (*write)(struct m6502_s *cpu, uint16_t addr, uint8_t val);
    // Optional opcode-fetch hook. When non-NULL it is used INSTEAD of read()
    // for the first byte of each instruction only (operands still use read()).
    // NULL (default) means fetch == read, no overhead. Used by the Data East
    // encrypted CPUs (DECO CPU-7 / C10707) whose opcodes are bit-swapped at
    // fetch time while data reads stay raw.
    uint8_t (*fetch)(struct m6502_s *cpu, uint16_t addr);
    void    *user;
    uint8_t  irq;
    uint8_t  nmi;
    // Set to 1 for CPU variants whose ALU hardware ignores the Decimal flag
    // (e.g. Ricoh RP2A03/2A07 used in the NES and dkong3's sound CPUs: the
    // BCD circuitry was omitted in silicon, SED/CLD still set the flag bit
    // but ADC/SBC always compute in binary regardless). Default 0 (from
    // struct zero-init) = real 6502 behaviour, decimal mode works normally.
    uint8_t  decimal_disabled;
} m6502_t;

#ifdef __cplusplus
extern "C" {
#endif

void       m6502_reset(m6502_t *cpu);
M6502_IRAM int  m6502_step (m6502_t *cpu);
M6502_IRAM void m6502_exec (m6502_t *cpu, int cycles);

#ifdef __cplusplus
}
#endif

#endif
