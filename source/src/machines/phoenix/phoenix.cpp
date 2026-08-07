// ============================================================================
// galagino - machines/phoenix/phoenix.cpp
//
// Phoenix (Amstar/Centuri 1980, set "phoenix" base MAME).
// CPU Z80 + 2-layer tilemap (FG+BG con scroll BG) + palette PROM 256 colori.
// NESSUN IRQ vblank: il game polla bit 7 di 0x7800 (DSW0) per sincronizzazione.
// Audio TMS3617 custom approssimato in Audio::phoenix_render_buffer() (emulation/audio.cpp).
//
// Monitor VERTICALE (ROT90). Arcade landscape MAME 256x208 (32 col x 26 row);
// reso qui in portrait 208x256 dentro il framebuffer galagino 224x288, con la
// rotazione 90 gradi cablata nella matematica dei tile (come galaxian).
// ============================================================================
#include "phoenix.h"

#define FB_W        224     // larghezza framebuffer galagino
#define PX_OFFSET   8       // (224-208)/2, centratura orizzontale portrait
#define ARCADE_COLS 26      // tile per riga portrait (208 px)

Phoenix::Phoenix()
  : videoreg(0), scroll_x(0), palette_bank(0),
    bg_decoded(nullptr), fg_decoded(nullptr),
    palette_cache(nullptr), cache_done(false) {
  memset(vram, 0, sizeof(vram));
}

// Pre-decode 256 tile x 8 row x 8 col -> pen 2-bit (1 byte) in orientamento
// NATIVO: out[code*64 + row*8 + col]. La rotazione avviene in render_row.
static void decode_tile_pens(const unsigned char *gfx, unsigned char *out) {
  for (int code = 0; code < 256; code++) {
    for (int row = 0; row < 8; row++) {
      unsigned char p0 = pgm_read_byte(&gfx[code * 8 + row]);
      unsigned char p1 = pgm_read_byte(&gfx[code * 8 + row + 0x800]);
      for (int col = 0; col < 8; col++) {
        out[(code << 6) | (row << 3) | col] =
          ((p0 >> col) & 1) | (((p1 >> col) & 1) << 1);
      }
    }
  }
}

void Phoenix::init(Input *in, unsigned short *fb,
                   sprite_S *sb, unsigned char *mem) {
  machineBase::init(in, fb, sb, mem);
  // Alloc cache spostata a reset(): init() e' chiamata al boot per TUTTE le
  // machine, allocare qui sprecherebbe DRAM interna a chi non usa Phoenix.
}

void Phoenix::reset() {
  machineBase::reset();

  // Lazy alloc: ~32.5 KB DRAM interna, solo al primo avvio di Phoenix.
  if (!cache_done) {
    const uint32_t CAPS = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
    bg_decoded    = (unsigned char*)  heap_caps_malloc(16384, CAPS);
    fg_decoded    = (unsigned char*)  heap_caps_malloc(16384, CAPS);
    palette_cache = (unsigned short*) heap_caps_malloc(256 * sizeof(unsigned short), CAPS);
    if (bg_decoded && fg_decoded && palette_cache) {
      decode_tile_pens(phoenix_bgtiles, bg_decoded);
      decode_tile_pens(phoenix_fgtiles, fg_decoded);
      for (int i = 0; i < 256; i++)
        palette_cache[i] = pgm_read_word(&phoenix_palette[i]);
      cache_done = true;
      Serial.println(F("[PHOENIX] tiles/palette cached in DRAM (~32.5 KB)"));
    } else {
      Serial.println(F("[PHOENIX] cache alloc FAILED"));
    }
  }

  memset(vram, 0, sizeof(vram));
  videoreg = 0;
  scroll_x = 0;
  palette_bank = 0;
  vblank_active = false;
}

const unsigned short *Phoenix::logo(void) {
  return phoenix_logo;
}

// High score: da MAME hiscore.dat, set "phoenix" (voce condivisa da tutto il
// gruppo avefenix/.../phoenix/.../vautour). 3 gruppi da 6 cifre BCD-tile: ogni
// cifra e' una cella FG separata (stride -0x20, blank=0x20), precedute da un
// "gate" di 3 byte a 0 (non distingue init da salvato, ma il pattern combinato
// con le 18 celle video e' comunque un gate solido). Nessun override di
// hiscoreRead/Write necessario (vedi commento in phoenix.h).
const hiscore_region_S *Phoenix::hiscoreRegions(unsigned char *count) {
  static const hiscore_region_S regions[] = {
    { 0x4389, 3, 0x00, 0x00 },
    { 0x41e1, 1, 0x20, 0x20 },
    { 0x41c1, 1, 0x20, 0x20 },
    { 0x41a1, 1, 0x20, 0x20 },
    { 0x4181, 1, 0x20, 0x20 },
    { 0x4161, 1, 0x20, 0x20 },
    { 0x4141, 1, 0x20, 0x20 },
    { 0x4381, 3, 0x00, 0x00 },
    { 0x4301, 1, 0x20, 0x20 },
    { 0x42e1, 1, 0x20, 0x20 },
    { 0x42c1, 1, 0x20, 0x20 },
    { 0x42a1, 1, 0x20, 0x20 },
    { 0x4281, 1, 0x20, 0x20 },
    { 0x4261, 1, 0x20, 0x20 },
    { 0x4385, 3, 0x00, 0x00 },
    { 0x40c1, 1, 0x20, 0x20 },
    { 0x40a1, 1, 0x20, 0x20 },
    { 0x4081, 1, 0x20, 0x20 },
    { 0x4061, 1, 0x20, 0x20 },
    { 0x4041, 1, 0x20, 0x20 },
    { 0x4021, 1, 0x20, 0x20 },
  };
  *count = sizeof(regions) / sizeof(regions[0]);
  return regions;
}

// ── Z80 op fetch ──
unsigned char Phoenix::opZ80(unsigned short Addr) {
  return rdZ80(Addr);
}

// ── Z80 memory read ──
unsigned char Phoenix::rdZ80(unsigned short Addr) {
  // ROM 0x0000-0x3FFF (PROGMEM, flash cache mappata su ESP32)
  if (Addr < 0x4000) return pgm_read_byte(&phoenix_rom[Addr]);

  // VRAM 0x4000-0x4FFF (page corrente)
  if (Addr <= 0x4FFF) {
    return vram[videoreg & 0x01][Addr & 0x0FFF];
  }

  // I/O area 0x5000-0x6FFF: write-only (read open bus)
  if (Addr <= 0x6FFF) return 0xFF;

  // IN0 read 0x7000-0x77FF — ACTIVE LOW (idle=1, pressed=0), verificato su
  // MAME phoenix_v.cpp player_input_r(). Mapping bit determinato su cabinet:
  // bit0 COIN, bit1 START, bit4 FIRE, bit5 RIGHT, bit6 LEFT, bit7 BARRIER.
  if (Addr <= 0x77FF) {
    unsigned char b = input ? input->buttons_get() : 0;
    unsigned char v = 0xFF;
    if (b & BUTTON_COIN)  v &= ~0x01;   // bit 0 COIN1
    if (b & BUTTON_START) v &= ~0x02;   // bit 1 START1
    if (b & BUTTON_FIRE)  v &= ~0x10;   // bit 4 SPARO
    // NB: se su HW LEFT/RIGHT risultano invertiti, scambia bit5/bit6.
    if (b & BUTTON_LEFT)  v &= ~0x40;   // bit 6 LEFT (MAME)
    if (b & BUTTON_RIGHT) v &= ~0x20;   // bit 5 RIGHT (MAME)
    if (b & BUTTON_EXTRA) v &= ~0x80;   // bit 7 SCUDO/BARRIER
    return v;
  }

  // DSW0 read 0x7800-0x7FFF + VBLANK live (bit 7 ACTIVE LOW).
  unsigned char vblank_bit = vblank_active ? 0x00 : 0x80;
  return (PHOENIX_DSW0 & 0x7F) | vblank_bit;
}

// ── Z80 memory write ──
void Phoenix::wrZ80(unsigned short Addr, unsigned char Value) {
  if (Addr < 0x4000) return;                 // ROM: ignora

  // VRAM 0x4000-0x4FFF (page corrente)
  if (Addr <= 0x4FFF) {
    vram[videoreg & 0x01][Addr & 0x0FFF] = Value;
    if (!game_started) game_started = 1;
    return;
  }

  // videoreg_w 0x5000-0x57FF (bit0 page sel, bit1 palette bank)
  if (Addr <= 0x57FF) {
    videoreg = Value;
    palette_bank = (Value >> 1) & 0x01;
    return;
  }

  // scroll_w 0x5800-0x5FFF
  if (Addr <= 0x5FFF) {
    scroll_x = Value;
    return;
  }

  // sound A latch 0x6000-0x67FF (effect 2: shoot/wing/swoop, letto da Audio::phoenix_render_buffer)
  if (Addr <= 0x67FF) {
    soundregs[0] = Value;
    return;
  }

  // sound B latch 0x6800-0x6FFF (effect 1: noise/HIT + melody select)
  soundregs[1] = Value;
}

// ── Frame loop ──
// VBLANK pilotato in 2 fasi dentro run_frame:
//   - fase display: vblank_active=false (bit 7 = 1), ~84% dei loop
//   - fase vblank:  vblank_active=true  (bit 7 = 0), ~16% dei loop
// Garantisce 1 transizione 1->0 per frame, vista dal polling Z80.
#define PHOENIX_LOOPS_PER_FRAME  2500
#define PHOENIX_DISPLAY_PHASE    2100

void Phoenix::run_frame() {
  current_cpu = 0;
  vblank_active = false;
  for (int i = 0; i < PHOENIX_DISPLAY_PHASE; i++) {
    StepZ80(&cpu[0]); StepZ80(&cpu[0]); StepZ80(&cpu[0]); StepZ80(&cpu[0]);
  }
  vblank_active = true;
  for (int i = PHOENIX_DISPLAY_PHASE; i < PHOENIX_LOOPS_PER_FRAME; i++) {
    StepZ80(&cpu[0]); StepZ80(&cpu[0]); StepZ80(&cpu[0]); StepZ80(&cpu[0]);
  }
  // NESSUN IRQ — Phoenix usa solo polling VBLANK (vedi rdZ80 0x7800)
}

void Phoenix::prepare_frame() {
  // Niente sprite hardware, niente da preparare.
}

// ============================================================================
// Render portrait 208x256 (rotazione ROT90 cablata).
//
// Mapping pixel portrait (px,py) -> arcade landscape MAME (fx,fy):
//   fx = py            (asse verticale portrait = asse 256 orizzontale MAME)
//   fy = 207 - px      (asse orizzontale portrait = asse 208 verticale MAME)
// In tile:  tx = fx>>3 (= colonna arcade), ty = fy>>3 (= riga arcade),
//           col nativa = fx&7, row nativa = fy&7.
// Portrait: pcol 0..25 (px = PX_OFFSET + pcol*8 + rx)  ->  ty = 25 - pcol
//           prow 0..31 (galagino row 2..33)            ->  tx dipende da fx
//           rx 0..7  ->  row nativa ly = 7 - rx
//
// BG scroll: scroll_x scrolla fx (orizzontale MAME) = verticale portrait.
// FG: nessuno scroll, pen 0 trasparente.
//
// NOTE HW (senso rotazione da verificare/commutare):
//   - immagine specchiata sull'asse largo 208  -> usare ty = pcol, ly = rx
//   - immagine capovolta  sull'asse alto  256  -> usare tx = 31 - prow, lx = 7-(fx&7)
//   - direzione scroll     -> (py - scroll_x) invece di (py + scroll_x)
// ============================================================================
void Phoenix::render_row(short row) {
  if (row < 2 || row > 33) return;      // fuori finestra (fb gia' azzerato)
  if (!cache_done) return;              // alloc cache fallita (OOM): niente da rendere

  unsigned char idx = videoreg & 0x01;
  unsigned char *vp = vram[idx];
  int prow = row - 2;                    // 0..31 = colonna arcade tx (FG)

  for (int ry = 0; ry < 8; ry++) {
    unsigned short *line = frame_buffer + ry * FB_W + PX_OFFSET;
    int py = (prow << 3) + ry;           // portrait Y globale (0..255) = fx MAME

    // ─── BG layer (opaco, scroll verticale) ───
    int fx = (py + scroll_x) & 0xFF;
    int bg_tx = fx >> 3;
    int bg_lx = fx & 7;
    for (int pcol = 0; pcol < ARCADE_COLS; pcol++) {
      int ty = 25 - pcol;
      unsigned char code = vp[(ty << 5) + bg_tx + 0x800];
      unsigned char col  = ((code >> 5) & 0x07) | (palette_bank << 4);
      const unsigned short *pal  = &palette_cache[col << 2];
      const unsigned char  *pens = &bg_decoded[(code << 6) + bg_lx];
      unsigned short *p = line + (pcol << 3);
      // rx 0..7 -> ly = 7-rx ; pen = pens[ly*8]
      p[0] = pal[pens[7 << 3]]; p[1] = pal[pens[6 << 3]];
      p[2] = pal[pens[5 << 3]]; p[3] = pal[pens[4 << 3]];
      p[4] = pal[pens[3 << 3]]; p[5] = pal[pens[2 << 3]];
      p[6] = pal[pens[1 << 3]]; p[7] = pal[pens[0]];
    }

    // ─── FG layer (overlay, pen 0 trasparente, no scroll) ───
    int fg_lx = ry;                      // fx = py -> lx = py&7 = ry ; tx = prow
    for (int pcol = 0; pcol < ARCADE_COLS; pcol++) {
      int ty = 25 - pcol;
      unsigned char code = vp[(ty << 5) + prow];
      unsigned char col  = ((code >> 5) & 0x07) | 0x08 | (palette_bank << 4);
      const unsigned short *pal  = &palette_cache[col << 2];
      const unsigned char  *pens = &fg_decoded[(code << 6) + fg_lx];
      unsigned short *p = line + (pcol << 3);
      unsigned char pen;
      pen = pens[7 << 3]; if (pen) p[0] = pal[pen];
      pen = pens[6 << 3]; if (pen) p[1] = pal[pen];
      pen = pens[5 << 3]; if (pen) p[2] = pal[pen];
      pen = pens[4 << 3]; if (pen) p[3] = pal[pen];
      pen = pens[3 << 3]; if (pen) p[4] = pal[pen];
      pen = pens[2 << 3]; if (pen) p[5] = pal[pen];
      pen = pens[1 << 3]; if (pen) p[6] = pal[pen];
      pen = pens[0];      if (pen) p[7] = pal[pen];
    }
  }
}
