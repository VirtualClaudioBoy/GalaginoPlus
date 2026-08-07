/*
 * M6803 CPU Emulator for Galagino
 * Lightweight implementation for ESP32 arcade emulation.
 * Covers the M6800/M6803 instruction set needed by Irem M52 sound board.
 */

#include "m6803.h"

/* Function pointer callbacks */
m6803_read_fn_t m6803_ext_read_fn = 0;
m6803_write_fn_t m6803_ext_write_fn = 0;
m6803_port_read_fn_t m6803_port_read_fn = 0;
m6803_port_write_fn_t m6803_port_write_fn = 0;

/* Internal RAM (accessible externally for hardware signal simulation) */
uint8_t m6803_internal_ram[128];

/* ============================================================
 * Memory access helpers
 * ============================================================ */

/* TCSR bits */
#define TCSR_IEDG  0x01  /* Input Edge */
#define TCSR_ETOI  0x02  /* Enable Timer Overflow Interrupt */
#define TCSR_EOCI  0x04  /* Enable Output Compare Interrupt */
#define TCSR_OLVL  0x08  /* Output Level */
#define TCSR_TOF   0x20  /* Timer Overflow Flag */
#define TCSR_OCF   0x40  /* Output Compare Flag */
#define TCSR_ICF   0x80  /* Input Capture Flag */

static uint8_t mem_read(m6803_state *s, uint16_t addr) {
    /* Internal I/O registers 0x0000-0x001F */
    if (addr <= 0x001F) {
        switch (addr) {
            case 0x00: return s->port1_ddr;
            case 0x01: return s->port2_ddr;
            case 0x02: /* Port 1 data read */
                if (m6803_port_read_fn) return m6803_port_read_fn(1);
                return s->port1_data;
            case 0x03: /* Port 2 data read */
                if (m6803_port_read_fn) return m6803_port_read_fn(2);
                return s->port2_data;
            case 0x08: /* TCSR read — reading starts OCF/TOF clear sequence */
                s->tcsr_read = 1;
                return s->tcsr;
            case 0x09: /* Timer counter high — latches low byte */
                s->timer_latch_h = s->timer_counter >> 8;
                /* Clear OCF/TOF if TCSR was read first (read TCSR then read counter) */
                if (s->tcsr_read) {
                    s->tcsr &= ~(TCSR_OCF | TCSR_TOF);
                    s->tcsr_read = 0;
                }
                return s->timer_latch_h;
            case 0x0A: /* Timer counter low (returns latched value) */
                return s->timer_counter & 0xFF;
            case 0x0B: return (s->timer_output_compare >> 8);
            case 0x0C: return (s->timer_output_compare & 0xFF);
            default: return 0;
        }
    }
    /* Internal RAM 0x0080-0x00FF */
    if (addr >= 0x0080 && addr <= 0x00FF)
        return m6803_internal_ram[addr - 0x0080];

    /* External memory */
    if (m6803_ext_read_fn)
        return m6803_ext_read_fn(addr);
    return 0xFF;
}

static void mem_write(m6803_state *s, uint16_t addr, uint8_t val) {
    /* Internal I/O registers */
    if (addr <= 0x001F) {
        switch (addr) {
            case 0x00: s->port1_ddr = val; break;
            case 0x01: s->port2_ddr = val; break;
            case 0x02:
                s->port1_data = val;
                if (m6803_port_write_fn) m6803_port_write_fn(1, val);
                break;
            case 0x03:
                s->port2_data = val;
                if (m6803_port_write_fn) m6803_port_write_fn(2, val);
                break;
            case 0x08: /* TCSR write — only lower 4 bits writable */
                s->tcsr = (s->tcsr & 0xF0) | (val & 0x0F);
                break;
            case 0x09: s->timer_counter = (val << 8) | (s->timer_counter & 0xFF); break;
            case 0x0A: s->timer_counter = (s->timer_counter & 0xFF00) | val; break;
            case 0x0B: s->timer_output_compare = (val << 8) | (s->timer_output_compare & 0xFF);
                s->tcsr &= ~TCSR_OCF; /* Writing OCR clears OCF */
                break;
            case 0x0C: s->timer_output_compare = (s->timer_output_compare & 0xFF00) | val; break;
        }
        return;
    }
    /* Internal RAM */
    if (addr >= 0x0080 && addr <= 0x00FF) {
        m6803_internal_ram[addr - 0x0080] = val;
        return;
    }
    /* External memory */
    if (m6803_ext_write_fn)
        m6803_ext_write_fn(addr, val);
}

static uint8_t fetch8(m6803_state *s) {
    return mem_read(s, s->PC++);
}

static uint16_t fetch16(m6803_state *s) {
    uint8_t hi = mem_read(s, s->PC++);
    uint8_t lo = mem_read(s, s->PC++);
    return ((uint16_t)hi << 8) | lo;
}

static uint16_t read16(m6803_state *s, uint16_t addr) {
    uint8_t hi = mem_read(s, addr);
    uint8_t lo = mem_read(s, (uint16_t)(addr + 1));
    return ((uint16_t)hi << 8) | lo;
}

static void write16(m6803_state *s, uint16_t addr, uint16_t val) {
    mem_write(s, addr, (uint8_t)(val >> 8));
    mem_write(s, (uint16_t)(addr + 1), (uint8_t)val);
}

/* ============================================================
 * Stack helpers
 * ============================================================ */

static void push8(m6803_state *s, uint8_t val) {
    mem_write(s, s->SP--, val);
}
static uint8_t pull8(m6803_state *s) {
    return mem_read(s, ++s->SP);
}
static void push16(m6803_state *s, uint16_t val) {
    push8(s, (uint8_t)val);
    push8(s, (uint8_t)(val >> 8));
}
static uint16_t pull16(m6803_state *s) {
    uint16_t hi = pull8(s);
    uint16_t lo = pull8(s);
    return (hi << 8) | lo;
}

/* ============================================================
 * Flag helpers
 * ============================================================ */

#define SET_Z8(v)  if(!(v)) s->CC |= M6803_CC_Z; else s->CC &= ~M6803_CC_Z
#define SET_N8(v)  if((v)&0x80) s->CC |= M6803_CC_N; else s->CC &= ~M6803_CC_N
#define SET_Z16(v) if(!(v)) s->CC |= M6803_CC_Z; else s->CC &= ~M6803_CC_Z
#define SET_N16(v) if((v)&0x8000) s->CC |= M6803_CC_N; else s->CC &= ~M6803_CC_N

static void set_nz8(m6803_state *s, uint8_t val) {
    s->CC &= ~(M6803_CC_N | M6803_CC_Z);
    if (val == 0) s->CC |= M6803_CC_Z;
    if (val & 0x80) s->CC |= M6803_CC_N;
}

static void set_nz16(m6803_state *s, uint16_t val) {
    s->CC &= ~(M6803_CC_N | M6803_CC_Z);
    if (val == 0) s->CC |= M6803_CC_Z;
    if (val & 0x8000) s->CC |= M6803_CC_N;
}

/* ============================================================
 * Addressing mode helpers
 * ============================================================ */

/* Direct page: 1-byte address (high byte = 0x00) */
static uint16_t addr_direct(m6803_state *s) {
    return fetch8(s);
}

/* Indexed: 1-byte offset + X register */
static uint16_t addr_indexed(m6803_state *s) {
    return (uint16_t)(fetch8(s) + s->X);
}

/* Extended: 2-byte absolute address */
static uint16_t addr_extended(m6803_state *s) {
    return fetch16(s);
}

/* ============================================================
 * ALU operations
 * ============================================================ */

static uint8_t op_add(m6803_state *s, uint8_t a, uint8_t b, uint8_t carry) {
    uint16_t r = a + b + carry;
    s->CC &= ~(M6803_CC_H | M6803_CC_N | M6803_CC_Z | M6803_CC_V | M6803_CC_C);
    if ((a ^ b ^ r) & 0x10) s->CC |= M6803_CC_H;
    if (r & 0x80) s->CC |= M6803_CC_N;
    if ((r & 0xFF) == 0) s->CC |= M6803_CC_Z;
    if ((a ^ r) & (b ^ r) & 0x80) s->CC |= M6803_CC_V;
    if (r & 0x100) s->CC |= M6803_CC_C;
    return (uint8_t)r;
}

static uint8_t op_sub(m6803_state *s, uint8_t a, uint8_t b, uint8_t carry) {
    uint16_t r = a - b - carry;
    s->CC &= ~(M6803_CC_N | M6803_CC_Z | M6803_CC_V | M6803_CC_C);
    if (r & 0x80) s->CC |= M6803_CC_N;
    if ((r & 0xFF) == 0) s->CC |= M6803_CC_Z;
    if ((a ^ b) & (a ^ r) & 0x80) s->CC |= M6803_CC_V;
    if (r & 0x100) s->CC |= M6803_CC_C;
    return (uint8_t)r;
}

static void op_cmp(m6803_state *s, uint8_t a, uint8_t b) {
    op_sub(s, a, b, 0);
}

static void op_cmp16(m6803_state *s, uint16_t a, uint16_t b) {
    uint32_t r = a - b;
    s->CC &= ~(M6803_CC_N | M6803_CC_Z | M6803_CC_V | M6803_CC_C);
    if (r & 0x8000) s->CC |= M6803_CC_N;
    if ((r & 0xFFFF) == 0) s->CC |= M6803_CC_Z;
    if ((a ^ b) & (a ^ r) & 0x8000) s->CC |= M6803_CC_V;
    if (r & 0x10000) s->CC |= M6803_CC_C;
}

static uint8_t op_and(m6803_state *s, uint8_t a, uint8_t b) {
    uint8_t r = a & b;
    s->CC &= ~M6803_CC_V;
    set_nz8(s, r);
    return r;
}

static uint8_t op_or(m6803_state *s, uint8_t a, uint8_t b) {
    uint8_t r = a | b;
    s->CC &= ~M6803_CC_V;
    set_nz8(s, r);
    return r;
}

static uint8_t op_eor(m6803_state *s, uint8_t a, uint8_t b) {
    uint8_t r = a ^ b;
    s->CC &= ~M6803_CC_V;
    set_nz8(s, r);
    return r;
}

static void op_bit(m6803_state *s, uint8_t a, uint8_t b) {
    uint8_t r = a & b;
    s->CC &= ~M6803_CC_V;
    set_nz8(s, r);
}

static uint8_t op_neg(m6803_state *s, uint8_t val) {
    uint8_t r = (uint8_t)(0 - val);
    s->CC &= ~(M6803_CC_N | M6803_CC_Z | M6803_CC_V | M6803_CC_C);
    if (r & 0x80) s->CC |= M6803_CC_N;
    if (r == 0) s->CC |= M6803_CC_Z;
    if (val == 0x80) s->CC |= M6803_CC_V;
    if (r != 0) s->CC |= M6803_CC_C;
    return r;
}

static uint8_t op_com(m6803_state *s, uint8_t val) {
    uint8_t r = ~val;
    s->CC &= ~(M6803_CC_V);
    s->CC |= M6803_CC_C;
    set_nz8(s, r);
    return r;
}

static uint8_t op_lsr(m6803_state *s, uint8_t val) {
    s->CC &= ~(M6803_CC_N | M6803_CC_Z | M6803_CC_C);
    if (val & 1) s->CC |= M6803_CC_C;
    uint8_t r = val >> 1;
    if (r == 0) s->CC |= M6803_CC_Z;
    return r;
}

static uint8_t op_ror(m6803_state *s, uint8_t val) {
    uint8_t c = (s->CC & M6803_CC_C) ? 0x80 : 0;
    s->CC &= ~(M6803_CC_N | M6803_CC_Z | M6803_CC_C);
    if (val & 1) s->CC |= M6803_CC_C;
    uint8_t r = (val >> 1) | c;
    set_nz8(s, r);
    return r;
}

static uint8_t op_asr(m6803_state *s, uint8_t val) {
    s->CC &= ~(M6803_CC_N | M6803_CC_Z | M6803_CC_C);
    if (val & 1) s->CC |= M6803_CC_C;
    uint8_t r = (val >> 1) | (val & 0x80);
    set_nz8(s, r);
    return r;
}

static uint8_t op_asl(m6803_state *s, uint8_t val) {
    s->CC &= ~(M6803_CC_N | M6803_CC_Z | M6803_CC_V | M6803_CC_C);
    if (val & 0x80) s->CC |= M6803_CC_C;
    uint8_t r = val << 1;
    set_nz8(s, r);
    if ((val ^ r) & 0x80) s->CC |= M6803_CC_V;
    return r;
}

static uint8_t op_rol(m6803_state *s, uint8_t val) {
    uint8_t c = (s->CC & M6803_CC_C) ? 1 : 0;
    s->CC &= ~(M6803_CC_N | M6803_CC_Z | M6803_CC_V | M6803_CC_C);
    if (val & 0x80) s->CC |= M6803_CC_C;
    uint8_t r = (val << 1) | c;
    set_nz8(s, r);
    if ((val ^ r) & 0x80) s->CC |= M6803_CC_V;
    return r;
}

static uint8_t op_dec(m6803_state *s, uint8_t val) {
    uint8_t r = val - 1;
    s->CC &= ~(M6803_CC_N | M6803_CC_Z | M6803_CC_V);
    if (val == 0x80) s->CC |= M6803_CC_V;
    set_nz8(s, r);
    return r;
}

static uint8_t op_inc(m6803_state *s, uint8_t val) {
    uint8_t r = val + 1;
    s->CC &= ~(M6803_CC_N | M6803_CC_Z | M6803_CC_V);
    if (val == 0x7F) s->CC |= M6803_CC_V;
    set_nz8(s, r);
    return r;
}

static void op_tst(m6803_state *s, uint8_t val) {
    s->CC &= ~(M6803_CC_N | M6803_CC_Z | M6803_CC_V | M6803_CC_C);
    set_nz8(s, val);
}

static uint8_t op_clr(m6803_state *s) {
    s->CC &= ~(M6803_CC_N | M6803_CC_V | M6803_CC_C);
    s->CC |= M6803_CC_Z;
    return 0;
}

/* DAA - Decimal Adjust Accumulator */
static void op_daa(m6803_state *s) {
    uint16_t t = s->A;
    if ((s->CC & M6803_CC_H) || (t & 0x0F) > 9) t += 6;
    if ((s->CC & M6803_CC_C) || t > 0x9F) { t += 0x60; s->CC |= M6803_CC_C; }
    s->A = (uint8_t)t;
    set_nz8(s, s->A);
}

/* ============================================================
 * Reset
 * ============================================================ */

void m6803_reset(m6803_state *s) {
    s->A = 0; s->B = 0;
    s->X = 0;
    s->SP = 0x00FF;
    s->CC = M6803_CC_I; /* IRQ masked at reset */
    s->irq_pending = 0;
    s->wai_state = 0;
    s->port1_data = 0; s->port2_data = 0;
    s->port1_ddr = 0; s->port2_ddr = 0;
    s->timer_counter = 0;
    s->timer_output_compare = 0xFFFF;
    s->tcsr = 0;
    s->tcsr_read = 0;
    s->timer_latch_h = 0;
    s->cycles = 0;

    /* Clear internal RAM */
    for (int i = 0; i < 128; i++) m6803_internal_ram[i] = 0;

    /* Read reset vector */
    s->PC = read16(s, 0xFFFE);
}

/* ============================================================
 * IRQ / NMI
 * ============================================================ */

void m6803_irq(m6803_state *s) {
    s->irq_pending = 1;
    if (s->wai_state) {
        s->wai_state = 0;
        if (!(s->CC & M6803_CC_I)) {
            s->CC |= M6803_CC_I;
            s->PC = read16(s, 0xFFF8);
            s->irq_pending = 0;
        }
    }
}

void m6803_nmi(m6803_state *s) {
    push16(s, s->PC);
    push16(s, s->X);
    push8(s, s->A);
    push8(s, s->B);
    push8(s, s->CC);
    s->CC |= M6803_CC_I;
    s->PC = read16(s, 0xFFFC);
    s->wai_state = 0;
}

/* ============================================================
 * Execute one instruction
 * ============================================================ */

int m6803_step(m6803_state *s) {
    /* Check for pending IRQ */
    if (s->irq_pending && !(s->CC & M6803_CC_I)) {
        push16(s, s->PC);
        push16(s, s->X);
        push8(s, s->A);
        push8(s, s->B);
        push8(s, s->CC);
        s->CC |= M6803_CC_I;
        s->PC = read16(s, 0xFFF8);
        s->irq_pending = 0;
        s->wai_state = 0;
        return 12;
    }

    if (s->wai_state) {
        /* Timer interrupt can wake from WAI (timer updated externally) */
        if ((s->tcsr & TCSR_OCF) && (s->tcsr & TCSR_EOCI) && !(s->CC & M6803_CC_I)) {
            s->wai_state = 0;
            s->CC |= M6803_CC_I;
            s->PC = read16(s, 0xFFF4); /* OCI vector */
            return 12;
        }
        return 4; /* WAI consumes cycles (timer advanced externally) */
    }

    /* Timer is incremented externally after m6803_step returns cycle count */

    /* Timer OCI interrupt */
    if ((s->tcsr & TCSR_OCF) && (s->tcsr & TCSR_EOCI) && !(s->CC & M6803_CC_I)) {
        push16(s, s->PC);
        push16(s, s->X);
        push8(s, s->A);
        push8(s, s->B);
        push8(s, s->CC);
        s->CC |= M6803_CC_I;
        s->PC = read16(s, 0xFFF4); /* OCI vector */
        return 12;
    }

    uint8_t op = fetch8(s);
    uint16_t ea;
    uint8_t tmp8;
    uint16_t tmp16;
    uint32_t tmp32;

    switch (op) {
    /* ── Inherent ── */
    case 0x01: /* NOP */ return 2;
    case 0x06: /* TAP */ s->CC = s->A | 0xC0; return 2;
    case 0x07: /* TPA */ s->A = s->CC; return 2;
    case 0x08: /* INX */ s->X++; s->CC &= ~M6803_CC_Z; if(s->X==0) s->CC|=M6803_CC_Z; return 4;
    case 0x09: /* DEX */ s->X--; s->CC &= ~M6803_CC_Z; if(s->X==0) s->CC|=M6803_CC_Z; return 4;
    case 0x0A: /* CLV */ s->CC &= ~M6803_CC_V; return 2;
    case 0x0B: /* SEV */ s->CC |= M6803_CC_V; return 2;
    case 0x0C: /* CLC */ s->CC &= ~M6803_CC_C; return 2;
    case 0x0D: /* SEC */ s->CC |= M6803_CC_C; return 2;
    case 0x0E: /* CLI */ s->CC &= ~M6803_CC_I; return 2;
    case 0x0F: /* SEI */ s->CC |= M6803_CC_I; return 2;

    case 0x10: /* SBA */ s->A = op_sub(s, s->A, s->B, 0); return 2;
    case 0x11: /* CBA */ op_cmp(s, s->A, s->B); return 2;
    case 0x16: /* TAB */ s->B = s->A; s->CC &= ~M6803_CC_V; set_nz8(s, s->B); return 2;
    case 0x17: /* TBA */ s->A = s->B; s->CC &= ~M6803_CC_V; set_nz8(s, s->A); return 2;
    case 0x19: /* DAA */ op_daa(s); return 2;
    case 0x1B: /* ABA */ s->A = op_add(s, s->A, s->B, 0); return 2;

    case 0x20: /* BRA */ tmp8 = fetch8(s); s->PC += (int8_t)tmp8; return 4;
    case 0x21: /* BRN */ fetch8(s); return 4;
    case 0x22: /* BHI */ tmp8 = fetch8(s); if(!(s->CC&(M6803_CC_C|M6803_CC_Z))) s->PC+=(int8_t)tmp8; return 4;
    case 0x23: /* BLS */ tmp8 = fetch8(s); if(s->CC&(M6803_CC_C|M6803_CC_Z)) s->PC+=(int8_t)tmp8; return 4;
    case 0x24: /* BCC/BHS */ tmp8 = fetch8(s); if(!(s->CC&M6803_CC_C)) s->PC+=(int8_t)tmp8; return 4;
    case 0x25: /* BCS/BLO */ tmp8 = fetch8(s); if(s->CC&M6803_CC_C) s->PC+=(int8_t)tmp8; return 4;
    case 0x26: /* BNE */ tmp8 = fetch8(s); if(!(s->CC&M6803_CC_Z)) s->PC+=(int8_t)tmp8; return 4;
    case 0x27: /* BEQ */ tmp8 = fetch8(s); if(s->CC&M6803_CC_Z) s->PC+=(int8_t)tmp8; return 4;
    case 0x28: /* BVC */ tmp8 = fetch8(s); if(!(s->CC&M6803_CC_V)) s->PC+=(int8_t)tmp8; return 4;
    case 0x29: /* BVS */ tmp8 = fetch8(s); if(s->CC&M6803_CC_V) s->PC+=(int8_t)tmp8; return 4;
    case 0x2A: /* BPL */ tmp8 = fetch8(s); if(!(s->CC&M6803_CC_N)) s->PC+=(int8_t)tmp8; return 4;
    case 0x2B: /* BMI */ tmp8 = fetch8(s); if(s->CC&M6803_CC_N) s->PC+=(int8_t)tmp8; return 4;
    case 0x2C: /* BGE */ tmp8 = fetch8(s); if(!((s->CC&M6803_CC_N)!=0)^((s->CC&M6803_CC_V)!=0)) s->PC+=(int8_t)tmp8; return 4;
    case 0x2D: /* BLT */ tmp8 = fetch8(s); if(((s->CC&M6803_CC_N)!=0)^((s->CC&M6803_CC_V)!=0)) s->PC+=(int8_t)tmp8; return 4;
    case 0x2E: /* BGT */ tmp8 = fetch8(s); if(!(s->CC&M6803_CC_Z)&&!((s->CC&M6803_CC_N)!=0)^((s->CC&M6803_CC_V)!=0)) s->PC+=(int8_t)tmp8; return 4;
    case 0x2F: /* BLE */ tmp8 = fetch8(s); if((s->CC&M6803_CC_Z)||((s->CC&M6803_CC_N)!=0)^((s->CC&M6803_CC_V)!=0)) s->PC+=(int8_t)tmp8; return 4;

    case 0x30: /* TSX */ s->X = s->SP + 1; return 4;
    case 0x31: /* INS */ s->SP++; return 4;
    case 0x32: /* PULA */ s->A = pull8(s); return 4;
    case 0x33: /* PULB */ s->B = pull8(s); return 4;
    case 0x34: /* DES */ s->SP--; return 4;
    case 0x35: /* TXS */ s->SP = s->X - 1; return 4;
    case 0x36: /* PSHA */ push8(s, s->A); return 4;
    case 0x37: /* PSHB */ push8(s, s->B); return 4;
    case 0x38: /* PULX (M6803) */ s->X = pull16(s); return 5;
    case 0x39: /* RTS */ s->PC = pull16(s); return 5;
    case 0x3A: /* ABX (M6803) */ s->X += s->B; return 3;
    case 0x3B: /* RTI */ s->CC = pull8(s); s->B = pull8(s); s->A = pull8(s); s->X = pull16(s); s->PC = pull16(s); return 10;
    case 0x3C: /* PSHX (M6803) */ push16(s, s->X); return 5;
    case 0x3D: /* MUL (M6803) */ tmp16 = (uint16_t)s->A * (uint16_t)s->B; s->A = tmp16>>8; s->B = tmp16&0xFF; s->CC &= ~M6803_CC_C; if(s->B&0x80) s->CC|=M6803_CC_C; return 10;
    case 0x3E: /* WAI */ s->wai_state = 1; push16(s, s->PC); push16(s, s->X); push8(s, s->A); push8(s, s->B); push8(s, s->CC); return 9;
    case 0x3F: /* SWI */ push16(s, s->PC); push16(s, s->X); push8(s, s->A); push8(s, s->B); push8(s, s->CC); s->CC |= M6803_CC_I; s->PC = read16(s, 0xFFFA); return 12;

    /* ── Accumulator A operations ── */
    case 0x40: /* NEGA */ s->A = op_neg(s, s->A); return 2;
    case 0x43: /* COMA */ s->A = op_com(s, s->A); return 2;
    case 0x44: /* LSRA */ s->A = op_lsr(s, s->A); return 2;
    case 0x46: /* RORA */ s->A = op_ror(s, s->A); return 2;
    case 0x47: /* ASRA */ s->A = op_asr(s, s->A); return 2;
    case 0x48: /* ASLA/LSLA */ s->A = op_asl(s, s->A); return 2;
    case 0x49: /* ROLA */ s->A = op_rol(s, s->A); return 2;
    case 0x4A: /* DECA */ s->A = op_dec(s, s->A); return 2;
    case 0x4C: /* INCA */ s->A = op_inc(s, s->A); return 2;
    case 0x4D: /* TSTA */ op_tst(s, s->A); return 2;
    case 0x4F: /* CLRA */ s->A = op_clr(s); return 2;

    /* ── Accumulator B operations ── */
    case 0x50: /* NEGB */ s->B = op_neg(s, s->B); return 2;
    case 0x53: /* COMB */ s->B = op_com(s, s->B); return 2;
    case 0x54: /* LSRB */ s->B = op_lsr(s, s->B); return 2;
    case 0x56: /* RORB */ s->B = op_ror(s, s->B); return 2;
    case 0x57: /* ASRB */ s->B = op_asr(s, s->B); return 2;
    case 0x58: /* ASLB/LSLB */ s->B = op_asl(s, s->B); return 2;
    case 0x59: /* ROLB */ s->B = op_rol(s, s->B); return 2;
    case 0x5A: /* DECB */ s->B = op_dec(s, s->B); return 2;
    case 0x5C: /* INCB */ s->B = op_inc(s, s->B); return 2;
    case 0x5D: /* TSTB */ op_tst(s, s->B); return 2;
    case 0x5F: /* CLRB */ s->B = op_clr(s); return 2;

    /* ── Indexed mode: memory operations ── */
    case 0x60: /* NEG idx */ ea = addr_indexed(s); mem_write(s, ea, op_neg(s, mem_read(s, ea))); return 7;
    case 0x63: /* COM idx */ ea = addr_indexed(s); mem_write(s, ea, op_com(s, mem_read(s, ea))); return 7;
    case 0x64: /* LSR idx */ ea = addr_indexed(s); mem_write(s, ea, op_lsr(s, mem_read(s, ea))); return 7;
    case 0x66: /* ROR idx */ ea = addr_indexed(s); mem_write(s, ea, op_ror(s, mem_read(s, ea))); return 7;
    case 0x67: /* ASR idx */ ea = addr_indexed(s); mem_write(s, ea, op_asr(s, mem_read(s, ea))); return 7;
    case 0x68: /* ASL idx */ ea = addr_indexed(s); mem_write(s, ea, op_asl(s, mem_read(s, ea))); return 7;
    case 0x69: /* ROL idx */ ea = addr_indexed(s); mem_write(s, ea, op_rol(s, mem_read(s, ea))); return 7;
    case 0x6A: /* DEC idx */ ea = addr_indexed(s); mem_write(s, ea, op_dec(s, mem_read(s, ea))); return 7;
    case 0x6C: /* INC idx */ ea = addr_indexed(s); mem_write(s, ea, op_inc(s, mem_read(s, ea))); return 7;
    case 0x6D: /* TST idx */ ea = addr_indexed(s); op_tst(s, mem_read(s, ea)); return 7;
    case 0x6E: /* JMP idx */ s->PC = addr_indexed(s); return 4;
    case 0x6F: /* CLR idx */ ea = addr_indexed(s); mem_write(s, ea, op_clr(s)); return 7;

    /* ── Extended mode: memory operations ── */
    case 0x70: /* NEG ext */ ea = addr_extended(s); mem_write(s, ea, op_neg(s, mem_read(s, ea))); return 6;
    case 0x73: /* COM ext */ ea = addr_extended(s); mem_write(s, ea, op_com(s, mem_read(s, ea))); return 6;
    case 0x74: /* LSR ext */ ea = addr_extended(s); mem_write(s, ea, op_lsr(s, mem_read(s, ea))); return 6;
    case 0x76: /* ROR ext */ ea = addr_extended(s); mem_write(s, ea, op_ror(s, mem_read(s, ea))); return 6;
    case 0x77: /* ASR ext */ ea = addr_extended(s); mem_write(s, ea, op_asr(s, mem_read(s, ea))); return 6;
    case 0x78: /* ASL ext */ ea = addr_extended(s); mem_write(s, ea, op_asl(s, mem_read(s, ea))); return 6;
    case 0x79: /* ROL ext */ ea = addr_extended(s); mem_write(s, ea, op_rol(s, mem_read(s, ea))); return 6;
    case 0x7A: /* DEC ext */ ea = addr_extended(s); mem_write(s, ea, op_dec(s, mem_read(s, ea))); return 6;
    case 0x7C: /* INC ext */ ea = addr_extended(s); mem_write(s, ea, op_inc(s, mem_read(s, ea))); return 6;
    case 0x7D: /* TST ext */ ea = addr_extended(s); op_tst(s, mem_read(s, ea)); return 6;
    case 0x7E: /* JMP ext */ s->PC = addr_extended(s); return 3;
    case 0x7F: /* CLR ext */ ea = addr_extended(s); mem_write(s, ea, op_clr(s)); return 6;

    /* ── A: Immediate ── */
    case 0x80: /* SUBA imm */ s->A = op_sub(s, s->A, fetch8(s), 0); return 2;
    case 0x81: /* CMPA imm */ op_cmp(s, s->A, fetch8(s)); return 2;
    case 0x82: /* SBCA imm */ s->A = op_sub(s, s->A, fetch8(s), s->CC & M6803_CC_C); return 2;
    case 0x83: /* SUBD imm (M6803) */ tmp16 = fetch16(s); tmp32 = ((uint16_t)(s->A<<8|s->B)) - tmp16; op_cmp16(s, (s->A<<8)|s->B, tmp16); s->A=(tmp32>>8)&0xFF; s->B=tmp32&0xFF; return 4;
    case 0x84: /* ANDA imm */ s->A = op_and(s, s->A, fetch8(s)); return 2;
    case 0x85: /* BITA imm */ op_bit(s, s->A, fetch8(s)); return 2;
    case 0x86: /* LDAA imm */ s->A = fetch8(s); s->CC &= ~M6803_CC_V; set_nz8(s, s->A); return 2;
    case 0x88: /* EORA imm */ s->A = op_eor(s, s->A, fetch8(s)); return 2;
    case 0x89: /* ADCA imm */ s->A = op_add(s, s->A, fetch8(s), s->CC & M6803_CC_C); return 2;
    case 0x8A: /* ORAA imm */ s->A = op_or(s, s->A, fetch8(s)); return 2;
    case 0x8B: /* ADDA imm */ s->A = op_add(s, s->A, fetch8(s), 0); return 2;
    case 0x8C: /* CPX imm (M6803) */ tmp16 = fetch16(s); op_cmp16(s, s->X, tmp16); return 4;
    case 0x8D: /* BSR */ tmp8 = fetch8(s); push16(s, s->PC); s->PC += (int8_t)tmp8; return 6;
    case 0x8E: /* LDS imm */ s->SP = fetch16(s); s->CC &= ~M6803_CC_V; set_nz16(s, s->SP); return 3;

    /* ── A: Direct ── */
    case 0x90: /* SUBA dir */ s->A = op_sub(s, s->A, mem_read(s, addr_direct(s)), 0); return 3;
    case 0x91: /* CMPA dir */ op_cmp(s, s->A, mem_read(s, addr_direct(s))); return 3;
    case 0x92: /* SBCA dir */ s->A = op_sub(s, s->A, mem_read(s, addr_direct(s)), s->CC & M6803_CC_C); return 3;
    case 0x93: /* SUBD dir */ ea = addr_direct(s); tmp16 = read16(s, ea); tmp32 = ((uint16_t)(s->A<<8|s->B)) - tmp16; op_cmp16(s, (s->A<<8)|s->B, tmp16); s->A=(tmp32>>8)&0xFF; s->B=tmp32&0xFF; return 5;
    case 0x94: /* ANDA dir */ s->A = op_and(s, s->A, mem_read(s, addr_direct(s))); return 3;
    case 0x95: /* BITA dir */ op_bit(s, s->A, mem_read(s, addr_direct(s))); return 3;
    case 0x96: /* LDAA dir */ ea = addr_direct(s); s->A = mem_read(s, ea); s->CC &= ~M6803_CC_V; set_nz8(s, s->A); return 3;
    case 0x97: /* STAA dir */ ea = addr_direct(s); mem_write(s, ea, s->A); s->CC &= ~M6803_CC_V; set_nz8(s, s->A); return 3;
    case 0x98: /* EORA dir */ s->A = op_eor(s, s->A, mem_read(s, addr_direct(s))); return 3;
    case 0x99: /* ADCA dir */ s->A = op_add(s, s->A, mem_read(s, addr_direct(s)), s->CC & M6803_CC_C); return 3;
    case 0x9A: /* ORAA dir */ s->A = op_or(s, s->A, mem_read(s, addr_direct(s))); return 3;
    case 0x9B: /* ADDA dir */ s->A = op_add(s, s->A, mem_read(s, addr_direct(s)), 0); return 3;
    case 0x9C: /* CPX dir */ ea = addr_direct(s); op_cmp16(s, s->X, read16(s, ea)); return 5;
    case 0x9D: /* JSR dir */ ea = addr_direct(s); push16(s, s->PC); s->PC = ea; return 5;
    case 0x9E: /* LDS dir */ ea = addr_direct(s); s->SP = read16(s, ea); s->CC &= ~M6803_CC_V; set_nz16(s, s->SP); return 4;
    case 0x9F: /* STS dir */ ea = addr_direct(s); write16(s, ea, s->SP); s->CC &= ~M6803_CC_V; set_nz16(s, s->SP); return 4;

    /* ── A: Indexed ── */
    case 0xA0: /* SUBA idx */ s->A = op_sub(s, s->A, mem_read(s, addr_indexed(s)), 0); return 5;
    case 0xA1: /* CMPA idx */ op_cmp(s, s->A, mem_read(s, addr_indexed(s))); return 5;
    case 0xA2: /* SBCA idx */ s->A = op_sub(s, s->A, mem_read(s, addr_indexed(s)), s->CC & M6803_CC_C); return 5;
    case 0xA3: /* SUBD idx */ ea = addr_indexed(s); tmp16 = read16(s, ea); tmp32 = ((uint16_t)(s->A<<8|s->B)) - tmp16; op_cmp16(s, (s->A<<8)|s->B, tmp16); s->A=(tmp32>>8)&0xFF; s->B=tmp32&0xFF; return 7;
    case 0xA4: /* ANDA idx */ s->A = op_and(s, s->A, mem_read(s, addr_indexed(s))); return 5;
    case 0xA5: /* BITA idx */ op_bit(s, s->A, mem_read(s, addr_indexed(s))); return 5;
    case 0xA6: /* LDAA idx */ ea = addr_indexed(s); s->A = mem_read(s, ea); s->CC &= ~M6803_CC_V; set_nz8(s, s->A); return 5;
    case 0xA7: /* STAA idx */ ea = addr_indexed(s); mem_write(s, ea, s->A); s->CC &= ~M6803_CC_V; set_nz8(s, s->A); return 5;
    case 0xA8: /* EORA idx */ s->A = op_eor(s, s->A, mem_read(s, addr_indexed(s))); return 5;
    case 0xA9: /* ADCA idx */ s->A = op_add(s, s->A, mem_read(s, addr_indexed(s)), s->CC & M6803_CC_C); return 5;
    case 0xAA: /* ORAA idx */ s->A = op_or(s, s->A, mem_read(s, addr_indexed(s))); return 5;
    case 0xAB: /* ADDA idx */ s->A = op_add(s, s->A, mem_read(s, addr_indexed(s)), 0); return 5;
    case 0xAC: /* CPX idx */ ea = addr_indexed(s); op_cmp16(s, s->X, read16(s, ea)); return 7;
    case 0xAD: /* JSR idx */ ea = addr_indexed(s); push16(s, s->PC); s->PC = ea; return 8;
    case 0xAE: /* LDS idx */ ea = addr_indexed(s); s->SP = read16(s, ea); s->CC &= ~M6803_CC_V; set_nz16(s, s->SP); return 6;
    case 0xAF: /* STS idx */ ea = addr_indexed(s); write16(s, ea, s->SP); s->CC &= ~M6803_CC_V; set_nz16(s, s->SP); return 6;

    /* ── A: Extended ── */
    case 0xB0: /* SUBA ext */ s->A = op_sub(s, s->A, mem_read(s, addr_extended(s)), 0); return 4;
    case 0xB1: /* CMPA ext */ op_cmp(s, s->A, mem_read(s, addr_extended(s))); return 4;
    case 0xB2: /* SBCA ext */ s->A = op_sub(s, s->A, mem_read(s, addr_extended(s)), s->CC & M6803_CC_C); return 4;
    case 0xB3: /* SUBD ext */ ea = addr_extended(s); tmp16 = read16(s, ea); tmp32 = ((uint16_t)(s->A<<8|s->B)) - tmp16; op_cmp16(s, (s->A<<8)|s->B, tmp16); s->A=(tmp32>>8)&0xFF; s->B=tmp32&0xFF; return 6;
    case 0xB4: /* ANDA ext */ s->A = op_and(s, s->A, mem_read(s, addr_extended(s))); return 4;
    case 0xB5: /* BITA ext */ op_bit(s, s->A, mem_read(s, addr_extended(s))); return 4;
    case 0xB6: /* LDAA ext */ ea = addr_extended(s); s->A = mem_read(s, ea); s->CC &= ~M6803_CC_V; set_nz8(s, s->A); return 4;
    case 0xB7: /* STAA ext */ ea = addr_extended(s); mem_write(s, ea, s->A); s->CC &= ~M6803_CC_V; set_nz8(s, s->A); return 4;
    case 0xB8: /* EORA ext */ s->A = op_eor(s, s->A, mem_read(s, addr_extended(s))); return 4;
    case 0xB9: /* ADCA ext */ s->A = op_add(s, s->A, mem_read(s, addr_extended(s)), s->CC & M6803_CC_C); return 4;
    case 0xBA: /* ORAA ext */ s->A = op_or(s, s->A, mem_read(s, addr_extended(s))); return 4;
    case 0xBB: /* ADDA ext */ s->A = op_add(s, s->A, mem_read(s, addr_extended(s)), 0); return 4;
    case 0xBC: /* CPX ext */ ea = addr_extended(s); op_cmp16(s, s->X, read16(s, ea)); return 6;
    case 0xBD: /* JSR ext */ ea = addr_extended(s); push16(s, s->PC); s->PC = ea; return 9;
    case 0xBE: /* LDS ext */ ea = addr_extended(s); s->SP = read16(s, ea); s->CC &= ~M6803_CC_V; set_nz16(s, s->SP); return 5;
    case 0xBF: /* STS ext */ ea = addr_extended(s); write16(s, ea, s->SP); s->CC &= ~M6803_CC_V; set_nz16(s, s->SP); return 5;

    /* ── B: Immediate ── */
    case 0xC0: /* SUBB imm */ s->B = op_sub(s, s->B, fetch8(s), 0); return 2;
    case 0xC1: /* CMPB imm */ op_cmp(s, s->B, fetch8(s)); return 2;
    case 0xC2: /* SBCB imm */ s->B = op_sub(s, s->B, fetch8(s), s->CC & M6803_CC_C); return 2;
    case 0xC3: /* ADDD imm (M6803) */ tmp16 = fetch16(s); tmp32 = ((uint16_t)(s->A<<8|s->B)) + tmp16; s->CC&=~(M6803_CC_N|M6803_CC_Z|M6803_CC_V|M6803_CC_C); if(tmp32&0x8000) s->CC|=M6803_CC_N; if((tmp32&0xFFFF)==0) s->CC|=M6803_CC_Z; if(((s->A<<8|s->B)^tmp32)&(tmp16^tmp32)&0x8000) s->CC|=M6803_CC_V; if(tmp32&0x10000) s->CC|=M6803_CC_C; s->A=(tmp32>>8)&0xFF; s->B=tmp32&0xFF; return 4;
    case 0xC4: /* ANDB imm */ s->B = op_and(s, s->B, fetch8(s)); return 2;
    case 0xC5: /* BITB imm */ op_bit(s, s->B, fetch8(s)); return 2;
    case 0xC6: /* LDAB imm */ s->B = fetch8(s); s->CC &= ~M6803_CC_V; set_nz8(s, s->B); return 2;
    case 0xC8: /* EORB imm */ s->B = op_eor(s, s->B, fetch8(s)); return 2;
    case 0xC9: /* ADCB imm */ s->B = op_add(s, s->B, fetch8(s), s->CC & M6803_CC_C); return 2;
    case 0xCA: /* ORAB imm */ s->B = op_or(s, s->B, fetch8(s)); return 2;
    case 0xCB: /* ADDB imm */ s->B = op_add(s, s->B, fetch8(s), 0); return 2;
    case 0xCC: /* LDD imm (M6803) */ s->A = fetch8(s); s->B = fetch8(s); s->CC &= ~M6803_CC_V; set_nz16(s, (s->A<<8)|s->B); return 3;
    case 0xCE: /* LDX imm */ s->X = fetch16(s); s->CC &= ~M6803_CC_V; set_nz16(s, s->X); return 3;

    /* ── B: Direct ── */
    case 0xD0: /* SUBB dir */ s->B = op_sub(s, s->B, mem_read(s, addr_direct(s)), 0); return 3;
    case 0xD1: /* CMPB dir */ op_cmp(s, s->B, mem_read(s, addr_direct(s))); return 3;
    case 0xD2: /* SBCB dir */ s->B = op_sub(s, s->B, mem_read(s, addr_direct(s)), s->CC & M6803_CC_C); return 3;
    case 0xD3: /* ADDD dir */ ea = addr_direct(s); tmp16 = read16(s, ea); tmp32 = ((uint16_t)(s->A<<8|s->B)) + tmp16; s->CC&=~(M6803_CC_N|M6803_CC_Z|M6803_CC_V|M6803_CC_C); if(tmp32&0x8000) s->CC|=M6803_CC_N; if((tmp32&0xFFFF)==0) s->CC|=M6803_CC_Z; if(((s->A<<8|s->B)^tmp32)&(tmp16^tmp32)&0x8000) s->CC|=M6803_CC_V; if(tmp32&0x10000) s->CC|=M6803_CC_C; s->A=(tmp32>>8)&0xFF; s->B=tmp32&0xFF; return 5;
    case 0xD4: /* ANDB dir */ s->B = op_and(s, s->B, mem_read(s, addr_direct(s))); return 3;
    case 0xD5: /* BITB dir */ op_bit(s, s->B, mem_read(s, addr_direct(s))); return 3;
    case 0xD6: /* LDAB dir */ ea = addr_direct(s); s->B = mem_read(s, ea); s->CC &= ~M6803_CC_V; set_nz8(s, s->B); return 3;
    case 0xD7: /* STAB dir */ ea = addr_direct(s); mem_write(s, ea, s->B); s->CC &= ~M6803_CC_V; set_nz8(s, s->B); return 3;
    case 0xD8: /* EORB dir */ s->B = op_eor(s, s->B, mem_read(s, addr_direct(s))); return 3;
    case 0xD9: /* ADCB dir */ s->B = op_add(s, s->B, mem_read(s, addr_direct(s)), s->CC & M6803_CC_C); return 3;
    case 0xDA: /* ORAB dir */ s->B = op_or(s, s->B, mem_read(s, addr_direct(s))); return 3;
    case 0xDB: /* ADDB dir */ s->B = op_add(s, s->B, mem_read(s, addr_direct(s)), 0); return 3;
    case 0xDC: /* LDD dir (M6803) */ ea = addr_direct(s); s->A = mem_read(s, ea); s->B = mem_read(s, ea+1); s->CC &= ~M6803_CC_V; set_nz16(s, (s->A<<8)|s->B); return 4;
    case 0xDD: /* STD dir (M6803) */ ea = addr_direct(s); mem_write(s, ea, s->A); mem_write(s, ea+1, s->B); s->CC &= ~M6803_CC_V; set_nz16(s, (s->A<<8)|s->B); return 4;
    case 0xDE: /* LDX dir */ ea = addr_direct(s); s->X = read16(s, ea); s->CC &= ~M6803_CC_V; set_nz16(s, s->X); return 4;
    case 0xDF: /* STX dir */ ea = addr_direct(s); write16(s, ea, s->X); s->CC &= ~M6803_CC_V; set_nz16(s, s->X); return 4;

    /* ── B: Indexed ── */
    case 0xE0: /* SUBB idx */ s->B = op_sub(s, s->B, mem_read(s, addr_indexed(s)), 0); return 5;
    case 0xE1: /* CMPB idx */ op_cmp(s, s->B, mem_read(s, addr_indexed(s))); return 5;
    case 0xE2: /* SBCB idx */ s->B = op_sub(s, s->B, mem_read(s, addr_indexed(s)), s->CC & M6803_CC_C); return 5;
    case 0xE3: /* ADDD idx */ ea = addr_indexed(s); tmp16 = read16(s, ea); tmp32 = ((uint16_t)(s->A<<8|s->B)) + tmp16; s->CC&=~(M6803_CC_N|M6803_CC_Z|M6803_CC_V|M6803_CC_C); if(tmp32&0x8000) s->CC|=M6803_CC_N; if((tmp32&0xFFFF)==0) s->CC|=M6803_CC_Z; if(((s->A<<8|s->B)^tmp32)&(tmp16^tmp32)&0x8000) s->CC|=M6803_CC_V; if(tmp32&0x10000) s->CC|=M6803_CC_C; s->A=(tmp32>>8)&0xFF; s->B=tmp32&0xFF; return 7;
    case 0xE4: /* ANDB idx */ s->B = op_and(s, s->B, mem_read(s, addr_indexed(s))); return 5;
    case 0xE5: /* BITB idx */ op_bit(s, s->B, mem_read(s, addr_indexed(s))); return 5;
    case 0xE6: /* LDAB idx */ ea = addr_indexed(s); s->B = mem_read(s, ea); s->CC &= ~M6803_CC_V; set_nz8(s, s->B); return 5;
    case 0xE7: /* STAB idx */ ea = addr_indexed(s); mem_write(s, ea, s->B); s->CC &= ~M6803_CC_V; set_nz8(s, s->B); return 5;
    case 0xE8: /* EORB idx */ s->B = op_eor(s, s->B, mem_read(s, addr_indexed(s))); return 5;
    case 0xE9: /* ADCB idx */ s->B = op_add(s, s->B, mem_read(s, addr_indexed(s)), s->CC & M6803_CC_C); return 5;
    case 0xEA: /* ORAB idx */ s->B = op_or(s, s->B, mem_read(s, addr_indexed(s))); return 5;
    case 0xEB: /* ADDB idx */ s->B = op_add(s, s->B, mem_read(s, addr_indexed(s)), 0); return 5;
    case 0xEC: /* LDD idx (M6803) */ ea = addr_indexed(s); s->A = mem_read(s, ea); s->B = mem_read(s, ea+1); s->CC &= ~M6803_CC_V; set_nz16(s, (s->A<<8)|s->B); return 6;
    case 0xED: /* STD idx (M6803) */ ea = addr_indexed(s); mem_write(s, ea, s->A); mem_write(s, ea+1, s->B); s->CC &= ~M6803_CC_V; set_nz16(s, (s->A<<8)|s->B); return 6;
    case 0xEE: /* LDX idx */ ea = addr_indexed(s); s->X = read16(s, ea); s->CC &= ~M6803_CC_V; set_nz16(s, s->X); return 6;
    case 0xEF: /* STX idx */ ea = addr_indexed(s); write16(s, ea, s->X); s->CC &= ~M6803_CC_V; set_nz16(s, s->X); return 6;

    /* ── B: Extended ── */
    case 0xF0: /* SUBB ext */ s->B = op_sub(s, s->B, mem_read(s, addr_extended(s)), 0); return 4;
    case 0xF1: /* CMPB ext */ op_cmp(s, s->B, mem_read(s, addr_extended(s))); return 4;
    case 0xF2: /* SBCB ext */ s->B = op_sub(s, s->B, mem_read(s, addr_extended(s)), s->CC & M6803_CC_C); return 4;
    case 0xF3: /* ADDD ext */ ea = addr_extended(s); tmp16 = read16(s, ea); tmp32 = ((uint16_t)(s->A<<8|s->B)) + tmp16; s->CC&=~(M6803_CC_N|M6803_CC_Z|M6803_CC_V|M6803_CC_C); if(tmp32&0x8000) s->CC|=M6803_CC_N; if((tmp32&0xFFFF)==0) s->CC|=M6803_CC_Z; if(((s->A<<8|s->B)^tmp32)&(tmp16^tmp32)&0x8000) s->CC|=M6803_CC_V; if(tmp32&0x10000) s->CC|=M6803_CC_C; s->A=(tmp32>>8)&0xFF; s->B=tmp32&0xFF; return 6;
    case 0xF4: /* ANDB ext */ s->B = op_and(s, s->B, mem_read(s, addr_extended(s))); return 4;
    case 0xF5: /* BITB ext */ op_bit(s, s->B, mem_read(s, addr_extended(s))); return 4;
    case 0xF6: /* LDAB ext */ ea = addr_extended(s); s->B = mem_read(s, ea); s->CC &= ~M6803_CC_V; set_nz8(s, s->B); return 4;
    case 0xF7: /* STAB ext */ ea = addr_extended(s); mem_write(s, ea, s->B); s->CC &= ~M6803_CC_V; set_nz8(s, s->B); return 4;
    case 0xF8: /* EORB ext */ s->B = op_eor(s, s->B, mem_read(s, addr_extended(s))); return 4;
    case 0xF9: /* ADCB ext */ s->B = op_add(s, s->B, mem_read(s, addr_extended(s)), s->CC & M6803_CC_C); return 4;
    case 0xFA: /* ORAB ext */ s->B = op_or(s, s->B, mem_read(s, addr_extended(s))); return 4;
    case 0xFB: /* ADDB ext */ s->B = op_add(s, s->B, mem_read(s, addr_extended(s)), 0); return 4;
    case 0xFC: /* LDD ext (M6803) */ ea = addr_extended(s); s->A = mem_read(s, ea); s->B = mem_read(s, ea+1); s->CC &= ~M6803_CC_V; set_nz16(s, (s->A<<8)|s->B); return 5;
    case 0xFD: /* STD ext (M6803) */ ea = addr_extended(s); mem_write(s, ea, s->A); mem_write(s, ea+1, s->B); s->CC &= ~M6803_CC_V; set_nz16(s, (s->A<<8)|s->B); return 5;
    case 0xFE: /* LDX ext */ ea = addr_extended(s); s->X = read16(s, ea); s->CC &= ~M6803_CC_V; set_nz16(s, s->X); return 5;
    case 0xFF: /* STX ext */ ea = addr_extended(s); write16(s, ea, s->X); s->CC &= ~M6803_CC_V; set_nz16(s, s->X); return 5;

    default:
        /* Unknown opcode — treat as NOP */
        return 2;
    }
}
