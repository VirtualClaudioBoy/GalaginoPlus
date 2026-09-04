#ifndef PBACTION_H
#define PBACTION_H

#include "pbaction_main_rom.h"
#include "pbaction_audio_rom.h"
#include "pbaction_fg_tiles.h"
#include "pbaction_bg_tiles.h"
#include "pbaction_sprites16.h"
#include "pbaction_sprites32.h"
#include "pbaction_dipswitches.h"
#include "pbaction_logo.h"
#include "../tileaddr.h"
#include "../machineBase.h"

// ---------------------------------------------------------------------------
// Pinball Action (Tehkan, 1985) - MAME `pbaction` set 1.
//
// Hardware (tecmo/pbaction.cpp):
//   main CPU   Z80 @ 4 MHz          NMI on vblank, gated by nmi_mask (0xe600)
//   audio CPU  Z80 @ 3 MHz (12/4)   Z80CTC daisy chain, IM2
//              ctc ch0 -> vector 0x00  (ISR 0x0108: read sound latch @0x8000)
//              ctc ch1 -> vector 0x02  (ISR 0x0113: periodic music tick)
//   sound      3x AY-3-8910 @ 1.5 MHz, I/O mapped 0x10/0x20/0x30
//   video      2x 8x8 tilemap (bg opaque + fg transparent), 32x32, SCAN_ROWS
//              256 sprites 16x16 + 32 sprites 32x32
//              256-entry xBGR_444 palette RAM at 0xe400
//   screen     ROT90.  MAME 256x256, visarea x 0..255 (32 cols) y 16..239
//              (28 rows).  Rotated -> port renders 224 wide x 256 tall:
//              render_row() row 2..33 == MAME tile column 0..31,
//              col 0..27 == MAME tile row 2..29  (tileaddr[row][col]).
// ---------------------------------------------------------------------------

// memory[] layout (the shared RAM buffer)
#define PBACTION_WORK_RAM     0x0000  // 0xc000-0xcfff  main work RAM (0x1000)
#define PBACTION_FG_VRAM      0x1000  // 0xd000-0xd3ff  fg tile codes  (0x400)
#define PBACTION_FG_CRAM      0x1400  // 0xd400-0xd7ff  fg attributes  (0x400)
#define PBACTION_BG_VRAM      0x1800  // 0xd800-0xdbff  bg tile codes  (0x400)
#define PBACTION_BG_CRAM      0x1C00  // 0xdc00-0xdfff  bg attributes  (0x400)
#define PBACTION_SPRITE_RAM   0x2000  // 0xe000-0xe07f  32 x 4 bytes    (0x80)
#define PBACTION_PALETTE_RAM  0x2080  // 0xe400-0xe5ff  256 x 2 bytes  (0x200)
#define PBACTION_AUDIO_RAM    0x2280  // 0x4000-0x47ff  audio work RAM (0x800)

// main-CPU I/O ports (read/write share the address, see main_map)
#define PBACTION_P1           0xe600  // R: IN0    W: nmi_mask
#define PBACTION_P2           0xe601  // R: IN1
#define PBACTION_SYSTEM       0xe602  // R: IN2 (coin/start)
#define PBACTION_DSW1_PORT    0xe604  // R: DSW1   W: flip screen
#define PBACTION_DSW2_PORT    0xe605  // R: DSW2
#define PBACTION_WATCHDOG     0xe606  // R: watchdog   W: bg/fg scroll
#define PBACTION_SOUND_CMD    0xe800  // W: sound command latch

class pbaction : public machineBase
{
public:
  pbaction() { }
  ~pbaction() { }

  void reset() override;
  signed char machineType() override { return MCH_PBACTION; }
  signed char useVideoHalfRate() override { return 1; }

  unsigned char rdZ80(unsigned short Addr) override;
     void wrZ80(unsigned short Addr, unsigned char Value) override;
  bool isGameRunning() const;
  // attract interno (demo/musiche dopo il game over o prima del via):
  // bit0 di 0xC000=0 -> partita non in corso -> audio silenziato
  bool audioAttractMute() override { return !isGameRunning(); }
  unsigned char opZ80(unsigned short Addr) override;

  unsigned char inZ80(unsigned short Port) override;
  void outZ80(unsigned short Port, unsigned char Value) override;
  void run_frame(void) override;
  void prepare_frame(void) override;
  void render_row(short row) override;
  const unsigned short *logo(void) override;

  // Persistent high score (NVS).  current_cpu is forced back to 0 at the end
  // of run_frame, so the default rdZ80/wrZ80 dispatch is coherent for the
  // main work RAM - no override needed beyond the key + regions.
  const char *hiscoreKey() override { return "pbaction"; }
  const hiscore_region_S *hiscoreRegions(unsigned char *count) override;
  unsigned char hiscoreRead(unsigned short addr) override {
    return memory[PBACTION_WORK_RAM + (addr - 0xc000)];
  }
  void hiscoreWrite(unsigned short addr, unsigned char value) override {
    memory[PBACTION_WORK_RAM + (addr - 0xc000)] = value;
  }

#ifdef LED_PIN
  void menuLeds(CRGB *leds) override;
  void gameLeds(CRGB *leds) override;
#endif

private:
  void blit_tile_bg(short row, char col);
  void blit_tile_fg(short row, char col);
  void blit_sprite(short row, unsigned char s_idx);
  void palette_write(unsigned short offset, unsigned char value);
  void scan_sprites(void);

  uint16_t pbaction_palette[256];

  // With useVideoHalfRate() the screen renders in two halves and the emulation
  // task advances a frame between them, so the tilemap is read at two different
  // ages across the screen.  To keep the fast-moving ball sprite consistent
  // with the tilemap it sits over, the sprite list is re-scanned live at the
  // start of each half (render_row row 2 and row 18) rather than snapshotted
  // once in prepare_frame() - otherwise the ball lags the bumper/bonus counter
  // it just hit ("flipper misses a well-timed ball").

  // video state
  short m_scroll = 0;        // shared bg+fg scroll (MAME x axis == port Y)
  bool  m_flip = false;      // flip screen (0xe604 write)
  bool  m_nmi_mask = false;  // main-CPU vblank NMI enable (0xe600 write)

  // sound
  unsigned char sound_latch = 0;
  unsigned char sound_cmd_pending = 0;   // ctc ch0 (vector 0x00) trigger
  unsigned char ay_address[3] = { 0, 0, 0 };

  // coin debounce (IN2 is IP_ACTIVE_HIGH; pulse it a few frames)
  unsigned char coinBackup = 0;
  unsigned char coinFrameCounter = 0;

#ifdef LED_PIN
  const CRGB menu_leds[7] = { LED_RED, LED_YELLOW, LED_RED, LED_WHITE, LED_RED, LED_YELLOW, LED_RED };
#endif
};

#endif
