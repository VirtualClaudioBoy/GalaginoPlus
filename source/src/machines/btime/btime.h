#ifndef BTIME_H
#define BTIME_H

#include "btime_rom_main.h"
#include "btime_rom_audio.h"
#include "btime_chartiles.h"
#include "btime_spritetiles.h"
#include "btime_bgtiles.h"
#include "btime_bgmap.h"
#include "btime_logo.h"
#include "btime_dipswitches.h"
#include "../../cpus/m6502/m6502.h"
#include "../machineBase.h"

// ============================================================
// Burger Time (Data East 1982, set "btime" / Data East set 1) memory map
// (da E:\Download\btime.cpp, letto per intero — NON mame4all)
//
// Main CPU (DECO CPU-7, 6502 cifrato @ 12MHz/2/2/2 = 1.5MHz):
//   0x0000-0x07ff: work RAM
//   0x0c00-0x0c0f: palette RAM (16 byte, formato BGR_233_inverted)
//   0x1000-0x13ff: videoram (tile code, 32x32 grid)
//   0x1400-0x17ff: colorram (bit0-1 = bank tile 256-1023, resto ignorato)
//   0x1800-0x1bff: mirror videoram (X/Y scambiati)
//   0x1c00-0x1fff: mirror colorram (X/Y scambiati)
//   0x4000: P1 (r)
//   0x4001: P2 (r)
//   0x4002: SYSTEM (r) / video_control_w: bit0=flip_screen (w)
//   0x4003: DSW1 (r) / soundlatch (w)
//   0x4004: DSW2 (r) / scroll[0]+tilemap select (w)
//   0xc000-0xffff: ROM cifrata CPU-7 (0xb000-0xbfff non popolato in questo set)
//
// Audio CPU (6502 nudo @ 12MHz/2/2/3/2 = 500KHz):
//   0x0000-0x1fff: RAM (mirror ogni 0x400)
//   0x2000-0x3fff: AY1 data_w
//   0x4000-0x5fff: AY1 address_w
//   0x6000-0x7fff: AY2 data_w
//   0x8000-0x9fff: AY2 address_w
//   0xa000-0xbfff: soundlatch (r)
//   0xc000-0xdfff: audio_nmi_enable_w bit0
//   0xe000-0xffff: ROM (0xe000-0xefff fisica, mirror 0x1000)
//
// Nessun interrupt periodico sul main CPU (a differenza di quasi tutti gli
// altri giochi del progetto): IRQ scatta SOLO all'inserimento moneta
// (coin_inserted_irq_hi, fronte di salita, HOLD_LINE). Il gameplay gira
// per puro polling, pacing dato solo dal budget di cicli CPU per frame.
// ============================================================

class btime : public machineBase
{
public:
  btime() { memset(&cpu_main, 0, sizeof(cpu_main)); memset(&cpu_audio, 0, sizeof(cpu_audio)); }
  ~btime() {}

  signed char machineType() override { return MCH_BTIME; }
  void start() override;
  void reset() override;

  void run_frame(void) override;
  void prepare_frame(void) override;
  void render_row(short row) override;
  const unsigned short *logo(void) override;

  // High score: da MAME hiscore.dat (voce condivisa btime/btime2/btime3/
  // btimem), unica regione 0x0033-0x0059 in work RAM pura (nessun side
  // effect in main_read/main_write per addr<0x0800). Il main CPU e' un
  // m6502 dedicato (non lo Z80 di machineBase::cpu[]), quindi il default
  // hiscoreRead/Write (rdZ80/wrZ80) non e' applicabile -- override diretto
  // su memory[] (WORK_RAM_OFFSET==0).
  const char *hiscoreKey() override { return "btime"; }
  const hiscore_region_S *hiscoreRegions(unsigned char *count) override;
  unsigned char hiscoreRead(unsigned short addr) override { return memory[WORK_RAM_OFFSET + addr]; }
  void hiscoreWrite(unsigned short addr, unsigned char value) override { memory[WORK_RAM_OFFSET + addr] = value; }

protected:
  void blit_tile(short row, char col) override;
  void blit_sprite(short row, unsigned char s) override;

  // --- memoria: layout dentro machineBase::memory --- (protected: STESSA
  // dimensione fisica di RAM/videoram/colorram/audioram su bnj, che le
  // riusa direttamente ereditando questi offset/puntatori -- vedi bnj.h)
  static constexpr unsigned short WORK_RAM_ADDR  = 0x0000;
  static constexpr unsigned short WORK_RAM_SIZE  = 0x0800;
  static constexpr unsigned short VIDEORAM_ADDR  = 0x1000;
  static constexpr unsigned short VIDEORAM_SIZE  = 0x0400;
  static constexpr unsigned short COLORRAM_ADDR  = 0x1400;
  static constexpr unsigned short COLORRAM_SIZE  = 0x0400;
  static constexpr unsigned short AUDIORAM_SIZE  = 0x0400; // RAM CPU audio (1KB fisici, mirror ogni 0x400 in 0x0000-0x1fff)

  static constexpr unsigned short WORK_RAM_OFFSET  = 0x0000;
  static constexpr unsigned short VIDEORAM_OFFSET  = WORK_RAM_OFFSET + WORK_RAM_SIZE;
  static constexpr unsigned short COLORRAM_OFFSET  = VIDEORAM_OFFSET + VIDEORAM_SIZE;
  static constexpr unsigned short AUDIORAM_OFFSET  = COLORRAM_OFFSET + COLORRAM_SIZE;
  static constexpr unsigned short MEM_FREE         = AUDIORAM_OFFSET + AUDIORAM_SIZE;
  static_assert(MEM_FREE <= RAMSIZE, "RAMSIZE too low for btime");

  unsigned char *work_ram;
  unsigned char *video_ram;
  unsigned char *color_ram;
  unsigned char *audio_ram;

  // --- palette RAM dinamica (0x0c00-0x0c0f per btime, 0x5c00-0x5c0f per
  // bnj -- STESSO formato BGR_233_inverted, PALETTE(config) ereditata
  // invariata da bnj(), vedi machine_config in btime.cpp) ---
  unsigned short palette[16];
  void palette_write(unsigned char index, unsigned char value);

  // --- stato macchina (condiviso, riusato da bnj) ---
  unsigned char flip_screen = 0;
  unsigned char sound_latch = 0;
  unsigned char bnj_scroll[2] = {0, 0};   // 0x4004 (scroll[0]) su btime / 0x5400+0x5800 su bnj
  unsigned char audio_nmi_enable = 0;     // scrittura diretta 0xc000-0xdfff bit0
  unsigned char audio_nmi_scanline_bit = 0;
  bool coin_prev = false;
  // bit7 di 0x4003 (btime) / 0x1000 (bnj) = segnale VBLANK reale
  // (PORT_READ_LINE_DEVICE_MEMBER screen::vblank in btime.cpp) -- il boot
  // fa polling attivo su questo bit (LDA/BPL loop, trovato col harness
  // Python offline) quindi DEVE alternarsi. IMPORTANTE (bring-up #2): il
  // gioco lo usa anche come riferimento di TEMPO durante il gameplay
  // normale, non solo al boot -- un primo tentativo che lo faceva
  // alternare ogni N LETTURE (invece che per FRAME reale) faceva avanzare
  // la logica di gioco a velocita' legata alla frequenza di polling della
  // CPU, non al tempo reale (velocita' "assolutamente eccessiva"
  // confermata su HW). Ora e' un semplice toggle UNA VOLTA per
  // run_frame() (~60Hz reale), costante per tutta la durata di un frame.
  unsigned char vblank_bit = 0;

  // --- CPU 6502 principale (btime: DECO CPU-7 cifrata dinamica; bnj:
  // DECO C10707 cifrata statica -- read/write/fetch riassegnati diversi
  // per ciascuna sottoclasse, il campo e' condiviso) ---
  m6502_t cpu_main;

  // --- CPU 6502 audio (non cifrata) + 2x AY-3-8910 (soundregs[], vedi
  // audio.cpp) -- audio_map IDENTICA byte-per-byte tra btime e bnj (vedi
  // btime.cpp: bnj() eredita m_audiocpu/audio_map da btime() senza
  // sovrascriverli), quindi bnj RIUSA audio_read/audio_write invariati ---
  m6502_t cpu_audio;
  unsigned char ay_port[2] = {0, 0};
  static uint8_t  audio_read (m6502_t *cpu, uint16_t addr);
  static void     audio_write(m6502_t *cpu, uint16_t addr, uint8_t val);

private:
  unsigned char btime_tilemap[4] = {0, 0, 0, 0};

  // --- CPU 6502 principale: fetch cifrato CPU-7 (SOLO Burger Time, bnj
  // usa un fetch statico diverso, vedi bnj.h) ---
  bool cpu7_had_written = false;
  static uint8_t  main_read (m6502_t *cpu, uint16_t addr);
  static void     main_write(m6502_t *cpu, uint16_t addr, uint8_t val);
  static uint8_t  main_fetch(m6502_t *cpu, uint16_t addr);

  // --- rendering (geometria specifica di Burger Time, NON riusata da bnj
  // che ha risoluzione nativa diversa 256x240 e un motore di sfondo
  // scorrevole completamente diverso) ---
  // Mapping ROT270 nativo->portrait derivato (vedi memoria progetto):
  // griglia tile nativa 32x32; colonna portrait = y_tile-2 (0..27);
  // riga portrait (bande da 8px, 0..35) valida per riga in [4,33],
  // x_tile = 34-riga. Sprite: posizione pixel-precisa,
  // portrait_x = native_y-16, portrait_y = 272-native_x.
  static constexpr short TILE_ROW_MIN = 4;
  static constexpr short TILE_ROW_MAX = 33;   // inclusive
  static constexpr short TILE_COL_OFFSET = 2; // native y_tile = col + 2
  static constexpr short TILE_ROW_XBASE  = 34; // native x_tile = 34 - row

  struct btime_sprite_s {
    short x, y;          // corner in portrait space (top-left del blocco 16x16 gia' ruotato)
    unsigned char code;
    unsigned char flip_x, flip_y;
  };
  btime_sprite_s spr_list[8];
  unsigned char spr_count = 0;

  // Layer di sfondo (draw_background in btime.cpp originale: piattaforme +
  // scale, attivo quando bnj_scroll[0]&0x10 -- ESSENZIALE per il gameplay,
  // non solo per schermate speciali). Blocchi 16x16 OPACHI, colore base 8.
  bool bg_enabled = false;
  struct btime_bgtile_s { short x, y; unsigned char code; };
  static constexpr int BG_LIST_MAX = 320;
  btime_bgtile_s bg_list[BG_LIST_MAX];
  int bg_count = 0;
  void blit_bg_tile(short row, int idx);
};

#endif
