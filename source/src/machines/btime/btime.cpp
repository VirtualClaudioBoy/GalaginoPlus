#include "btime.h"

// ---------------------------------------------------------------------------
// DECO CPU-7 decrypt (decocpu7.cpp, letto per intero): SOLO sul fetch di
// opcode, SOLO se l'istruzione precedente ha scritto in memoria (flag
// had_written, consumato una volta), E SOLO se (addr & 0x104) == 0x104.
// bitswap<8>(v, 6,5,3,4,2,7,1,0): out7=in6 out6=in5 out5=in3 out4=in4
// out3=in2 out2=in7 out1=in1 out0=in0
// ---------------------------------------------------------------------------
static inline uint8_t deco_cpu7_bitswap(uint8_t v) {
  return (uint8_t)(
    (((v >> 6) & 1) << 7) | (((v >> 5) & 1) << 6) |
    (((v >> 3) & 1) << 5) | (((v >> 4) & 1) << 4) |
    (((v >> 2) & 1) << 3) | (((v >> 7) & 1) << 2) |
    (((v >> 1) & 1) << 1) | (((v >> 0) & 1) << 0));
}

static inline unsigned short xy_swap(unsigned short local_offset) {
  unsigned char x = local_offset / 32;
  unsigned char y = local_offset % 32;
  return 32 * y + x;
}

void btime::start() {
  work_ram  = memory + WORK_RAM_OFFSET;
  video_ram = memory + VIDEORAM_OFFSET;
  color_ram = memory + COLORRAM_OFFSET;
  audio_ram = memory + AUDIORAM_OFFSET;
  memset(memory, 0, MEM_FREE);
  memset(palette, 0, sizeof(palette));

  cpu_main.read  = main_read;
  cpu_main.write = main_write;
  cpu_main.fetch = main_fetch;
  cpu_main.user  = this;
  m6502_reset(&cpu_main);

  cpu_audio.read  = audio_read;
  cpu_audio.write = audio_write;
  cpu_audio.fetch = 0; // non cifrata: fetch == read
  cpu_audio.user  = this;
  m6502_reset(&cpu_audio);
}

void btime::reset() {
  machineBase::reset();
  work_ram  = memory + WORK_RAM_OFFSET;
  video_ram = memory + VIDEORAM_OFFSET;
  color_ram = memory + COLORRAM_OFFSET;
  audio_ram = memory + AUDIORAM_OFFSET;

  memset(palette, 0, sizeof(palette));
  flip_screen = 0;
  sound_latch = 0;
  bnj_scroll[0] = bnj_scroll[1] = 0;
  memset(btime_tilemap, 0, sizeof(btime_tilemap));
  audio_nmi_enable = 0;
  audio_nmi_scanline_bit = 0;
  coin_prev = false;
  ay_port[0] = ay_port[1] = 0;
  cpu7_had_written = false;

  // emulation_start() chiama reset() PRIMA di start() (vedi emulation.cpp):
  // al primo giro cpu_main.read/cpu_audio.read sono ancora NULL (struct
  // azzerata dal costruttore) -- m6502_reset() farebbe una lettura a vuoto
  // (rd16 su 0xFFFC) chiamando un function pointer NULL e crashando
  // (stesso guard gia' usato in dkong3::reset() per lo stesso motivo).
  if (cpu_main.read)  m6502_reset(&cpu_main);
  if (cpu_audio.read) m6502_reset(&cpu_audio);
}

// ---------------------------------------------------------------------------
// Palette RAM (0x0c00-0x0c0f), formato BGR_233_inverted (btime.cpp righe
// 412-427): bit7/6=BLUE(15k/33k) bit5/4/3=GREEN(15k/33k/47k)
// bit2/1/0=RED(15k/33k/47k), TUTTI invertiti. Pesi derivati dai valori
// di resistenza dati nel sorgente (conduttanza 1/R normalizzata a 255).
// ---------------------------------------------------------------------------
void btime::palette_write(unsigned char index, unsigned char value) {
  if (index >= 16) return;
  unsigned char inv = ~value;
  unsigned char r = (inv & 1) * 46 + ((inv >> 1) & 1) * 65 + ((inv >> 2) & 1) * 144;
  unsigned char g = ((inv >> 3) & 1) * 46 + ((inv >> 4) & 1) * 65 + ((inv >> 5) & 1) * 144;
  unsigned char b = ((inv >> 6) & 1) * 80 + ((inv >> 7) & 1) * 175;

  unsigned short rgb = ((r * 31 / 255) << 11) | ((g * 63 / 255) << 5) | (b * 31 / 255);
  palette[index] = ((rgb & 0xff00) >> 8) | ((rgb & 0xff) << 8); // byte-swapped, come altrove nel progetto
}

// ---------------------------------------------------------------------------
// Main CPU (DECO CPU-7)
// ---------------------------------------------------------------------------
uint8_t btime::main_read(m6502_t *cpu, uint16_t addr) {
  btime *s = (btime*)cpu->user;

  // ROM controllata per PRIMA: e' il caso di gran lunga piu' frequente
  // (ogni fetch di opcode + gran parte delle letture dati passa da qui,
  // dato che il programma vive quasi tutto in 0xc000-0xffff) -- prima
  // versione controllava RAM/video/color PRIMA della ROM, penalizzando
  // il percorso piu' caldo ad ogni singola istruzione eseguita (misurato
  // con DEBUG_TIMING: CPU quasi al 100% del budget per frame).
  if (addr >= 0xb000) return btime_rom_main[addr - 0xb000];

  if (addr < 0x0800) return s->work_ram[addr];
  if (addr >= 0x1000 && addr < 0x1400) return s->video_ram[addr - 0x1000];
  if (addr >= 0x1400 && addr < 0x1800) return s->color_ram[addr - 0x1400];
  if (addr >= 0x1800 && addr < 0x1c00) return s->video_ram[xy_swap(addr - 0x1800)];
  if (addr >= 0x1c00 && addr < 0x2000) return s->color_ram[xy_swap(addr - 0x1c00)];

  if (addr == 0x4000) { // P1
    unsigned char k = s->input->buttons_get();
    unsigned char r = 0xff;
    if (k & BUTTON_RIGHT) r &= ~0x01;
    if (k & BUTTON_LEFT)  r &= ~0x02;
    if (k & BUTTON_UP)    r &= ~0x04;
    if (k & BUTTON_DOWN)  r &= ~0x08;
    if (k & BUTTON_FIRE)  r &= ~0x10;
    return r;
  }
  if (addr == 0x4001) return 0xff; // P2 (cocktail, non usato)
  if (addr == 0x4002) { // SYSTEM: start/tilt ACTIVE_LOW, coin ACTIVE_HIGH
    unsigned char k = s->input->buttons_get();
    unsigned char r = 0x3f; // bit0/1/2/3 low-active a riposo=1, bit6/7 high-active a riposo=0
    if (k & BUTTON_START) r &= ~0x01;
    if (k & BUTTON_COIN)  r |= 0x40;
    return r;
  }
  if (addr == 0x4003) return (unsigned char)(BTIME_DSW1 | (s->vblank_bit << 7));
  if (addr == 0x4004) return BTIME_DSW2;

  return 0xff;
}

void btime::main_write(m6502_t *cpu, uint16_t addr, uint8_t val) {
  btime *s = (btime*)cpu->user;
  s->cpu7_had_written = true;

  if (addr < 0x0800) { s->work_ram[addr] = val; return; }
  if (addr >= 0x0c00 && addr <= 0x0c0f) { s->palette_write(addr - 0x0c00, val); return; }
  if (addr >= 0x1000 && addr < 0x1400) { s->video_ram[addr - 0x1000] = val; return; }
  if (addr >= 0x1400 && addr < 0x1800) { s->color_ram[addr - 0x1400] = val; return; }
  if (addr >= 0x1800 && addr < 0x1c00) { s->video_ram[xy_swap(addr - 0x1800)] = val; return; }
  if (addr >= 0x1c00 && addr < 0x2000) { s->color_ram[xy_swap(addr - 0x1c00)] = val; return; }

  if (addr == 0x4002) { s->flip_screen = val & 1; return; }
  if (addr == 0x4003) {
    s->sound_latch = val;
    // GENERIC_LATCH_8 data_pending_callback -> IRQ CPU audio (vedi nota in
    // audio_read): ogni scrittura del main CPU sveglia l'audio con un IRQ,
    // indipendente dalla NMI periodica.
    s->cpu_audio.irq = 1;
    return;
  }
  if (addr == 0x4004) { s->bnj_scroll[0] = val; return; }
}

uint8_t btime::main_fetch(m6502_t *cpu, uint16_t addr) {
  uint8_t raw = btime::main_read(cpu, addr);
  btime *s = (btime*)cpu->user;
  if (s->cpu7_had_written) {
    s->cpu7_had_written = false;
    if ((addr & 0x104) == 0x104)
      raw = deco_cpu7_bitswap(raw);
  }
  return raw;
}

// ---------------------------------------------------------------------------
// Audio CPU (6502 nudo) + 2x AY-3-8910 (soundregs[], vedi emulation/audio.cpp)
// ---------------------------------------------------------------------------
uint8_t btime::audio_read(m6502_t *cpu, uint16_t addr) {
  btime *s = (btime*)cpu->user;
  if (addr < 0x2000) return s->audio_ram[addr & 0x3ff];
  if (addr >= 0xa000 && addr < 0xc000) {
    // GENERIC_LATCH_8 + data_pending_callback->set_inputline(m_audiocpu,0)
    // (btime.cpp righe 2307/2321-2322, MAI implementato prima d'ora): ogni
    // NUOVA scrittura del main CPU al soundlatch genera un IRQ (non NMI!)
    // sulla CPU audio -- e' COSI' che il gioco dice "c'e' un comando nuovo
    // da eseguire ORA", separato dalla NMI periodica (che scandisce solo il
    // tempo). La lettura del latch (qui) ACKNOWLEDGE automaticamente l'IRQ
    // (btime non chiama set_separate_acknowledge, a differenza di disco).
    cpu->irq = 0;
    return s->sound_latch;
  }
  if (addr >= 0xe000) return btime_rom_audio[addr & 0x0fff];
  return 0xff;
}

void btime::audio_write(m6502_t *cpu, uint16_t addr, uint8_t val) {
  btime *s = (btime*)cpu->user;
  if (addr < 0x2000) { s->audio_ram[addr & 0x3ff] = val; return; }
  if (addr >= 0x2000 && addr < 0x4000) { if (s->ay_port[0] < 14) s->soundregs[0x00 + s->ay_port[0]] = val; return; }
  if (addr >= 0x4000 && addr < 0x6000) { s->ay_port[0] = val & 0x0f; return; }
  if (addr >= 0x6000 && addr < 0x8000) { if (s->ay_port[1] < 14) s->soundregs[0x10 + s->ay_port[1]] = val; return; }
  if (addr >= 0x8000 && addr < 0xa000) { s->ay_port[1] = val & 0x0f; return; }
  if (addr >= 0xc000 && addr < 0xe000) { s->audio_nmi_enable = val & 1; return; }
}

// ---------------------------------------------------------------------------
// Frame execution
// ---------------------------------------------------------------------------
void btime::run_frame(void) {
  // Main CPU: nessun interrupt periodico (a differenza di quasi tutti gli
  // altri giochi del progetto) -- solo IRQ al fronte di salita di COIN.
  unsigned char keymask = input->buttons_get();
  bool coin_now = (keymask & BUTTON_COIN) != 0;
  if (coin_now && !coin_prev) cpu_main.irq = 1;
  coin_prev = coin_now;

  current_cpu = 0;
  // VBLANK (bring-up #3): un singolo toggle per frame reale faceva restare
  // il bit COSTANTE per tutto il budget di m6502_exec() -- se il gioco fa
  // un "wait for vblank" esplicito nel suo loop principale (tipico per un
  // gioco senza interrupt periodico), poteva non vedere MAI una transizione
  // entro il frame e sprecare l'intero budget in attesa (velocita' troppo
  // bassa). Ora si esegue in 2 tronconi che approssimano la proporzione
  // reale display-attivo/vblank (visibile 240 righe su 272 totali):
  // attivo (bit=0) poi vblank (bit=1), cosi' il polling vede SEMPRE
  // almeno una transizione vera entro ogni frame.
  vblank_bit = 0;
  m6502_exec(&cpu_main, 22000);
  vblank_bit = 1;
  m6502_exec(&cpu_main, 3000);
  cpu_main.irq = 0; // livello "one-shot": assunto servito entro il budget del frame

  // Audio CPU: NMI approssimata (vedi memoria progetto) -- il timer reale
  // scatta ogni 8 scanline (~17 volte/frame su 272 scanline totali),
  // gated dall'enable diretto scritto dal programma audio stesso.
  current_cpu = 1;
  const int NMI_SEGS = 17;
  const int CYCLES_PER_SEG = 490; // 500000/60/17 circa
  for (int i = 0; i < NMI_SEGS; i++) {
    m6502_exec(&cpu_audio, CYCLES_PER_SEG);
    if (audio_nmi_enable) cpu_audio.nmi = 1;
  }

  if (!game_started) game_started = 1;
}

// ---------------------------------------------------------------------------
// Video: raccolta sprite (prepare_frame) e rendering a bande (render_row)
// ---------------------------------------------------------------------------
void btime::prepare_frame(void) {
  spr_count = 0;
  for (int i = 0; i < 8 && spr_count < 8; i++) {
    unsigned short offs = 128 * i;
    unsigned char attr = video_ram[offs + 0];
    if (!(attr & 0x01)) continue;

    unsigned char code   = video_ram[offs + 32];
    unsigned char y_byte = video_ram[offs + 64];
    unsigned char x_byte = video_ram[offs + 96];

    // draw_sprites(bitmap,cliprect,0,1,0,m_videoram,0x20) nel sorgente
    // originale: sprite_y_adjust=1 -- "y = y - sprite_y_adjust;" applicato
    // DOPO il calcolo base, PRIMA di questa sessione mai trascritto (bug
    // trovato confrontando pixel-per-pixel una foto HW con uno screenshot
    // MAME: gli ingredienti fluttuavano sopra le piattaforme invece di
    // toccarle/sovrapporle leggermente come in MAME).
    short native_x = 240 - x_byte;
    short native_y = 240 - y_byte - 1;

    // Mapping ROT270 derivato (vedi memoria progetto project_btime.md):
    // portrait_px = native_y - 16 ; portrait_py = 272 - native_x
    short px = native_y - 16;
    short py = 272 - native_x;

    if (px <= -16 || px >= 224 || py <= -16 || py >= 288) continue;

    btime_sprite_s &sp = spr_list[spr_count++];
    sp.x = px;
    sp.y = py;
    sp.code = code;
    sp.flip_x = (attr & 0x04) ? 1 : 0;
    sp.flip_y = (attr & 0x02) ? 1 : 0;
  }

  // Layer di sfondo (draw_background originale) -- piattaforme/scale.
  // Attivo quando bnj_scroll[0]&0x10. bnj_scroll[1] resta sempre 0 in
  // questo set (btime_map non mappa 0x4005), quindi scroll parte da
  // -((bnj_scroll[0]&0x03)<<8), come da sorgente.
  bg_enabled = (bnj_scroll[0] & 0x10) != 0;
  bg_count = 0;
  if (bg_enabled) {
    unsigned char start = flip_screen ? 0 : 1;
    for (int i = 0; i < 4; i++) {
      btime_tilemap[i] = start | (bnj_scroll[0] & 0x04);
      start = (start + 1) & 0x03;
    }

    int scroll = -((int)(bnj_scroll[1]) | ((bnj_scroll[0] & 0x03) << 8));
    for (int i = 0; i < 5 && bg_count < BG_LIST_MAX; i++, scroll += 256) {
      if (scroll > 256) break;
      if (scroll < -256) continue;
      unsigned short tileoffset = btime_tilemap[i & 3] * 0x100;
      for (int offs = 0; offs < 0x100 && bg_count < BG_LIST_MAX; offs++) {
        int native_x = 240 - (16 * (offs / 16) + scroll) - 1;
        int native_y = 16 * (offs % 16);
        if (flip_screen) { native_x = 240 - native_x; native_y = 240 - native_y; }

        short px = native_y - 16;
        short py = 272 - native_x;
        if (px <= -16 || px >= 224 || py <= -16 || py >= 288) continue;

        btime_bgtile_s &bt = bg_list[bg_count++];
        bt.x = px;
        bt.y = py;
        bt.code = btime_bgmap[tileoffset + offs];
      }
    }
  }
}

void btime::blit_sprite(short row, unsigned char s) {
  const btime_sprite_s &sp = spr_list[s];
  short band_y0 = row * 8;

  for (int ly = 0; ly < 16; ly++) {
    short py = sp.y + ly;
    if (py < band_y0 || py >= band_y0 + 8) continue;
    int local_row = py - band_y0;

    // 180 gradi rispetto alla lettura diretta tile[ly][lx] (bug trovato su
    // HW: sprite/char corretti in posizione ma ruotati di 180 gradi) --
    // sy/sx di base sono invertiti (15-ly/15-lx); il flip di gioco, se
    // attivo, annulla l'inversione su quell'asse (doppia negazione).
    // IMPORTANTE (bring-up #3): flip_x/flip_y sono definiti da MAME
    // sull'asse NATIVO (pre-rotazione). La trasformazione ROT270 scambia
    // gli assi (nativo_x -> portrait_y, nativo_y -> portrait_x), quindi e'
    // flip_x (nativo_x) a dover pilotare l'asse VERTICALE portrait (sy),
    // e flip_y (nativo_y) l'asse ORIZZONTALE portrait (sx) -- scambiati
    // rispetto al primo tentativo, che li applicava dritti. Sintomo che ha
    // confermato lo scambio: solo gli sprite con flip attivo (es. il
    // personaggio che va "in alto"/"a destra") restavano ruotati di 180,
    // mentre char/sfondo (che usano solo flip_screen globale, sempre 0)
    // erano gia' corretti dal fix precedente.
    int sy = sp.flip_x ? ly : (15 - ly);
    unsigned short *ptr = frame_buffer + local_row * 224;
    for (int lx = 0; lx < 16; lx++) {
      short px = sp.x + lx;
      if (px < 0 || px >= 224) continue;
      int sx = sp.flip_y ? lx : (15 - lx);
      unsigned char pixel = btime_spritetiles[sp.code][sy][sx];
      if (pixel) ptr[px] = palette[pixel];
    }
  }
}

void btime::blit_bg_tile(short row, int idx) {
  const btime_bgtile_s &bt = bg_list[idx];
  short band_y0 = row * 8;

  for (int ly = 0; ly < 16; ly++) {
    short py = bt.y + ly;
    if (py < band_y0 || py >= band_y0 + 8) continue;
    int local_row = py - band_y0;
    int sy = 15 - ly; // stessa correzione 180 gradi di sprite/char

    unsigned short *ptr = frame_buffer + local_row * 224;
    for (int lx = 0; lx < 16; lx++) {
      short px = bt.x + lx;
      if (px < 0 || px >= 224) continue;
      int sx = 15 - lx;
      unsigned char pixel = btime_bgtiles[bt.code][sy][sx];
      ptr[px] = palette[8 + pixel]; // opaco: colore base 8, disegna anche pixel 0
    }
  }
}

// CALIBRAZIONE EMPIRICA (bring-up #12/#13): gli ingredienti a riposo sono
// CARATTERI (8x8, banco alto via colorram&3), NON sprite ne' sfondo --
// confermato scartando sistematicamente le altre ipotesi (log seriale
// mostra che gli 8 slot sprite usano SOLO codici 64-116 = giocatore/
// nemici; lo sfondo contiene solo piattaforme+punteggi, l'utente ha
// confermato di non toccarlo). Range codice ESTESO a 512-863 (confermato
// dall'utente: 512 e' la parte alta del panino, inizialmente scambiata
// per una tile "scala" nel preview grigio a bassa risoluzione). Texture
// ripetuta (barre verticali/ondulata=pane/lattuga, punteggiata=carne) su
// molte tile adiacenti, diversa dai frammenti personaggio/nemico
// (256-511 esclusi qui, forme variegate non ripetute). Applicare lo
// spostamento di 8px (1 riga tile) SOLO a questo range, leggendo la riga
// precedente (native) e disegnandola alla riga corrente se il suo
// codice e' nel range -- equivalente a "abbassare di una riga tile"
// senza serve un layer pixel-preciso separato (il carattere e' comunque
// sulla stessa griglia da 8px).
static inline bool btime_is_ingredient_char(unsigned short code) {
  return code >= 512 && code <= 863;
}

void btime::blit_tile(short row, char col) {
  if (row < TILE_ROW_MIN || row > TILE_ROW_MAX) return;

  short x_tile = TILE_ROW_XBASE - row;      // 0..31
  short y_tile = col + TILE_COL_OFFSET;     // 0..31
  unsigned short offs = 32 * (31 - x_tile) + y_tile;
  unsigned short code = video_ram[offs] + 256 * (color_ram[offs] & 3);

  // Se la riga precedente (native x_tile+1, cioe' un tile PIU' IN ALTO in
  // termini nativi che sotto ROT270 diventa una riga portrait PIU' IN
  // ALTO cioe' row-1) contiene un ingrediente, mostralo QUI (spostato di
  // 8px in basso) al posto del contenuto naturale di questa riga.
  short x_tile_prev = x_tile + 1;
  if (x_tile_prev <= 31) {
    unsigned short offs_prev = 32 * (31 - x_tile_prev) + y_tile;
    unsigned short code_prev = video_ram[offs_prev] + 256 * (color_ram[offs_prev] & 3);
    if (btime_is_ingredient_char(code_prev)) {
      code = code_prev;
    } else if (btime_is_ingredient_char(code)) {
      // Il contenuto NATURALE di questa riga e' un ingrediente: verra'
      // disegnato spostato in basso quando si processa row+1, non qui.
      return;
    }
  } else if (btime_is_ingredient_char(code)) {
    return;
  }

  const unsigned char (*tile)[8] = btime_chartiles[code];

  unsigned short *ptr = frame_buffer + 8 * col;
  for (char r = 0; r < 8; r++, ptr += 224) {
    for (char c = 0; c < 8; c++) {
      // 180 gradi (vedi nota in blit_sprite)
      unsigned char pixel = tile[7 - r][7 - c];
      // pen0 trasparente SOLO quando lo sfondo e' attivo (lascia vedere le
      // scale/piattaforme sotto), opaco altrimenti (draw_chars transparency
      // = bg_enabled nel sorgente originale)
      if (pixel) ptr[c] = palette[pixel];
      else if (!bg_enabled) ptr[c] = palette[0];
    }
  }
}

void btime::render_row(short row) {
  if (bg_enabled) {
    for (int i = 0; i < bg_count; i++) {
      if (bg_list[i].y < 8 * (row + 1) && (bg_list[i].y + 16) > 8 * row)
        blit_bg_tile(row, i);
    }
  }

  for (char col = 0; col < 28; col++)
    blit_tile(row, col);

  for (unsigned char s = 0; s < spr_count; s++) {
    if (spr_list[s].y < 8 * (row + 1) && (spr_list[s].y + 16) > 8 * row)
      blit_sprite(row, s);
  }
}

const unsigned short *btime::logo(void) {
  return btime_logo;
}

// High score: da MAME hiscore.dat, voce condivisa dal gruppo
// btime/btime2/btime3/btimem (nessuna variante per set nel dat).
const hiscore_region_S *btime::hiscoreRegions(unsigned char *count) {
  static const hiscore_region_S regions[] = {
    { 0x0033, 0x27, 0x00, 0xff },
  };
  *count = sizeof(regions) / sizeof(regions[0]);
  return regions;
}
