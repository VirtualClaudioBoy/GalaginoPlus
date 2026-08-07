/*
 * M6803 CPU Emulator for Galagino
 * Lightweight Motorola 6803 emulator for ESP32.
 * Used by Irem M52 sound board (MotoRace, Moon Patrol, etc.)
 *
 * M6803 is a single-chip MCU variant of M6800 with:
 *   - 128 bytes internal RAM (0x0080-0x00FF)
 *   - 2 I/O ports (Port 1, Port 2)
 *   - Built-in timer (simplified here)
 *   - Same instruction set as M6800 + a few extras (ABX, etc.)
 *
 * Registers: A, B (8-bit accumulators), X (16-bit index),
 *            SP (16-bit stack), PC (16-bit program counter),
 *            CC (condition codes: HINZVC)
 */

#ifndef M6803_H
#define M6803_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Condition Code register bits */
#define M6803_CC_C  0x01  /* Carry */
#define M6803_CC_V  0x02  /* Overflow */
#define M6803_CC_Z  0x04  /* Zero */
#define M6803_CC_N  0x08  /* Negative */
#define M6803_CC_I  0x10  /* IRQ mask */
#define M6803_CC_H  0x20  /* Half carry */

typedef struct m6803_state_S {
    uint8_t  A, B;       /* Accumulators */
    uint16_t X;          /* Index register */
    uint16_t SP;         /* Stack pointer */
    uint16_t PC;         /* Program counter */
    uint8_t  CC;         /* Condition code register */

    uint8_t  irq_pending;
    uint8_t  wai_state;  /* waiting for interrupt (WAI instruction) */

    /* Internal I/O ports */
    uint8_t  port1_data;
    uint8_t  port2_data;
    uint8_t  port1_ddr;
    uint8_t  port2_ddr;

    /* Internal timer */
    uint16_t timer_counter;
    uint16_t timer_output_compare;
    uint8_t  tcsr;           /* Timer Control/Status Register */
    uint8_t  timer_latch_h;  /* Latched counter high byte */
    uint8_t  tcsr_read;      /* TCSR was read (for OCF clear sequence) */

    int      cycles;
} m6803_state;

/* Function pointer callbacks for external memory access */
typedef uint8_t (*m6803_read_fn_t)(uint16_t addr);
typedef void (*m6803_write_fn_t)(uint16_t addr, uint8_t val);

extern m6803_read_fn_t m6803_ext_read_fn;

/* Direct access to M6803 internal RAM (128 bytes, mapped at 0x0080-0x00FF) */
extern uint8_t m6803_internal_ram[128];
extern m6803_write_fn_t m6803_ext_write_fn;

/* Port I/O callbacks (optional, for direct port access) */
typedef uint8_t (*m6803_port_read_fn_t)(uint8_t port);
typedef void (*m6803_port_write_fn_t)(uint8_t port, uint8_t val);

extern m6803_port_read_fn_t m6803_port_read_fn;
extern m6803_port_write_fn_t m6803_port_write_fn;

/* Reset CPU - reads reset vector from 0xFFFE */
void m6803_reset(m6803_state *s);

/* Execute one instruction, returns cycles consumed */
int m6803_step(m6803_state *s);

/* Signal IRQ */
void m6803_irq(m6803_state *s);

/* Signal NMI */
void m6803_nmi(m6803_state *s);

#ifdef __cplusplus
}
#endif

#endif /* M6803_H */
