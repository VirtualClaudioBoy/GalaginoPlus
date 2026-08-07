#ifndef XEVIOUS_H
#define XEVIOUS_H

#include "xevious_rom_cpu1.h"
#include "xevious_rom_cpu2.h"
#include "xevious_rom_cpu3.h"
#include "xevious_planetmap.h"
#include "xevious_fgtilemap.h"
#include "xevious_bgtilemap.h"
#include "xevious_spritemap.h"
#include "xevious_cmap_fg.h"
#include "xevious_cmap_bg.h"
#include "xevious_cmap_sprites.h"
#include "xevious_wavetable.h"
#include "xevious_sample_boom.h"
#include "xevious_sample_boom2.h"
#include "xevious_dipswitches.h"
#include "xevious_logo.h"
#include "../machineBase.h"

// ============================================================
// Xevious (Namco 1982) — 3xZ80 (main/motion/sound), stessa famiglia
// hardware di Galaga (namco 06XX+50XX+51XX+54XX, namco WSG 3 voci),
// video completamente diverso: due tilemap RAM 64x32 (fg testo + bg
// scrollabile su 2 assi), sfondo popolato a runtime dalla CPU leggendo
// una funzione di lookup su 3 ROM ("planet map", 0xF000-0xFFFF).
//
// Fonti: E:\Download\galaga.cpp (driver condiviso, ROM_START/memory map/
// machine_config), E:\Download\xevious.h/.cpp (classe reale, video).
// xevious_m.cpp NON usato: riguarda solo il bootleg battles_state, la
// xevious_state reale usa i chip Namco veri via device MAME, non uno
// shortcut ad-hoc — qui replichiamo pero' lo SHORTCUT gia' collaudato di
// galaga.cpp (risposta diretta ai comandi 06xx/51xx invece del protocollo
// byte-per-byte reale), stessa famiglia di chip.
//
// Memory map CPU1 (0x0000-0x3fff ROM, 16KB):
//  0x6800-0x6807 r   dsw (bit0=DSW0,bit1=DSW1 interleaved per indirizzo)
//  0x6800-0x681f w   registri Namco WSG (suono, 3 voci)
//  0x6820-0x6827 w   latch ls259: bit0=irq1(main) bit1=irq2(sub/motion)
//                     bit2=nmi(sub2/sound) bit3=reset sub+sub2 (invertito:
//                     1=running, 0=reset) + reset 50xx/51xx/54xx
//  0x6830        w   watchdog (no-op)
//  0x7000-0x70ff rw  namco 06xx data port (51xx/50xx multiplexati)
//  0x7100        rw  namco 06xx control/command port
//  0x7800-0x7fff rw  work RAM
//  0x8000-0x87ff rw  work RAM + sr1 (Y/X posizione sprite, ultimi 0x80 byte)
//  0x9000-0x97ff rw  work RAM + sr2 (size/flip/mode sprite, ultimi 0x80 byte)
//  0xa000-0xa7ff rw  work RAM + sr3 (codice/colore sprite, ultimi 0x80 byte)
//  0xb000-0xb7ff rw  fg colorram (attributo tile testo)
//  0xb800-0xbfff rw  bg colorram (attributo tile sfondo)
//  0xc000-0xc7ff rw  fg videoram (codice tile testo)
//  0xc800-0xcfff rw  bg videoram (codice tile sfondo)
//  0xd000-0xd07f w   scroll/flip latch (reg0=bg scrollx reg1=fg scrollx
//                     reg2=bg scrolly reg3=fg scrolly reg7=flip, 9 bit:
//                     valore=data|((Addr&1)<<8))
//  0xf000-0xffff rw  xevious_bs_w (latch 2 byte) / xevious_bb_r (planet map)
// CPU2 (0x0000-0x1fff ROM, 8KB) e CPU3 (0x0000-0x0fff ROM, 4KB) condividono
// le stesse finestre RAM/video/scroll; CPU3 in piu' ha i registri WSG a
// 0x6800-0x681f (stesso indirizzo, CPU diversa: sound CPU scrive i suoni).
//
// Rotazione ROT90: Xevious e' ROT90 come Galaga (framebuffer raw
// landscape 288x224, MAME applica ROT90). Convenzione (bring-up #27):
// asse MAME "sy" (0-223) -> X portrait RIBALTATO (XP = C - sy, forma
// ancorata dal testo leggibile su HW); asse MAME "sx" (0-287) -> Y
// portrait diretto. Applicata identica a sprite e tilemap.
// ============================================================

class xevious : public machineBase
{
public:
  xevious() { }
  ~xevious() { }

  signed char machineType() override { return MCH_XEVIOUS; }
  // videoFlipX/Y: nessuna evidenza di un flip necessario (derivazione
  // modellata su galaxian.cpp, che usa i default 0/0) — se la preview su
  // HW risulta specchiata, aggiustare qui prima di toccare le formule.

  unsigned char rdZ80(unsigned short Addr) override;
  void wrZ80(unsigned short Addr, unsigned char Value) override;
  unsigned char opZ80(unsigned short Addr) override;

  // nessun indirizzo di boot noto per rilevare "partita iniziata" (servirebbe
  // disassemblare la ROM): game_started forzato a 1 subito, stesso approccio
  // di turtles/rocnrope/circusc/anteater (nessuna euristica). Costo: il boot
  // gira gia' sincronizzato a 60Hz invece che a tutta velocita', cosmetico.
  void start(void) override;
  void run_frame(void) override;
  void prepare_frame(void) override;
  void render_row(short row) override;

  const signed char *waveRom(unsigned char value) override;
  const unsigned short *logo(void) override;
  bool hasNamcoAudio() override { return true; }

  // esplosioni: campioni PCM digitalizzati (stesso schema di
  // galaga::snd_boom_cnt/ptr — vedi trigger_sound_explosion())
  unsigned short snd_boom_cnt = 0;
  const signed char *snd_boom_ptr = NULL;
  unsigned char snd_boom_ship = 0;   // 1 = sta suonando explo2 (nave/tipo A)

  // persistent high scores (voce hiscore.dat MAME per il gruppo
  // battles/sxevious/xevios/xevious/xeviousa — vedi xevious.cpp)
  const char *hiscoreKey() override { return "xevious"; }
  const hiscore_region_S *hiscoreRegions(unsigned char *count) override;

protected:
  void blit_tile(short row, char col) override { }   // non usato: vedi blit_tilemap_row
  void blit_sprite(short row, unsigned char s) override;
  void blit_tilemap_row(short row, bool bg);

private:
  void trigger_sound_explosion(unsigned char ship);
  unsigned char n54_skip = 0;   // byte parametro 54xx ancora da consumare

  // Cache DRAM lazy (bring-up #27, ricetta mappy): al primo start() ROM cpu,
  // planet-map e gfx tile/colormap vengono copiati in DRAM interna (~58KB);
  // gli sprite (160KB) restano in flash. Se l'alloc fallisce i puntatori
  // restano sugli array flash (fallback trasparente).
  const unsigned char  *rom_cpu1  = xevious_rom_cpu1;
  const unsigned char  *rom_cpu2  = xevious_rom_cpu2;
  const unsigned char  *rom_cpu3  = xevious_rom_cpu3;
  const unsigned char  *planetmap = xevious_planetmap;
  const unsigned char  (*fgtiles)[8]  = xevious_fgtilemap;
  const unsigned short (*bgtiles)[8]  = xevious_bgtilemap;
  const unsigned short (*cmap_fg)[2]  = xevious_colormap_fg;
  const unsigned short (*cmap_bg)[4]  = xevious_colormap_bg;
  const unsigned short (*cmap_spr)[8] = xevious_colormap_sprites;
  bool rom_cached = false;

  // planet-map lookup (schematic 9B), formula esatta da xevious_bb_r()
  unsigned char xevious_bb_r(unsigned char offset);
  void xevious_bs_w(unsigned char offset, unsigned char value);
  unsigned char xevious_bs[2] = { 0, 0 };

  // registri scroll/flip (0xd000-0xd07f), 9 bit ciascuno
  unsigned short bg_scrollx = 0, bg_scrolly = 0;
  unsigned short fg_scrollx = 0, fg_scrolly = 0;

  // latch ls259 0x6820-0x6827 e stato IO namco 06xx/51xx/50xx (shortcut,
  // stesso schema di galaga.cpp)
  char sub_cpu_reset = 1;
  unsigned char cs_ctrl = 0;
  int namco_cnt = 0;
  int namco_busy = 0;
  int nmi_cnt = 0;
  unsigned char credit = 0;
  char credit_mode = 0;
  int coincredMode = 0;
  unsigned long rng_state = 0xACE1u;   // 50xx: LFSR semplice, non ciclo-esatto
  // 50xx protezione (Xevious): il boot invia un parametro via comando 0x64 e
  // poi legge 4 byte via 0x74; il 4o byte deve valere 0x05 se l'ultimo
  // parametro era 0x80/0x10, altrimenti 0x95 (derivato dai due check a
  // 0x3b9e/0x3bc5 della ROM: CP 5 / CP 0x95 -> JP nz,0x0000). Senza questo il
  // gioco si auto-resetta a ogni ciclo.
  unsigned char prot_param = 0;

  // 06xx serializzazione: 1 = trasferimento in corso ("busy"). Alzato da un
  // comando di trasferimento (bit 0xe0) su 0x7100, abbassato dal deselect 0x10
  // che il gioco scrive a fine trasferimento (handler NMI 0x006f). rd(0x7100)
  // ritorna 0xe0 quando busy, cosi' comandi 06xx concorrenti (es. la lettura
  // input 0x71 dell'ISR mentre e' in corso la protezione 0x74) vengono ACCODATI
  // dal gioco (path 0x00f8) invece di sovrascrivere cs_ctrl a meta' trasferimento
  // (era la causa del reset-loop protezione: vedi project_xevious.md #17/#20).
  unsigned char xfer_busy = 0;

  // offset RAM dentro il buffer condiviso memory[] (8 finestre da 0x800)
  static constexpr unsigned short RAM_WORK    = 0 * 0x800;  // 0x7800-0x7fff
  static constexpr unsigned short RAM_SR1     = 1 * 0x800;  // 0x8000-0x87ff (Y/X)
  static constexpr unsigned short RAM_SR2     = 2 * 0x800;  // 0x9000-0x97ff (size/flip)
  static constexpr unsigned short RAM_SR3     = 3 * 0x800;  // 0xa000-0xa7ff (code/color)
  static constexpr unsigned short RAM_FGCOLOR = 4 * 0x800;  // 0xb000-0xb7ff
  static constexpr unsigned short RAM_BGCOLOR = 5 * 0x800;  // 0xb800-0xbfff
  static constexpr unsigned short RAM_FGVIDEO = 6 * 0x800;  // 0xc000-0xc7ff
  static constexpr unsigned short RAM_BGVIDEO = 7 * 0x800;  // 0xc800-0xcfff
};

#endif
