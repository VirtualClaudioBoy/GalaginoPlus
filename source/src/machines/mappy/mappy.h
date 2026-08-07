#ifndef MAPPY_H
#define MAPPY_H

// ============================================================================
// Mappy (Namco 1983) — hardware "Super Pacman class":
// 2x MC6809E @ 1.536MHz (main + sound), WSG 15XX 8 voci (clock 24000 Hz =
// sample rate galagino, zero resampling), 2x custom I/O Namco 58XX,
// tilemap 36x60 con scroll + 2x2 colonne fisse, 64 sprite 16x16 4bpp.
//
// Riferimenti MAME: namco/mappy.cpp, mappy_v.cpp, namcoio.cpp, sound/namco.cpp.
// Grafica pre-ruotata ROT90 dal converter (romconv/mappy/mappy_rom_convert.py,
// autotest contro le ROM galaga): asse X galagino (224) = asse Y MAME
// (righe tilemap, scrollato), asse Y galagino (288) = asse X MAME (36 colonne).
// ============================================================================

#include "../machineBase.h"
#include "../../cpus/m6809/m6809.h"
#include "mappy_rom_main.h"
#include "mappy_rom_sub.h"
#include "mappy_tilemap.h"
#include "mappy_spritemap.h"
#include "mappy_cmap.h"
#include "mappy_wavetable.h"
#include "mappy_dipswitches.h"
#include "mappy_logo.h"

// Interleave CPU: per frame MAPPY_SLICES iterazioni da 4 istruzioni per CPU.
// 2x M6809E @ 1.536MHz ~ 25600 cicli/frame ~ 5700 istruzioni reali: 1600*4 =
// 6400 e' gia' oltre la velocita' originale. ATTENZIONE (visto su HW
// 2026-07-12): il gioco lento NON era CPU affamata ma run_frame oltre i
// 16.6ms (frame persi) — alzare le slice PEGGIORA. La cura e' stata copiare
// le ROM in DRAM interna (vedi reset()); tenere 1600 e alzare solo se, con
// DEBUG_TIMING (emulation.h) attivo, i 10-frame stanno sotto i 160ms.
#define MAPPY_SLICES 1600

// Layout del buffer memory[] condiviso (RAMSIZE 16KB), indirizzi CPU main
// IDENTICI agli offset fino a 0x27FF:
//   0x0000-0x0FFF  VRAM (tile 0x000-0x7FF, attributi 0x800-0xFFF)
//   0x1000-0x27FF  work RAM main con sprite RAM annegata
//     (tab1 tile/colore @0x1780, tab2 Y/X @0x1F80, tab3 flags @0x2780)
//   0x2800-0x2BFF  RAM condivisa main 0x4000-0x43FF / sub 0x0000-0x03FF
//     (i primi 0x40 byte sono i registri WSG 15XX -> soundregs[])
#define MAPPY_SHARED_OFF 0x2800

class mappy : public machineBase
{
public:
    mappy() : rom_main(mappy_rom_main), rom_sub(mappy_rom_sub),
              tiles(mappy_tilemap), cmap_tiles(mappy_colormap_tiles),
              cmap_prio(mappy_colormap_tiles_prio),
              cmap_sprites(mappy_colormap_sprites), rom_cached(false) { }
    ~mappy() { }

    signed char machineType() override { return MCH_MAPPY; }
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
    const signed char *waveRom(unsigned char value) override { return mappy_wavetable[value & 7]; }

    const unsigned short *logo(void) override { return mappy_logo; }

    // High score persistente: da MAME hiscore.dat (mappy/mappyj).
    // Tutte le regioni stanno in VRAM/work RAM main (< 0x2800) = accesso
    // diretto a memory[] (rdZ80/wrZ80 qui non esistono, CPU e' un M6809).
    // La regione in VRAM (0x07xx) e' anche il punteggio a video: iniettarla
    // ridisegna da sola il "HIGH SCORE" mostrato al boot.
    const char *hiscoreKey() override { return "mappy"; }
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

    // --- Namco 58XX custom I/O (2 chip), protocollo da MAME namcoio.cpp ---
    struct n58xx_S {
        unsigned char ram[16];      // 16 nibble visti dalla CPU (0xF0 | val)
        int lastcoins, lastbuttons;
        int credits;
        int coins[2];
        int coins_per_cred[2];
        int creds_per_coin[2];
    };
    void io_chips_reset(void);
    void customio_run(unsigned char chip);
    void handle_coins(unsigned char chip, unsigned char swap);
    unsigned char io_in(unsigned char chip, unsigned char port);

    m6809_state main_cpu;
    m6809_state sub_cpu;

    // ROM + grafica tile copiate in DRAM interna (lazy alloc a reset(),
    // stile Phoenix): le due 6809 fetchano ~770k opcode/s e il render fa
    // due passate tile per strip; il traffico verso gli array const in
    // flash sfratta la cache su entrambi i core -> frame persi, gioco
    // lento (visto su HW 2026-07-12). Se l'alloc fallisce i puntatori
    // restano sulla flash (funziona, ma lento). Totale ~37.5KB.
    const unsigned char *rom_main;               // 24KB, main 0xA000-0xFFFF
    const unsigned char *rom_sub;                // 8KB, sub 0xE000-0xFFFF
    const unsigned short (*tiles)[8];            // 4KB, mappy_tilemap
    const unsigned short (*cmap_tiles)[4];       // 512B
    const unsigned short (*cmap_prio)[4];        // 512B
    const unsigned short (*cmap_sprites)[16];    // 512B
    bool rom_cached;

    n58xx_S io[2];
    unsigned char dipmux_sel;      // LS157: 0 = DSW2 nibble basso, 1 = alto

    // mainlatch LS259 @0x5000 main / 0x2000 sub (A0 = dato):
    // Q0 = sub irq mask, Q1 = main irq mask, Q2 = flip (ignorato),
    // Q3 = sound enable, Q4 = !reset namcoio, Q5 = !reset sub CPU
    unsigned char main_irq_mask, sub_irq_mask;
    unsigned char wsg_enable;
    unsigned char sub_reset, io_reset;

    unsigned char scroll;          // 0x3800-0x3FFF: m_scroll = offset >> 3
};

#endif
