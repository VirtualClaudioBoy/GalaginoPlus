#ifndef BNJ_H
#define BNJ_H

#include "bnj_rom_main.h"
#include "bnj_rom_audio.h"
#include "bnj_chartiles.h"
#include "bnj_spritetiles.h"
#include "bnj_bgtiles.h"
#include "bnj_logo.h"
#include "bnj_dipswitches.h"
#include "../../cpus/m6502/m6502.h"
#include "../btime/btime.h"

// ============================================================
// Bump'n'Jump (Data East 1982, set "bnjm" / Bally Midway license) memory
// map (da E:\Download\btime.cpp, stesso driver di Burger Time -- funzione
// bnj_map/bnj()/screen_update_bnj/gfx_bnj/init_bnj, letto per intero, NON
// mame4all). Eredita da `btime` (STESSO PCB, hardware audio IDENTICO) per
// riusare cpu_audio/audio_read/audio_write/palette_write/work_ram/
// video_ram/color_ram/audio_ram/bnj_scroll/vblank_bit/coin_prev invariati
// -- vedi commenti "protected" in btime.h.
//
// Main CPU (DECO C10707, 6502 cifrato STATICO @ 12MHz/2/2/2/2 = 750KHz --
// bitswap<8>(v,7,5,6,4,3,2,1,0) = scambio bit5<->bit6 SU OGNI fetch
// opcode, NESSUNO stato, diverso dalla CPU-7 dinamica di Burger Time):
//   0x0000-0x07ff: work RAM
//   0x1000: DSW1 (r, bit7=VBLANK dinamico)
//   0x1001: DSW2 (r) / bnj_video_control_w (w, solo se cocktail -- noop in upright)
//   0x1002: P1 (r) / soundlatch (w)
//   0x1003: P2 (r, cocktail, non usato)
//   0x1004: SYSTEM (r)
//   0x4000-0x43ff: videoram
//   0x4400-0x47ff: colorram
//   0x4800-0x4bff: mirror videoram (X/Y scambiati)
//   0x4c00-0x4fff: mirror colorram (X/Y scambiati)
//   0x5000-0x51ff: bnj_backgroundram (sfondo scrollabile, 512 celle 16x16)
//   0x5200-0x53ff: RAM libera (nessun side effect, backed dallo stesso
//                  buffer di bnj_backgroundram per semplicita')
//   0x5400: scroll[0] (w) -- riusa bnj_scroll[0] ereditato da btime
//   0x5800: scroll[1] (w) -- riusa bnj_scroll[1] ereditato da btime
//   0x5c00-0x5c0f: palette RAM (w, STESSO formato BGR_233_inverted di
//                  btime -- PALETTE(config) ereditata invariata da bnj())
//   0xa000-0xffff: ROM cifrata C10707 (24KB)
//
// Audio CPU: IDENTICA a Burger Time (bnj() eredita m_audiocpu/audio_map
// da btime() senza sovrascriverli in machine_config) -- riusa
// btime::audio_read/audio_write invariati.
//
// Coin: NMI (non IRQ come btime) -- coin_inserted_nmi_lo in btime.cpp,
// fronte di salita, stesso stile edge-detect di btime ma su cpu_main.nmi.
//
// Video: visarea 256x240 (0..255, 8..247, "confirmed" nel commento
// sorgente) -- DIVERSA da btime (240x240), quindi native_width per il
// mapping ROT270 e' 256 non 240. Sfondo: motore NUOVO (copyscrollbitmap
// su bitmap 512x256 "due volte piu' largo dello schermo", commento
// VIDEO_START_MEMBER(bnj) -- l'asse che scorre e' quello orizzontale
// nativo, che sotto ROT270 diventa l'asse VERTICALE portrait, coerente
// con "si scorre mentre si guida").
// ============================================================

class bnj : public btime
{
public:
  bnj() { memset(&cpu_main, 0, sizeof(cpu_main)); memset(&cpu_audio, 0, sizeof(cpu_audio)); }
  ~bnj() {}

  signed char machineType() override { return MCH_BNJ; }
  void start() override;
  void reset() override;

  void run_frame(void) override;
  void prepare_frame(void) override;
  void render_row(short row) override;
  const unsigned short *logo(void) override;

  // High score: da MAME hiscore.dat -- vedi bnj.cpp per la voce reale.
  const char *hiscoreKey() override { return "bnj"; }
  const hiscore_region_S *hiscoreRegions(unsigned char *count) override;
  unsigned char hiscoreRead(unsigned short addr) override { return memory[WORK_RAM_OFFSET + addr]; }
  void hiscoreWrite(unsigned short addr, unsigned char value) override { memory[WORK_RAM_OFFSET + addr] = value; }

protected:
  void blit_tile(short row, char col) override;
  void blit_sprite(short row, unsigned char s) override;

private:
  // --- memoria: bg RAM aggiuntiva dopo il layout ereditato da btime
  // (work/video/color/audio RAM condividono ESATTAMENTE gli stessi
  // offset/dimensioni -- vedi btime.h "protected") ---
  static constexpr unsigned short BGRAM_SIZE = 0x0400; // 0x5000-0x53ff (bg vero 0x200 + 0x200 RAM libera)
  static constexpr unsigned short BGRAM_OFFSET = AUDIORAM_OFFSET + AUDIORAM_SIZE;
  static constexpr unsigned short MEM_FREE_BNJ  = BGRAM_OFFSET + BGRAM_SIZE;
  static_assert(MEM_FREE_BNJ <= RAMSIZE, "RAMSIZE too low for bnj");

  unsigned char *bg_ram;

  // --- CPU 6502 principale (DECO C10707 cifrata STATICA) ---
  static uint8_t  main_read (m6502_t *cpu, uint16_t addr);
  static void     main_write(m6502_t *cpu, uint16_t addr, uint8_t val);
  static uint8_t  main_fetch(m6502_t *cpu, uint16_t addr);

  // --- CPU audio: SOLO la lettura va ridefinita (btime::audio_read fa
  // riferimento DIRETTO a btime_rom_audio, hardcoded -- riusarla invariata
  // farebbe eseguire alla CPU audio di bnj la ROM audio di Burger Time!
  // btime::audio_write invece non referenzia alcuna ROM, resta riusata
  // invariata, vedi bnj::start()). STESSA logica di btime::audio_read,
  // solo bnj_rom_audio al posto di btime_rom_audio.
  static uint8_t  audio_read(m6502_t *cpu, uint16_t addr);

  // --- rendering: griglia char 32x32 (STESSA di btime sull'asse
  // colonna/native_y, DIVERSA sull'asse riga/native_x perche' bnj non ha
  // crop -- visarea 256 piena, derivato matematicamente da C=272
  // (STESSA costante hardware-invariante usata per gli sprite, vedi
  // bnj.cpp) ---
  static constexpr short TILE_ROW_MIN = 3;
  static constexpr short TILE_ROW_MAX = 34;   // inclusive (32 valori, nessun crop)
  static constexpr short TILE_COL_OFFSET = 2; // invariato da btime (asse nativo_y, 240 in entrambi)
  static constexpr short TILE_ROW_XBASE  = 34; // invariato (C=272 condiviso con gli sprite)

  struct bnj_sprite_s {
    short x, y;
    unsigned char code;
    unsigned char flip_x, flip_y;
  };
  bnj_sprite_s spr_list[8];
  unsigned char spr_count = 0;

  // Sfondo scrollabile: lista di blocchi 16x16 visibili, ricalcolata ogni
  // frame in prepare_frame() a partire da bnj_backgroundram + scroll.
  // bg_enabled = (bnj_scroll[0]!=0) -- attiva/disattiva trasparenza char
  // e la doppia passata priority (vedi render_row/blit_tile).
  bool bg_enabled = false;
  struct bnj_bgtile_s { short x, y; unsigned char code; };
  static constexpr int BG_LIST_MAX = 512; // al massimo tutte le 512 celle
  bnj_bgtile_s bg_list[BG_LIST_MAX];
  int bg_count = 0;
  void blit_bg_tile(short row, int idx);

  // Filtro priorita' per blit_tile (-1=nessun filtro, 0/1=solo quel bit di
  // code>>7) -- impostato da render_row prima di ciascuna passata char
  // quando bg_enabled (draw_chars priority 1 poi 0, vedi screen_update_bnj).
  // blit_tile() e' un override (firma fissa da machineBase), quindi il
  // parametro priorita' passa per questo campo di stato invece che per
  // argomento.
  signed char tile_priority_filter = -1;
};

#endif
