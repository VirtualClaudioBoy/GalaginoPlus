#ifndef MOTOS_H
#define MOTOS_H

// ============================================================================
// Motos (Namco 1985): Mappy's video board (a 36x60 tilemap that scrolls, with
// two fixed columns each side, and 16x16 sprites at 4 bits a pixel, 256 of
// them here) with a 32 KB program and, like Super Pac-Man, two Namco 56XX
// custom I/O chips. 2x MC6809E @ 1.536MHz (main + sound), WSG 15XX.
//
// MAME references: namco/mappy.cpp, mappy_v.cpp, namcoio.cpp, sound/namco.cpp.
// The video is Mappy's (mappy.cpp here). The rest is kept closer to the
// hardware than Mappy's, because people write games of their own for this
// board: the CPUs are given the board's cycles line by line, the interrupt
// lines stay raised until the program lowers them, SYNC and CWAI wait, and
// the 56XXs answer every mode MAME knows of.
// ============================================================================

#include "../machineBase.h"
#include "../../cpus/m6809/m6809.h"
#include "motos_rom_main.h"
#include "motos_rom_sub.h"
#include "motos_tilemap.h"
#include "motos_spritemap.h"
#include "motos_cmap.h"
#include "motos_wavetable.h"
#include "motos_info.h"
#include "motos_dipswitches.h"
#include "motos_logo.h"

// Video timing as Super Pac-Man's: 264 lines of 96 CPU cycles.
#define MOTOS_LINES            264
#define MOTOS_CYCLES_PER_LINE  96

// memory[] (RAMSIZE): main CPU addresses are the offsets up to 0x27FF:
//   0x0000-0x0FFF  video RAM (tiles 0x000-0x7FF, attributes 0x800-0xFFF)
//   0x1000-0x27FF  work RAM with the sprite RAM inside it
//     (tile/colour @0x1780, Y/X @0x1F80, flags @0x2780)
//   0x2800-0x2BFF  RAM shared by main 0x4000-0x43FF and sub 0x0000-0x03FF
//     (its first 0x40 bytes are the WSG 15XX registers -> soundregs[])
#define MOTOS_SHARED_OFF 0x2800

class motos : public machineBase
{
public:
    motos() : rom_main(motos_rom_main), rom_sub(motos_rom_sub),
              tiles(motos_tilemap), cmap_tiles(motos_colormap_tiles),
              cmap_prio(motos_colormap_tiles_prio),
              cmap_sprites(motos_colormap_sprites), rom_cached(false) { }
    ~motos() { }

    signed char machineType() override { return MCH_MOTOS; }
    signed char useVideoHalfRate() override { return 1; }
    // Two control panels (the second for a cocktail table's far side): games written for the board play both at once
    bool usesSecondPlayer() override { return true; }

    void reset() override;

    unsigned char m6809_read(m6809_state *s, uint16_t addr) override;
    void m6809_write(m6809_state *s, uint16_t addr, uint8_t val) override;
    unsigned char m6809_read_opcode(m6809_state *s, uint16_t addr) override;

    void run_frame(void) override;
    void prepare_frame(void) override;
    void render_row(short row) override;

    // WSG 15XX, 8 voices (Audio::namco_15xx_render_buffer reads soundregs)
    bool hasNamco15xxAudio() override { return true; }
    bool namcoSoundEnabled() override { return wsg_enable != 0; }
    const signed char *waveRom(unsigned char value) override { return motos_wavetable[value & 7]; }

    const unsigned short *logo(void) override { return motos_logo; }

    // High scores, from MAME's hiscore.dat (motos), and only for that program:
    // the regions are that game's RAM.
    const char *hiscoreKey() override { return motos_info[0] ? "motos" : 0; }
    const hiscore_region_S *hiscoreRegions(unsigned char *count) override;
    unsigned char hiscoreRead(unsigned short addr) override { return memory[addr]; }
    void hiscoreWrite(unsigned short addr, unsigned char value) override { memory[addr] = value; }

protected:
    void blit_tile(unsigned short idx, short x, char prio);
    void blit_sprite(short row, unsigned char s) override;

private:
    void render_tiles(short row, char prio);
    void mainlatch_w(uint16_t addr);
    void install_rom_direct(void);
    void run_line(m6809_state *s, short *owed, unsigned char irq_line);

    // --- Namco 56XX custom I/O (2 chips), protocol from MAME namcoio.cpp ---
    struct n56xx_S {
        unsigned char ram[16];      // 16 nibbles as the CPU sees them
        int lastcoins, lastbuttons;
        int credits;
        int coins[2];
        int coins_per_cred[2];
        int creds_per_coin[2];
    };
    void io_chips_reset(void);
    void customio_run(unsigned char chip);
    void handle_coins(unsigned char chip);
    unsigned char io_in(unsigned char chip, unsigned char port);

    m6809_state main_cpu;
    m6809_state sub_cpu;
    short main_owed, sub_owed;      // cycles each CPU has still to run

    // ROMs and tile graphics copied to internal RAM at reset(), as Mappy does.
    const unsigned char *rom_main;               // 32KB, main 0x8000-0xFFFF
    const unsigned char *rom_sub;                // 8KB, sub 0xE000-0xFFFF
    const unsigned short (*tiles)[8];            // 4KB
    const unsigned short (*cmap_tiles)[4];       // 512B
    const unsigned short (*cmap_prio)[4];        // 512B
    const unsigned short (*cmap_sprites)[16];    // 512B
    bool rom_cached;

    n56xx_S io[2];
    unsigned char dipmux_sel;      // LS157: 0 = DSW2 low nibble, 1 = high

    // mainlatch LS259 @0x5000 main / 0x2000 sub (A0 = data):
    // Q0 = sub irq enable, Q1 = main irq enable, Q2 = flip (ignored),
    // Q3 = sound enable, Q4 = !reset of the 56XXs, Q5 = !reset of the sub CPU
    unsigned char main_irq_mask, sub_irq_mask;
    unsigned char main_irq_line, sub_irq_line;   // raised at vblank, lowered by the program
    unsigned char wsg_enable;
    unsigned char sub_reset, io_reset;

    unsigned char scroll;          // 0x3800-0x3FFF: the value is offset >> 3
};

#endif
