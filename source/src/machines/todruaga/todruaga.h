#ifndef TODRUAGA_H
#define TODRUAGA_H

// ============================================================================
// The Tower of Druaga (Namco 1984) — STESSO hardware di Mappy ("Super Pacman
// class", driver MAME mappy.cpp, config todruaga = digdug2 + gfx_todruaga):
// 2x MC6809E @ 1.536MHz (main + sound), WSG 15XX 8 voci a 24kHz nativi,
// tilemap 36x60 con scroll + 2x2 colonne fisse, 64 sprite 16x16 4bpp.
//
// Differenze rispetto a mappy (vedi src/machines/mappy/mappy.h per tutto
// il resto, incluse le note sulle prestazioni rom_direct/DRAM):
//  - ROM main 32KB @0x8000-0xFFFF (mappy: 24KB @0xA000)
//  - namcoio#1 (0x4810) e' un 56XX, non un 58XX: dipmux al mode 9 (non 4)
//    e bootcheck a CHECKSUM al mode 8 (niente LFSR); il 58XX resta su
//    namcoio#0 per crediti/input
//  - sprite con 64 gruppi colore (lookup PROM td1-7.5k da 0x400 byte)
//  - joystick 4 direzioni + bottone (mappy: solo destra/sinistra)
// ============================================================================

#include "../machineBase.h"
#include "../../cpus/m6809/m6809.h"
#include "todruaga_rom_main.h"
#include "todruaga_rom_sub.h"
#include "todruaga_tilemap.h"
#include "todruaga_spritemap.h"
#include "todruaga_cmap.h"
#include "todruaga_wavetable.h"
#include "todruaga_dipswitches.h"
#include "todruaga_logo.h"

// Interleave CPU identico a mappy (vedi commento in mappy.h: NON alzare
// senza controllare DEBUG_TIMING, run_frame oltre i 16.6ms = frame persi)
#define TODRUAGA_SLICES 1600

// Layout del buffer memory[] condiviso (RAMSIZE 16KB), indirizzi CPU main
// IDENTICI agli offset fino a 0x27FF (come mappy):
//   0x0000-0x0FFF  VRAM (tile 0x000-0x7FF, attributi 0x800-0xFFF)
//   0x1000-0x27FF  work RAM main con sprite RAM annegata
//     (tab1 tile/colore @0x1780, tab2 Y/X @0x1F80, tab3 flags @0x2780)
//   0x2800-0x2BFF  RAM condivisa main 0x4000-0x43FF / sub 0x0000-0x03FF
//     (i primi 0x40 byte sono i registri WSG 15XX -> soundregs[])
#define TODRUAGA_SHARED_OFF 0x2800

class todruaga : public machineBase
{
public:
    todruaga() : rom_main(todruaga_rom_main), rom_sub(todruaga_rom_sub),
                 tiles(todruaga_tilemap), cmap_tiles(todruaga_colormap_tiles),
                 cmap_prio(todruaga_colormap_tiles_prio),
                 cmap_sprites(todruaga_colormap_sprites), rom_cached(false) { }
    ~todruaga() { }

    signed char machineType() override { return MCH_TODRUAGA; }
    signed char useVideoHalfRate() override { return 1; }

    void reset() override;

    unsigned char m6809_read(m6809_state *s, uint16_t addr) override;
    void m6809_write(m6809_state *s, uint16_t addr, uint8_t val) override;
    unsigned char m6809_read_opcode(m6809_state *s, uint16_t addr) override;

    void run_frame(void) override;
    void prepare_frame(void) override;
    void render_row(short row) override;

    // audio WSG 15XX 8 voci (Audio::namco_15xx_render_buffer legge soundregs)
    bool hasNamco15xxAudio() override { return true; }
    bool namcoSoundEnabled() override { return wsg_enable != 0; }
    const signed char *waveRom(unsigned char value) override { return todruaga_wavetable[value & 7]; }

    const unsigned short *logo(void) override { return todruaga_logo; }

    // High score persistente: da MAME hiscore.dat (todruaga/todruagao/
    // todruagas). Entrambe le regioni sono in work RAM main (< 0x2800) =
    // accesso diretto a memory[] (la CPU e' un M6809, rdZ80/wrZ80 non esistono)
    const char *hiscoreKey() override { return "todruaga"; }
    const hiscore_region_S *hiscoreRegions(unsigned char *count) override;
    unsigned char hiscoreRead(unsigned short addr) override { return memory[addr]; }
    void hiscoreWrite(unsigned short addr, unsigned char value) override { memory[addr] = value; }

protected:
    void blit_tile(unsigned short idx, short x, char prio);
    void blit_sprite(short row, unsigned char s);

private:
    void render_tiles(short row, char prio);
    void mainlatch_w(uint16_t addr);
    void install_rom_direct(void);

    // --- Namco custom I/O: chip 0 = 58XX (crediti/input), chip 1 = 56XX
    // (DIP switch); protocolli da MAME namcoio.cpp ---
    struct nio_S {
        unsigned char ram[16];      // 16 nibble visti dalla CPU (0xF0 | val)
        int lastcoins, lastbuttons;
        int credits;
        int coins[2];
        int coins_per_cred[2];
        int creds_per_coin[2];
    };
    void io_chips_reset(void);
    void customio_run_58(unsigned char chip);
    void customio_run_56(unsigned char chip);
    void handle_coins(unsigned char chip, unsigned char swap);
    unsigned char io_in(unsigned char chip, unsigned char port);

    m6809_state main_cpu;
    m6809_state sub_cpu;

    // ROM + grafica copiate in DRAM interna al reset (lazy, stile mappy:
    // vedi commento dettagliato in mappy.h). Totale ~47KB: ROM main 32KB +
    // ROM sub 8KB + tilemap 4KB + colormap 3KB (le sprite hanno 64 gruppi).
    // Se l'alloc fallisce i puntatori restano sulla flash (lento ma vivo).
    const unsigned char *rom_main;               // 32KB, main 0x8000-0xFFFF
    const unsigned char *rom_sub;                // 8KB, sub 0xE000-0xFFFF
    const unsigned short (*tiles)[8];            // 4KB, todruaga_tilemap
    const unsigned short (*cmap_tiles)[4];       // 512B
    const unsigned short (*cmap_prio)[4];        // 512B
    const unsigned short (*cmap_sprites)[16];    // 2KB (64 gruppi x 16)
    bool rom_cached;

    nio_S io[2];
    unsigned char dipmux_sel;      // LS157: 0 = DSW2 nibble basso, 1 = alto

    // mainlatch LS259 @0x5000 main / 0x2000 sub (A0 = dato), come mappy:
    // Q0 = sub irq mask, Q1 = main irq mask, Q2 = flip (ignorato),
    // Q3 = sound enable, Q4 = !reset namcoio, Q5 = !reset sub CPU
    unsigned char main_irq_mask, sub_irq_mask;
    unsigned char wsg_enable;
    unsigned char sub_reset, io_reset;

    unsigned char scroll;          // 0x3800-0x3FFF: m_scroll = offset >> 3
};

#endif
