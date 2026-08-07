#include "bnj.h"

// ---------------------------------------------------------------------------
// DECO C10707 decrypt (deco222.cpp, letto per intero): bitswap STATICO e
// INCONDIZIONATO su OGNI fetch di opcode, nessuno stato, nessuna
// dipendenza da indirizzo -- bitswap<8>(v,7,5,6,4,3,2,1,0) = scambio
// bit5<->bit6, resto invariato. Molto piu' semplice della CPU-7 dinamica
// di Burger Time.
// ---------------------------------------------------------------------------
static inline uint8_t deco_c10707_bitswap(uint8_t v) {
  return (uint8_t)((v & 0x9F) | ((v & 0x20) << 1) | ((v & 0x40) >> 1));
}

static inline unsigned short xy_swap(unsigned short local_offset) {
  unsigned char x = local_offset / 32;
  unsigned char y = local_offset % 32;
  return 32 * y + x;
}

void bnj::start() {
  work_ram  = memory + WORK_RAM_OFFSET;
  video_ram = memory + VIDEORAM_OFFSET;
  color_ram = memory + COLORRAM_OFFSET;
  audio_ram = memory + AUDIORAM_OFFSET;
  bg_ram    = memory + BGRAM_OFFSET;
  memset(memory, 0, MEM_FREE_BNJ);
  memset(palette, 0, sizeof(palette));

  cpu_main.read  = main_read;
  cpu_main.write = main_write;
  cpu_main.fetch = main_fetch;
  cpu_main.user  = this;
  m6502_reset(&cpu_main);

  // Audio IDENTICO a Burger Time (audio_map condivisa byte-per-byte, vedi
  // btime.cpp: bnj() eredita m_audiocpu da btime() senza sovrascriverlo) --
  // audio_write e' generica (nessun riferimento a ROM), riusata invariata;
  // audio_read invece va ridefinita (bnj_rom_audio, non btime_rom_audio).
  cpu_audio.read  = bnj::audio_read;
  cpu_audio.write = btime::audio_write;
  cpu_audio.fetch = 0; // non cifrata: fetch == read
  cpu_audio.user  = this;
  m6502_reset(&cpu_audio);
}

void bnj::reset() {
  machineBase::reset();
  work_ram  = memory + WORK_RAM_OFFSET;
  video_ram = memory + VIDEORAM_OFFSET;
  color_ram = memory + COLORRAM_OFFSET;
  audio_ram = memory + AUDIORAM_OFFSET;
  bg_ram    = memory + BGRAM_OFFSET;

  memset(palette, 0, sizeof(palette));
  flip_screen = 0;
  sound_latch = 0;
  bnj_scroll[0] = bnj_scroll[1] = 0;
  audio_nmi_enable = 0;
  audio_nmi_scanline_bit = 0;
  coin_prev = false;
  ay_port[0] = ay_port[1] = 0;
  vblank_bit = 0;
  tile_priority_filter = -1;

  // emulation_start() chiama reset() PRIMA di start() (vedi btime.cpp per
  // la stessa lezione gia' imparata sul bring-up #1 di Burger Time).
  if (cpu_main.read)  m6502_reset(&cpu_main);
  if (cpu_audio.read) m6502_reset(&cpu_audio);
}

// ---------------------------------------------------------------------------
// Main CPU (DECO C10707)
// ---------------------------------------------------------------------------
uint8_t bnj::main_read(m6502_t *cpu, uint16_t addr) {
  bnj *s = (bnj*)cpu->user;

  // ROM controllata per PRIMA (stesso motivo/fix performance di Burger Time:
  // e' il path piu' frequente ad ogni singola istruzione).
  if (addr >= 0xa000) return bnj_rom_main[addr - 0xa000];

  if (addr < 0x0800) return s->work_ram[addr];

  if (addr == 0x1000) return (unsigned char)(BNJ_DSW1 | (s->vblank_bit << 7));
  if (addr == 0x1001) return BNJ_DSW2;
  if (addr == 0x1002) { // P1
    unsigned char k = s->input->buttons_get();
    unsigned char r = 0xff;
    if (k & BUTTON_RIGHT) r &= ~0x01;
    if (k & BUTTON_LEFT)  r &= ~0x02;
    if (k & BUTTON_UP)    r &= ~0x04;
    if (k & BUTTON_DOWN)  r &= ~0x08;
    if (k & BUTTON_FIRE)  r &= ~0x10;
    return r;
  }
  if (addr == 0x1003) return 0xff; // P2 (cocktail, non usato)
  if (addr == 0x1004) { // SYSTEM: tilt/start ACTIVE_LOW, coin ACTIVE_LOW
    unsigned char k = s->input->buttons_get();
    unsigned char r = 0xff;
    if (k & BUTTON_START) r &= ~0x08;
    if (k & BUTTON_COIN)  r &= ~0x40;
    return r;
  }

  if (addr >= 0x4000 && addr < 0x4400) return s->video_ram[addr - 0x4000];
  if (addr >= 0x4400 && addr < 0x4800) return s->color_ram[addr - 0x4400];
  if (addr >= 0x4800 && addr < 0x4c00) return s->video_ram[xy_swap(addr - 0x4800)];
  if (addr >= 0x4c00 && addr < 0x5000) return s->color_ram[xy_swap(addr - 0x4c00)];
  if (addr >= 0x5000 && addr < 0x5400) return s->bg_ram[addr - 0x5000];

  return 0xff;
}

void bnj::main_write(m6502_t *cpu, uint16_t addr, uint8_t val) {
  bnj *s = (bnj*)cpu->user;

  if (addr < 0x0800) { s->work_ram[addr] = val; return; }

  // bnj_video_control_w: nel sorgente reale imposta flip_screen SOLO in
  // modalita' cocktail (DSW1 bit6) -- BNJ_DSW1 fissa bit6=0 (upright),
  // quindi questa scrittura e' sempre un no-op per noi (stesso schema di
  // btime_video_control_w mai attivato per lo stesso motivo).
  if (addr == 0x1001) return;

  if (addr == 0x1002) {
    s->sound_latch = val;
    // GENERIC_LATCH_8 data_pending_callback -> IRQ CPU audio, IDENTICO a
    // Burger Time (stessa infrastruttura audio, vedi btime.cpp).
    s->cpu_audio.irq = 1;
    return;
  }

  if (addr >= 0x4000 && addr < 0x4400) { s->video_ram[addr - 0x4000] = val; return; }
  if (addr >= 0x4400 && addr < 0x4800) { s->color_ram[addr - 0x4400] = val; return; }
  if (addr >= 0x4800 && addr < 0x4c00) { s->video_ram[xy_swap(addr - 0x4800)] = val; return; }
  if (addr >= 0x4c00 && addr < 0x5000) { s->color_ram[xy_swap(addr - 0x4c00)] = val; return; }
  if (addr >= 0x5000 && addr < 0x5400) { s->bg_ram[addr - 0x5000] = val; return; }

  if (addr == 0x5400) { s->bnj_scroll[0] = val; return; }
  if (addr == 0x5800) { s->bnj_scroll[1] = val; return; }
  if (addr >= 0x5c00 && addr <= 0x5c0f) { s->palette_write((unsigned char)(addr - 0x5c00), val); return; }
}

uint8_t bnj::main_fetch(m6502_t *cpu, uint16_t addr) {
  return deco_c10707_bitswap(bnj::main_read(cpu, addr));
}

// ---------------------------------------------------------------------------
// Audio CPU: STESSA logica di btime::audio_read (RAM/soundlatch/IRQ-ack
// identici, audio_map condivisa byte-per-byte), ma bnj_rom_audio al posto
// di btime_rom_audio -- btime::audio_read referenzia quest'ultima
// DIRETTAMENTE (hardcoded), quindi non e' riusabile invariata per bnj
// (bug reale trovato su HW: "si sente la musica di Burger Time").
// ---------------------------------------------------------------------------
uint8_t bnj::audio_read(m6502_t *cpu, uint16_t addr) {
  bnj *s = (bnj*)cpu->user;
  if (addr < 0x2000) return s->audio_ram[addr & 0x3ff];
  if (addr >= 0xa000 && addr < 0xc000) {
    cpu->irq = 0; // ack-on-read (stesso schema di btime, non separate_acknowledge)
    return s->sound_latch;
  }
  if (addr >= 0xe000) return bnj_rom_audio[addr & 0x0fff];
  return 0xff;
}

// ---------------------------------------------------------------------------
// Frame execution
// ---------------------------------------------------------------------------
void bnj::run_frame(void) {
  // Main CPU: coin genera NMI (non IRQ come Burger Time), fronte di
  // salita, stesso stile edge-detect gia' confermato su HW per btime.
  unsigned char keymask = input->buttons_get();
  bool coin_now = (keymask & BUTTON_COIN) != 0;
  if (coin_now && !coin_prev) cpu_main.nmi = 1;
  coin_prev = coin_now;

  current_cpu = 0;
  // Budget cicli dimezzato rispetto a Burger Time: main CPU a 750KHz
  // invece di 1.5MHz (12MHz/2/2/2/2 vs 12MHz/2/2/2, vedi bnj() in
  // btime.cpp) -- 750000/60=12500 cicli/frame esatti, stessa proporzione
  // 88%/12% attivo/vblank gia' confermata su HW per btime (fix
  // performance bring-up #3, stesso motivo: un "wait for vblank" nel loop
  // principale deve vedere SEMPRE almeno una transizione per frame).
  vblank_bit = 0;
  m6502_exec(&cpu_main, 11000);
  vblank_bit = 1;
  m6502_exec(&cpu_main, 1500);
  cpu_main.nmi = 0; // one-shot: assunto servito entro il budget del frame

  // Audio CPU: IDENTICA a Burger Time (stessa clock 500KHz, stessa NMI
  // approssimata a 17 impulsi/frame, stesso IRQ-on-latch-write).
  current_cpu = 1;
  const int NMI_SEGS = 17;
  const int CYCLES_PER_SEG = 490;
  for (int i = 0; i < NMI_SEGS; i++) {
    m6502_exec(&cpu_audio, CYCLES_PER_SEG);
    if (audio_nmi_enable) cpu_audio.nmi = 1;
  }

  if (!game_started) game_started = 1;
}

// ---------------------------------------------------------------------------
// Video: raccolta sprite+sfondo (prepare_frame) e rendering a bande (render_row)
//
// Mapping ROT270 derivato (vedi memoria progetto project_btime.md sezione
// bnj): C=272 e' la STESSA costante hardware-invariante usata da Burger
// Time per gli sprite (deriva da "240-x_byte", formula FISSA di
// draw_sprites() condivisa tra tutti i giochi di questo driver,
// indipendente dalla visarea) -- riusata qui identica per sprite E char.
// Per lo sfondo (bnj-specifico, copyscrollbitmap su bitmap 512x256 "due
// volte piu' largo dello schermo"), la colonna di destinazione (0..255,
// asse nativo orizzontale = visarea PIENA per bnj, diversa da btime che
// croppava a 240) e' la STESSA coordinata bitmap grezza usata dagli
// sprite, quindi usa la STESSA costante C=272 per restare allineata.
// ---------------------------------------------------------------------------
void bnj::prepare_frame(void) {
  // --- sprite (draw_sprites condivisa con btime, sprite_y_adjust=0 per
  // bnj -- vedi screen_update_bnj: draw_sprites(...,0,0,0,m_videoram,0x20)) ---
  spr_count = 0;
  for (int i = 0; i < 8 && spr_count < 8; i++) {
    unsigned short offs = 128 * i;
    unsigned char attr = video_ram[offs + 0];
    if (!(attr & 0x01)) continue;

    unsigned char code   = video_ram[offs + 32];
    unsigned char y_byte = video_ram[offs + 64];
    unsigned char x_byte = video_ram[offs + 96];

    short native_x = 240 - x_byte;   // costante hardware fissa, NON legata a visarea
    short native_y = 240 - y_byte;   // sprite_y_adjust=0 per bnj: nessun "-1"

    short px = native_y - 16;
    short py = 272 - native_x;
    if (px <= -16 || px >= 224 || py <= -16 || py >= 288) continue;

    bnj_sprite_s &sp = spr_list[spr_count++];
    sp.x = px;
    sp.y = py;
    sp.code = code;
    sp.flip_x = (attr & 0x04) ? 1 : 0;
    sp.flip_y = (attr & 0x02) ? 1 : 0;
  }

  // --- sfondo scrollabile (screen_update_bnj + VIDEO_START_MEMBER(bnj)) ---
  bg_enabled = (bnj_scroll[0] != 0);
  bg_count = 0;
  if (bg_enabled) {
    // scroll (btime.cpp riga 854-856): flip_screen sempre 0 per noi
    // (upright, bnj_video_control_w e' un no-op -- vedi main_write), quindi
    // si prende sempre il ramo "767-scroll".
    int scroll = (int)(bnj_scroll[0] & 0x02) * 128 + 511 - (int)bnj_scroll[1];
    scroll = 767 - scroll;

    for (int offs = 0; offs < 0x200 && bg_count < BG_LIST_MAX; offs++) {
      // Griglia di riempimento (screen_update_bnj righe 834-850), ricavata
      // per intero dal sorgente: colonna/riga PRIMA della trasposizione
      // "sx=496-sx" (che inverte solo la colonna).
      int col_written = ((offs & 0x7f) / 8) + ((offs >= 0x100) ? 16 : 0); // 0..31
      int row_written = (offs & 7) + (((offs & 0xff) >= 0x80) ? 8 : 0);   // 0..15
      int final_sx = 496 - 16 * col_written; // posizione nel buffer 512-wide
      int sy = 16 * row_written;

      // Posizione sullo schermo (0..255) dopo lo scroll con wraparound
      // (copyscrollbitmap, sorgente 512 wide -> finestra 256 wide).
      // CALIBRAZIONE EMPIRICA (bring-up #1): il segno "final_sx - scroll"
      // derivato a tavolino risultava scorrere al CONTRARIO su HW reale --
      // invertito in "final_sx + scroll" (la semantica esatta di
      // copyscrollbitmap non era verificabile da un riferimento diretto,
      // solo dedotta -- vedi memoria progetto).
      int raw_x = final_sx + scroll;
      raw_x = ((raw_x % 512) + 512) % 512;
      if (raw_x > 255) continue; // fuori dalla finestra visibile

      short native_x = (short)raw_x;
      short native_y = (short)sy;

      short px = native_y - 16;
      short py = 272 - native_x;
      if (px <= -16 || px >= 224 || py <= -16 || py >= 288) continue;

      unsigned char code = (unsigned char)((bg_ram[offs] >> 4) + (((offs & 0x80) ? 1 : 0) << 4) + 32);
      bnj_bgtile_s &bt = bg_list[bg_count++];
      bt.x = px;
      bt.y = py;
      bt.code = code;
    }
  }
}

void bnj::blit_sprite(short row, unsigned char s) {
  const bnj_sprite_s &sp = spr_list[s];
  short band_y0 = row * 8;

  for (int ly = 0; ly < 16; ly++) {
    short py = sp.y + ly;
    if (py < band_y0 || py >= band_y0 + 8) continue;
    int local_row = py - band_y0;

    // 180 gradi + scambio flip_x<->asse verticale/flip_y<->asse orizzontale
    // (conseguenza diretta della trasformazione ROT270, STESSA regola
    // generale gia' derivata e confermata su HW per Burger Time -- vedi
    // memoria progetto, "da applicare DA SUBITO per ogni futuro gioco
    // ROT270 con sprite flippabili").
    int sy = sp.flip_x ? ly : (15 - ly);
    unsigned short *ptr = frame_buffer + local_row * 224;
    for (int lx = 0; lx < 16; lx++) {
      short px = sp.x + lx;
      if (px < 0 || px >= 224) continue;
      int sx = sp.flip_y ? lx : (15 - lx);
      unsigned char pixel = bnj_spritetiles[sp.code][sy][sx];
      if (pixel) ptr[px] = palette[pixel];
    }
  }
}

void bnj::blit_bg_tile(short row, int idx) {
  const bnj_bgtile_s &bt = bg_list[idx];
  short band_y0 = row * 8;

  for (int ly = 0; ly < 16; ly++) {
    short py = bt.y + ly;
    if (py < band_y0 || py >= band_y0 + 8) continue;
    int local_row = py - band_y0;
    int sy = 15 - ly; // stessa correzione 180 gradi

    unsigned short *ptr = frame_buffer + local_row * 224;
    for (int lx = 0; lx < 16; lx++) {
      short px = bt.x + lx;
      if (px < 0 || px >= 224) continue;
      int sx = 15 - lx;
      unsigned char pixel = bnj_bgtiles[bt.code][sy][sx];
      ptr[px] = palette[8 + pixel]; // opaco, colore base 8 (come btime)
    }
  }
}

void bnj::blit_tile(short row, char col) {
  if (row < TILE_ROW_MIN || row > TILE_ROW_MAX) return;

  short x_tile = TILE_ROW_XBASE - row;      // 0..31, NESSUN crop (visarea 256 piena)
  short y_tile = col + TILE_COL_OFFSET;     // 0..31 (invariato da btime)
  unsigned short offs = 32 * (31 - x_tile) + y_tile;
  unsigned short code = video_ram[offs] + 256 * (color_ram[offs] & 3);

  // Filtro priorita' (draw_chars: "priority" -- bit7 del code, usato da
  // screen_update_bnj per disegnare in DUE passate quando lo sfondo e'
  // attivo: priority=1 PRIMA degli sprite, priority=0 DOPO -- vedi
  // render_row). tile_priority_filter=-1 = nessun filtro (schermo senza
  // sfondo attivo, draw_chars priority=-1 nel sorgente).
  if (tile_priority_filter != -1 && tile_priority_filter != ((code >> 7) & 1)) return;

  const unsigned char (*tile)[8] = bnj_chartiles[code];
  unsigned short *ptr = frame_buffer + 8 * col;
  for (char r = 0; r < 8; r++, ptr += 224) {
    for (char c = 0; c < 8; c++) {
      unsigned char pixel = tile[7 - r][7 - c]; // 180 gradi
      if (pixel) ptr[c] = palette[pixel];
      // pen0: trasparente quando lo sfondo e' attivo (transparency=true in
      // ENTRAMBE le draw_chars di screen_update_bnj quando bnj_scroll[0]
      // != 0), opaco altrimenti (transparency=false, priority=-1).
      else if (!bg_enabled) ptr[c] = palette[0];
    }
  }
}

void bnj::render_row(short row) {
  // Sfarfallio confermato SOLO nello sfondo (muretti/erba ai lati), NON
  // nel testo HUD (punteggio/etichette, stabile nel video analizzato) --
  // misurato con precisione (varianza temporale per riga su video HW
  // allineato a 224x288): instabilita' reale ~40px in cima e ~24px in
  // fondo; +8px ciascun lato ("perfetto" su HW); +altri 16px ciascun lato
  // su richiesta esplicita dell'utente. Va nascosto SOLO il layer di
  // sfondo (e gli sprite, vedi sotto) in quelle bande, i char
  // (punteggio/etichette) devono restare visibili -- quindi il salto e'
  // dentro il solo ciclo blit_bg_tile/blit_sprite, non un return
  // dell'intera riga (primo tentativo, sbagliato: cancellava anche il testo).
  bool hide_bg_here = (row <= 3 || row >= 34);

  if (bg_enabled) {
    if (!hide_bg_here) {
      for (int i = 0; i < bg_count; i++) {
        if (bg_list[i].y < 8 * (row + 1) && (bg_list[i].y + 16) > 8 * row)
          blit_bg_tile(row, i);
      }
    }

    // priorita'=1 PRIMA degli sprite
    tile_priority_filter = 1;
    for (char col = 0; col < 28; col++)
      blit_tile(row, col);

    // Niente sprite dove lo sfondo e' nascosto (richiesto dall'utente:
    // un'auto "sospesa" sul nero senza strada/muretti sotto era brutta da
    // vedere) -- coerente col nascondere l'intero layer di sfondo li'.
    if (!hide_bg_here) {
      for (unsigned char s = 0; s < spr_count; s++) {
        if (spr_list[s].y < 8 * (row + 1) && (spr_list[s].y + 16) > 8 * row)
          blit_sprite(row, s);
      }
    }

    // priorita'=0 DOPO gli sprite (sopra)
    tile_priority_filter = 0;
    for (char col = 0; col < 28; col++)
      blit_tile(row, col);
  } else {
    tile_priority_filter = -1;
    for (char col = 0; col < 28; col++)
      blit_tile(row, col);

    for (unsigned char s = 0; s < spr_count; s++) {
      if (spr_list[s].y < 8 * (row + 1) && (spr_list[s].y + 16) > 8 * row)
        blit_sprite(row, s);
    }
  }
}

const unsigned short *bnj::logo(void) {
  return bnj_logo;
}

// High score: da MAME hiscore.dat, voce condivisa bnj/bnjm/brubber.
const hiscore_region_S *bnj::hiscoreRegions(unsigned char *count) {
  static const hiscore_region_S regions[] = {
    { 0x000a, 3,     0x12, 0x01 },
    { 0x0500, 0x26c, 0x01, 0x00 },
    { 0x0640, 1,     0x4d, 0x4d },
  };
  *count = sizeof(regions) / sizeof(regions[0]);
  return regions;
}
