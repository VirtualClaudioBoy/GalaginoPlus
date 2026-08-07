#ifndef GYRUSS_H
#define GYRUSS_H

#include "../machineBase.h"
#include "gyruss_rom_main.h"
#include "gyruss_rom_sub.h"
#include "gyruss_rom_audio.h"
#include "gyruss_rom_i8039.h"
#include "gyruss_tilemap.h"
#include "gyruss_spritemap.h"
#include "gyruss_palette.h"
#include "gyruss_dipswitches.h"
#include "gyruss_logo.h"

// Gyruss memory layout offsets in shared memory[] buffer
// memory[0x0000..0x03FF] = Color RAM (1KB)
// memory[0x0400..0x07FF] = Video RAM (1KB)
// memory[0x0800..0x17FF] = Main Z80 Work RAM (4KB)
// memory[0x1800..0x1FFF] = Audio Z80 RAM (1KB, at offset 0x1800 from AY perspective mapped at 0x6000)
#define GYR_CRAM_OFF   0x0000
#define GYR_VRAM_OFF   0x0400
#define GYR_WRAM_OFF   0x0800
#define GYR_ARAM_OFF   0x1800
// memory[0x2000-0x2FFF]: copia in RAM interna della ROM i8039 (4KB) — il
// fetch a 576k/s dalla flash sfrattava dalla cache l'emulazione Z80/M6809
// sul core emu (gioco rallentato); da RAM il fetch e' a ciclo singolo.
// La regione 0x2000-0x3FFF del buffer condiviso (RAMSIZE 16KB) era libera.
#define GYR_I8039ROM_OFF 0x2000

// ============================================================
// Batteria i8039 (drums) — parametri utente
// GYRUSS_ENABLE_DRUMS: 1 = emulata e mixata con i 5 AY, 0 = disattivata
//   (niente stepping ne' mix: audio identico a prima del 2026-07-11)
// GYRUSS_DRUMS_VOLUME: scala del DAC 8039 nel mix (default 3; max ~381
//   nel contratto +/-512 di valueToBuffer — alzare con prudenza)
// GYRUSS_DRUMS_SPEED: istruzioni 8039 emulate per campione a 24kHz =
//   velocita'/pitch della batteria. Chip reale: 8MHz / 15 clk per ciclo
//   macchina / ~1.4 cicli medi a istruzione ~= 380k istr/s -> ~16 per
//   campione.
//   Piu' alto = tamburi piu' veloci e acuti. Tarare a orecchio 15..18.
// ============================================================
#define GYRUSS_ENABLE_DRUMS  0
#define GYRUSS_DRUMS_VOLUME  2
#define GYRUSS_DRUMS_SPEED   10

// Ring buffer campioni drums i8039 (potenza di 2) e livello di riempimento
// target: 768 campioni @24kHz = ~32ms di anticipo del produttore
#define GYR_DRUM_RING  1024
#define GYR_DRUM_FILL  768

class gyruss : public machineBase
{
public:
    gyruss() { }
    ~gyruss() { }

    void start() override;
    void reset() override;

    signed char machineType() override { return MCH_GYRUSS; }
    signed char useVideoHalfRate() override { return 1; }
    signed char videoFlipY() override { return 0; }
    signed char videoFlipX() override { return 1; }
    
    unsigned char rdZ80(unsigned short Addr) override;
    void wrZ80(unsigned short Addr, unsigned char Value) override;
    void outZ80(unsigned short Port, unsigned char Value) override;
    unsigned char opZ80(unsigned short Addr) override;
    unsigned char inZ80(unsigned short Port) override;

    unsigned char m6809_read(m6809_state *s, uint16_t addr) override;
    void m6809_write(m6809_state *s, uint16_t addr, uint8_t val) override;
    unsigned char m6809_read_opcode(m6809_state *s, uint16_t addr) override;

    // --- i8039 sample MCU (drums/percussioni) ---
    // 4a CPU reale di Gyruss: lo Z80 audio scrive il comando su soundlatch2
    // (porta 0x18) e pulsa l'IRQ (porta 0x14); l'8039 legge il comando dal
    // BUS/MOVX e riproduce il campione scrivendo P1 -> i8039_dac (DAC).
    // Vedi MAME konami/gyruss.cpp. Steppato per-campione nel render audio
    // (audio.cpp, MCH_GYRUSS), NON in run_audio_batch (crackle da raffiche).
    void wrI8048_port(struct i8048_state_S *state, unsigned char port, unsigned char pos) override;
    unsigned char rdI8048_port(struct i8048_state_S *state, unsigned char port) override;
    unsigned char rdI8048_xdm(struct i8048_state_S *state, unsigned char addr) override;
    unsigned char rdI8048_rom(struct i8048_state_S *state, unsigned short addr) override;
    inline int  i8039_sample() { return (int)i8039_dac - 128; }   // DAC centrato per il mix
    inline void step_i8039()   { i8048_step(&i8039_cpu); }        // 1 istruzione 8039
    // IRQ passato via flag volatile (Z80 audio e render girano su core diversi);
    // il core la azzera da solo all'acknowledge (irq_oneshot=1, HOLD_LINE)
    inline void service_i8039_irq() {
        if (i8039_irq_pending) { i8039_cpu.notINT = false; i8039_irq_pending = 0; }
    }
    // Chiamata dal render audio una volta per campione (24kHz). NON steppa
    // l'8039: steppare qui (loopTask) congela gyruss su HW (verificato con
    // stub 2026-07-11). Consuma solo il ring riempito da run_frame sul core
    // di emulazione (produce_drum_samples, stile transfer buffer dkong);
    // a ring vuoto ripete l'ultimo campione. Ritorna il campione gia'
    // scalato per GYRUSS_DRUMS_VOLUME (audio.cpp non scala piu').
    int renderDrumSample() override {
#if GYRUSS_ENABLE_DRUMS
        if (drum_rd != drum_wr) {
            drum_last = drum_ring[drum_rd];
            drum_rd = (drum_rd + 1) & (GYR_DRUM_RING - 1);
        }
        return ((int)drum_last - 128) * GYRUSS_DRUMS_VOLUME;
#else
        return 0;
#endif
    }

    void run_frame(void) override;
    void prepare_frame(void) override;
    void render_row(short row) override;
    const unsigned short *logo(void) override;

    // High score persistente (NVS). OBBLIGATORIO bypassare rdZ80/wrZ80:
    // il dispatch main/audio usa is_audio_cpu() = xPortGetCoreID() !=
    // emu_core_id, ma il manager hiscore gira nel loop() Arduino, che sta
    // sul core OPPOSTO al task emulazione (emu_core_id) -> per il manager
    // is_audio_cpu() e' TRUE e rdZ80 risolverebbe 0x9488/0x940b nella mappa
    // della CPU AUDIO (che li' ritorna 0xFF fisso e scarta le scritture):
    // gate di init mai combaciante, inject perso (bug "hiscore non funziona"
    // trovato su HW 2026-07-10). Entrambe le regioni stanno nella work RAM
    // main 0x9000-0x9FFF -> accesso diretto a memory[], nessun side effect.
    const char *hiscoreKey() override { return "gyruss"; }
    const hiscore_region_S *hiscoreRegions(unsigned char *count) override;
    unsigned char hiscoreRead(unsigned short addr) override { return memory[GYR_WRAM_OFF + (addr - 0x9000)]; }
    void hiscoreWrite(unsigned short addr, unsigned char value) override { memory[GYR_WRAM_OFF + (addr - 0x9000)] = value; }

#ifdef LED_PIN
    void menuLeds(CRGB *leds) override;
    void gameLeds(CRGB *leds) override;
#endif

    // Audio Z80 dual-core support
    void start_audio_task();
    void stop_audio_task();
    void run_audio_batch(int steps);
    inline bool is_audio_cpu();
    volatile uint8_t audio_running;

protected:
    void blit_tile(short row, char col);
    void blit_sprite(short row, unsigned char s_idx);

private:
    void preapre_sprites(unsigned char *sr);
    // M6809 sub-CPU
    m6809_state sub_cpu;
    unsigned char sub_ram[0x800];     // Sub-CPU local RAM (0x4000-0x47FF)
    unsigned char shared_ram[0x800];  // Shared RAM (Z80: 0xA000-0xA7FF, M6809: 0x6000-0x67FF)

    // Audio
    volatile unsigned char sound_latch;
    unsigned char sound_latch_pending;
    volatile unsigned char sound_irq_pending;   // latched IRQ for audio Z80
    unsigned char ay_address[5];                // 5 AY address latches
    volatile unsigned long audio_cycle_approx;  // approximate audio Z80 cycle counter

    // i8039 sample MCU state (drums)
    struct i8048_state_S i8039_cpu;
    volatile unsigned char soundlatch2;         // comando dallo Z80 audio (porta 0x18)
    volatile unsigned char i8039_dac;           // P1 = valore DAC corrente (campione)
    volatile unsigned char i8039_irq_pending;   // IRQ 8039 latchato dallo Z80 (porta 0x14)

    // Ring SPSC campioni drums: produttore run_frame (core emulazione, 24
    // step 8039 per slot), consumatore renderDrumSample (loopTask). Indici
    // gia' mascherati, un solo scrittore ciascuno -> nessun lock necessario.
    void produce_drum_samples();
    unsigned char drum_ring[GYR_DRUM_RING];
    volatile unsigned short drum_wr, drum_rd;
    unsigned char drum_last;                    // ultimo campione consumato (fallback ring vuoto)

    // Audio dual-core
    TaskHandle_t audio_task_handle = NULL;
    char emu_core_id;
    
    // Video
    unsigned char flip_screen;
    unsigned char scanline_counter;
    
#ifdef LED_PIN
    const CRGB menu_leds[7] = { LED_BLUE, LED_CYAN, LED_WHITE, LED_CYAN, LED_WHITE, LED_CYAN, LED_BLUE };
#endif
};

#endif
