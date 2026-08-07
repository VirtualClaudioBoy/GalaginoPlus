#ifndef GAPLUS_H
#define GAPLUS_H

// ============================================================================
// Gaplus / Galaga 3 (Namco 1984) — 3x MC6809E @ 1.536MHz (main, sub, sub2
// alias "sound"), WSG 15XX 8 voci (come mappy/todruaga), 2x custom I/O
// Namco 56XX (input) + 58XX (dip), tilemap 36x28 STATICA (NO scroll, stesso
// hw video di galaga: tileaddr.h riusato verbatim, stesso split videoram
// tile/attr 0x400+0x400, stesse 2 categorie di priorita'), sprite 16x16
// 3bpp con raddoppio INDIPENDENTE in X/Y + flag "duplicate", starfield CUS26
// a 3 set con velocita' indipendenti.
//
// Riferimenti MAME: namco/gaplus.cpp, gaplus_v.cpp, gaplus_m.cpp, namcoio.cpp.
// Grafica pre-ruotata ROT90 dal converter (romconv/gaplus/gaplus_rom_convert.py).
//
// ATTENZIONE romset IBRIDO (vedi memoria project_gaplus.md): main+sub code
// = "galaga3", gfx1/color sprite = variante "gaplus" (gp2-*): differenza
// quasi solo cosmetica, non impatta la logica di gioco.
//
// Indirizzamento a BIT DI INDIRIZZO (non LS259 dati come mappy/circusc):
// irq_1/2/3_ctrl_w, sreset_w, freset_w decodificano solo l'INDIRIZZO
// scritto (qualunque valore va bene), vedi m6809_write() in gaplus.cpp.
// ============================================================================

#include "../machineBase.h"
#include "../../cpus/m6809/m6809.h"
#include "../tileaddr.h"
#include "gaplus_rom_main.h"
#include "gaplus_rom_sub.h"
#include "gaplus_rom_sub2.h"
#include "gaplus_tilemap.h"
#include "gaplus_spritemap.h"
#include "gaplus_cmap.h"
#include "gaplus_wavetable.h"
#include "gaplus_starseed.h"
#include "gaplus_dipswitches.h"
#include "gaplus_logo.h"
#include "gaplus_sample_bang.h"

// Interleave CPU: 3x M6809E fini, stesso schema di mappy/todruaga (MAI
// alzare senza controllare DEBUG_TIMING: run_frame oltre i 16.6ms = frame
// persi, vedi project_mappy.md).
#define GAPLUS_SLICES 1600

// Layout memory[] (RAMSIZE 16KB, qui ne servono solo 0x2000 = 8KB):
//   0x0000-0x07FF  videoram (tile 0x000-0x3FF, attr 0x400-0x7FF) — shared main+sub
//   0x0800-0x1FFF  spriteram (0x1800), shared main+sub
// Il sub2 (sound) NON accede a queste zone: solo namco_15xx (soundregs[]).

class gaplus : public machineBase
{
public:
    gaplus() : rom_main(gaplus_rom_main), rom_sub(gaplus_rom_sub),
               rom_sub2(gaplus_rom_sub2), tiles(gaplus_tilemap),
               cmap_tiles(gaplus_colormap_tiles), cmap_prio(gaplus_colormap_tiles_prio),
               cmap_sprites(gaplus_colormap_sprites), rom_cached(false) { }
    ~gaplus() { }

    signed char machineType() override { return MCH_GAPLUS; }
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
    const signed char *waveRom(unsigned char value) override { return gaplus_wavetable[value & 7]; }

    const unsigned short *logo(void) override { return gaplus_logo; }

    // campione esplosione "bang" (customio_3, offset9>=0x0F), mixato in
    // Audio::namco_15xx_render_buffer (gated MCH_GAPLUS), stesso schema di
    // galaga::snd_boom_cnt/ptr ma a piena velocita' (campione gia' a 24kHz,
    // il puntatore avanza ad OGNI campione, non ogni due come galaga)
    unsigned short snd_bang_cnt = 0;
    const signed char *snd_bang_ptr = NULL;

    // High score persistente: da MAME hiscore.dat (namco/gaplus.cpp, voce
    // condivisa da galaga3/galaga3a/.../gaplus/gaplusa/gaplusd/gaplust —
    // il nostro romset e' "galaga3" per il codice CPU, vedi project_gaplus.md).
    // Tutte le regioni sono in RAM pura (videoram/spriteram condivisa,
    // addr<0x2000): nessun side-effect, memory[addr] diretto e' sicuro.
    const char *hiscoreKey() override { return "galaga3"; }
    const hiscore_region_S *hiscoreRegions(unsigned char *count) override;
    unsigned char hiscoreRead(unsigned short addr) override { return memory[addr]; }
    void hiscoreWrite(unsigned short addr, unsigned char value) override { memory[addr] = value; }

protected:
    void blit_tile(short row, char col, char prio);
    void blit_sprite(short row, unsigned char s);

private:
    void install_rom_direct(void);
    unsigned char customio3_r(unsigned char offset);

    // --- Namco 56XX (input, chip0) + 58XX (dip, chip1) — protocollo
    // fedele a MAME namcoio.cpp, riuso diretto da todruaga.cpp (qui i
    // ruoli sono INVERTITI: 56xx=input, 58xx=dip, vedi project_gaplus.md) ---
    struct nio_S {
        unsigned char ram[16];
        int lastcoins, lastbuttons;
        int credits;
        int coins[2];
        int coins_per_cred[2];
        int creds_per_coin[2];
    };
    void io_chips_reset(void);
    void customio_run_56(unsigned char chip);
    void customio_run_58(unsigned char chip);
    void handle_coins(unsigned char chip, unsigned char swap);
    unsigned char io_in(unsigned char chip, unsigned char port);

    m6809_state main_cpu;
    m6809_state sub_cpu;
    m6809_state sub2_cpu;   // sound CPU (namco_15xx)

    // namco_15xx amap (MAME sound/namco.cpp): 0x000-0x03F = registri
    // (soundregs[]), 0x040-0x3FF = RAM CONDIVISA VERA (map(...).ram()),
    // non solo "resto ignorato" — il self-test di boot del gioco la
    // scrive/rilegge per intero e si blocca se non si comporta da RAM
    // (bug trovato 2026-07-13 con harness Python mc6809, vedi
    // project_gaplus.md: senza questo il main CPU si impalla in un loop
    // di verifica poco dopo l'inizializzazione).
    unsigned char namco15xx_ram[0x3C0];

    // ROM + grafica copiate in DRAM interna al reset (lazy, stile mappy:
    // vedi project_mappy.md per il perche'). Totale ~66KB: main 24KB +
    // sub 24KB + sub2 8KB + tilemap 8KB + colormap ~2.5KB.
    const unsigned char *rom_main;    // 24KB, main 0xA000-0xFFFF
    const unsigned char *rom_sub;     // 24KB, sub  0xA000-0xFFFF (ALTRO bus)
    const unsigned char *rom_sub2;    // 8KB,  sub2 0xE000-0xFFFF
    const unsigned short (*tiles)[8];            // 8KB, gaplus_tilemap (512 tile)
    const unsigned short (*cmap_tiles)[4];        // 512B
    const unsigned short (*cmap_prio)[4];         // 512B
    const unsigned short (*cmap_sprites)[8];      // 1KB (64 gruppi x8 pen)
    bool rom_cached;

    nio_S io[2];
    unsigned char dipmux_sel;
    unsigned char customio3_ram[16];   // chip semplice @0x6820 (IN2 cabinet/service)

    // controllo indirizzo-decodificato (vedi gaplus.cpp per i range esatti)
    unsigned char main_irq_mask, sub_irq_mask, sub2_irq_mask;
    unsigned char subs_reset;      // 1 = sub+sub2 IN reset (sreset_w)
    unsigned char io_reset;        // 1 = namcoio IN reset (freset_w)
    unsigned char wsg_enable;      // combinato con subs_reset (stesso bit HW)

    // starfield CUS26: 3 set, velocita' indipendente (registro raw MAME)
    unsigned char starfield_control[4];
    float star_x[GAPLUS_NUM_STARS];
    float star_y[GAPLUS_NUM_STARS];
    unsigned short star_col[GAPLUS_NUM_STARS];
    unsigned char star_set[GAPLUS_NUM_STARS];
    int starfield_framecount;
    void starfield_advance(void);
};

#endif
