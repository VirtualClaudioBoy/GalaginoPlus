#ifndef SUPERPAC_H
#define SUPERPAC_H

// ============================================================================
// Super Pac-Man (Namco 1982): the first board of the family Mappy is from.
// 2x MC6809E @ 1.536MHz (main + sound), WSG 15XX with 8 voices (clock 24000 Hz
// = galagino's sample rate), 2x Namco 56XX custom I/O, a fixed 36x28 tilemap
// and 64 sprites of 16x16 at 2 bits a pixel.
//
// MAME references: namco/mappy.cpp, mappy_v.cpp, namcoio.cpp, sound/namco.cpp.
// Graphics are turned ROT90 by the converter (romconv/superpac/
// superpac_rom_convert.py), as Mappy's are: galagino's X (224) is MAME's Y,
// galagino's Y (288) is MAME's X (the 36 columns).
//
// People write games of their own for this board, so it is kept closer to
// the hardware than a machine that only has to run its one game: the CPUs
// are given the board's cycles line by line, the interrupt lines stay
// raised until the program lowers them, SYNC and CWAI wait, and the 56XX
// answers every mode MAME knows of.
// ============================================================================

#include "../machineBase.h"
#include "../../cpus/m6809/m6809.h"
#include "superpac_rom_main.h"
#include "superpac_rom_sub.h"
#include "superpac_tilemap.h"
#include "superpac_spritemap.h"
#include "superpac_cmap.h"
#include "superpac_wavetable.h"
#include "superpac_info.h"
#include "superpac_dipswitches.h"
#include "superpac_logo.h"

// Video timing: 384 pixel clocks a line, 264 lines (60.6 Hz); the CPUs run at
// a quarter of the pixel clock, so 96 cycles a line and 25344 a frame.
#define SUPERPAC_LINES            264
#define SUPERPAC_CYCLES_PER_LINE  96

// memory[] (RAMSIZE): main CPU addresses are the offsets up to 0x1FFF:
//   0x0000-0x07FF  video RAM (tiles 0x000-0x3FF, attributes 0x400-0x7FF)
//   0x0800-0x1FFF  work RAM with the sprite RAM inside it
//     (tile/colour @0x0F80, Y/X @0x1780, flags @0x1F80)
//   0x2000-0x23FF  RAM shared by main 0x4000-0x43FF and sub 0x0000-0x03FF
//     (its first 0x40 bytes are the WSG 15XX registers -> soundregs[])
#define SUPERPAC_SHARED_OFF 0x2000

class superpac : public machineBase
{
public:
    superpac() : rom_main(superpac_rom_main), rom_sub(superpac_rom_sub),
                 tiles(superpac_tilemap), cmap_tiles(superpac_colormap_tiles),
                 cmap_prio(superpac_colormap_tiles_prio),
                 cmap_sprites(superpac_colormap_sprites),
                 cmap_super(superpac_colormap_sprites_super), rom_cached(false) { }
    ~superpac() { }

    signed char machineType() override { return MCH_SUPERPAC; }
    signed char useVideoHalfRate() override { return 1; }

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
    const signed char *waveRom(unsigned char value) override { return superpac_wavetable[value & 7]; }

    const unsigned short *logo(void) override { return superpac_logo; }

    // High scores, from MAME's hiscore.dat (superpac/superpacm), and only for
    // those two programs: the regions are that game's RAM, and writing them
    // into another program's would corrupt it.
    const char *hiscoreKey() override { return superpac_info[0] ? "superpac" : 0; }
    const hiscore_region_S *hiscoreRegions(unsigned char *count) override;
    unsigned char hiscoreRead(unsigned short addr) override { return memory[addr]; }
    void hiscoreWrite(unsigned short addr, unsigned char value) override { memory[addr] = value; }

protected:
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

    // ROMs and tile graphics copied to internal RAM at reset(), as Mappy does
    // and for the same reason (see mappy.h). If that fails they stay where
    // the build put them.
    const unsigned char *rom_main;               // 24KB, main 0xA000-0xFFFF
    const unsigned char *rom_sub;                // 8KB, sub 0xE000-0xFFFF
    const unsigned short (*tiles)[8];            // 4KB
    const unsigned short (*cmap_tiles)[4];       // 512B
    const unsigned short (*cmap_prio)[4];        // 512B
    const unsigned short (*cmap_sprites)[4];     // 512B
    const unsigned short (*cmap_super)[4];       // 512B
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

    // Sprite pixels in pen 0 or 1 stay in front of the priority tiles (MAME's
    // "super priority"): one strip of them, 0 where there is none.
    unsigned short super_px[224 * 8];
    bool strip_sprites, strip_super;
};

#endif
