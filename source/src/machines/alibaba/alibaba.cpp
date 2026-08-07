#include "alibaba.h"

unsigned char alibaba::opZ80(unsigned short Addr) {
  return alibaba_rom[Addr];
}

unsigned char alibaba::rdZ80(unsigned short Addr) {
  // NB: NIENTE "Addr &= 0x7fff" qui (a differenza di pacman/crush base): per
  // quei giochi il bit 15 non e' cablato (intera ROM rispecchiata su
  // 0x8000-0xffff), ma per Ali Baba il bit 15 e' SIGNIFICATIVO -- distingue
  // i banchi ROM extra (0x8000/0xa000, vedi sopra) dalla ROM principale.
  // Mascherarlo farebbe leggere/eseguire i dati sbagliati per quegli indirizzi.

  if(Addr < 0x4000)
    return alibaba_rom[Addr];

  // banchi ROM extra: 0x8000-0x8fff (6l) e 0xa000-0xbfff (6m, mirror(0x1800)
  // per MAME -> replicato 4 volte in alibaba_rom.h). Letture DATI (non solo
  // fetch istruzioni via opZ80) devono vedere questi byte, non 0xff.
  if(Addr >= 0x8000 && Addr <= 0x8fff)
    return alibaba_rom[Addr];
  if(Addr >= 0xa000 && Addr <= 0xbfff)
    return alibaba_rom[Addr];

  // videoram/colorram/spriteram1, identico al layout pacman base.
  // ATTENZIONE: in MAME la RAM ha mirror(0xa000) -- i bit 13 e 15 NON sono
  // decodificati, quindi 0x4000-0x4fff appare anche a 0x6000/0xc000/0xe000.
  // La routine di stampa messaggi in ROM (0x2c5e) ci conta davvero: per le
  // stringhe "riga orizzontale" (HIGH SCORE/CREDIT/LEVEL) la tabella a 0x36a5
  // codifica il flag "stride -1" nel bit 15 dell'offset VRAM e la routine NON
  // lo maschera mai -> scrive testo/colore a 0xc000-0xc7ff confidando nello
  // specchio hardware. Senza questo mirror quelle scritte si perdono (bug
  // "etichette mai disegnate" del primo bring-up).
  if((Addr & 0x5000) == 0x4000)
    return memory[Addr & 0x0fff];

  // extra RAM 0x9000-0x93ff (mirror 0x0c00 fino a 0x9fff)
  if((Addr & 0xfc00) == 0x9000)
    return memory[0x1000 + (Addr & 0x3ff)];

  if((Addr & 0xf000) == 0x5000) {
    // non conosciamo il marker esatto di "boot completato" scritto in video
    // RAM da questo gioco (nessuna fonte video MAME disponibile): usiamo la
    // prima lettura della zona I/O come indicazione robusta che il boot e'
    // avviato (stesso approccio di crush.cpp), per non rischiare di non far
    // mai scattare game_started e girare l'emulazione permanentemente senza
    // sincronizzazione a 60Hz.
    game_started = 1;

    unsigned char keymask = input->buttons_get();

    if(Addr == 0x5000) {
      unsigned char retval = 0xff;
      if(keymask & BUTTON_UP)    retval &= ~0x01;
      if(keymask & BUTTON_LEFT)  retval &= ~0x02;
      if(keymask & BUTTON_RIGHT) retval &= ~0x04;
      if(keymask & BUTTON_DOWN)  retval &= ~0x08;
      if(keymask & BUTTON_COIN)  retval &= ~0x20;
      if(keymask & BUTTON_FIRE)  retval &= ~0x40;
      return retval;
    }

    if(Addr == 0x5040) {
      unsigned char retval = 0xff;
      if(keymask & BUTTON_START) retval &= ~0x20;
      return retval;
    }

    if(Addr == 0x5080)
      return ALIBABA_DIP;

    // mystery item: registri di lettura non contigui (0x50c0-0x50c1)
    if(Addr == 0x50c0)
      return random() & 0x0f;          // determina il tipo di premio
    if(Addr == 0x50c1)
      return (mystery_clock >= 24) ? 1 : 0;  // 1 = lampada "accesa"
  }
  return 0xff;
}

void alibaba::wrZ80(unsigned short Addr, unsigned char Value) {
  // NB: niente mascheratura GLOBALE del bit 15 qui (i banchi ROM 0x8000/0xa000
  // lo usano davvero), ma la RAM SI' che e' specchiata: mirror(0xa000) come in
  // MAME (bit 13/15 ignorati), vedi commento in rdZ80 -- indispensabile per la
  // routine di stampa messaggi che scrive HIGH SCORE/CREDIT/LEVEL passando
  // dagli specchi 0xc000-0xc7ff (bit 15 = flag stride nella tabella stringhe).
  if((Addr & 0x5000) == 0x4000) {
    memory[Addr & 0x0fff] = Value;
    return;
  }

  if((Addr & 0xfc00) == 0x9000) {
    memory[0x1000 + (Addr & 0x3ff)] = Value;
    return;
  }

  if((Addr & 0xf000) == 0x5000) {
    // latch1 @0x5000-0x5007: solo bit4/5 (LED) e 6/7 (coin lockout/counter),
    // tutti cosmetici, ignorati (0x5000 stesso e' il watchdog, idem ignorato)

    // spriteram2 @0x5050-0x505f: va controllato PRIMA di sound_w qui sotto.
    // In MAME i due range si sovrappongono sulla carta (0x5040-0x506f per il
    // suono, "non contiguo" per davvero: il buco e' proprio 0x5050-0x505f,
    // riservato a spriteram2) -- MAME risolve la sovrapposizione dando
    // priorita' all'installazione PIU' RECENTE nella memory map (spriteram2 e'
    // dichiarato DOPO sound_w). Controllare prima sound_w (bug originale)
    // intercettava per errore OGNI scrittura di posizione sprite, che quindi
    // non raggiungeva mai la RAM -- sintomo osservato su HW: sprite di gioco
    // "congelati" fuori schermo, mai aggiornati durante il movimento reale.
    if(Addr >= 0x5050 && Addr <= 0x505f) {
      memory[Addr - 0x4000] = Value;
      return;
    }

    // sound_w @0x5040-0x504f e 0x5060-0x506f (buco 0x5050-0x505f sopra):
    // registro non contiguo, va rimappato prima di scrivere sul motore Namco
    // (identico a MAME alibaba_state::sound_w)
    if(Addr >= 0x5040 && Addr <= 0x506f) {
      unsigned short offset = Addr - 0x5040;
      unsigned char reg = ((offset >> 1) & 0x10) | (offset & 0x0f);
      if(reg < 0x20 && soundregs[reg] != (Value & 0x0f))
        soundregs[reg] = Value & 0x0f;
      return;
    }

    // mystery_w @0x5080: d0 avvia/ferma l'orologio, d1 lo mostra
    if(Addr == 0x5080) {
      if((mystery_control ^ Value) & 1)
        mystery_prescaler = mystery_clock = 0;
      mystery_control = Value;
      return;
    }

    // latch2 @0x50c0-0x50c7: bit0=abilita audio (ignorato, sempre attivo),
    // bit1=flip screen, bit2=irq mask
    if(Addr >= 0x50c0 && Addr <= 0x50c7) {
      unsigned char bit = Addr - 0x50c0;
      unsigned char v = Value & 1;
      if(bit == 1) flip_screen = v;
      if(bit == 2) irq_mask = v;
      return;
    }
  }
}

// from MAME hiscore.dat: alibaba condivide le regioni con l'intero gruppo
// hardware pacman base (stesso layout BCD + copie a video)
const hiscore_region_S *alibaba::hiscoreRegions(unsigned char *count) {
  static const hiscore_region_S regions[] = {
    { 0x4e88, 4, 0x00, 0x00 },
    { 0x43ed, 6, 0x40, 0x40 },
    { 0x43d1, 1, 0x48, 0x48 },
  };
  *count = sizeof(regions) / sizeof(regions[0]);
  return regions;
}

// DEBUG bring-up temporaneo: stampa TUTTA la videoram (0x000-0x3ff) e colorram
// (0x400-0x7ff) grezze, una volta ogni 2 secondi. Serve a cercare dove finiscono
// davvero i byte ASCII di "HIGH SCORE"/"CREDIT"/"LEVEL" (confermati presenti in
// ROM a 0x371d come testo statico) dentro la VRAM, invece di ipotizzare la
// colonna/riga a priori: se il testo compare da qualche parte con colore errato
// e' un bug di colore ("nero su nero"), se non compare mai il testo non viene
// proprio copiato in VRAM (routine non eseguita).
#define ALIBABA_DEBUG_LABELS 0

void alibaba::run_frame(void) {
  for(int i = 0; i < INST_PER_FRAME; i++) {
    StepZ80(cpu); StepZ80(cpu); StepZ80(cpu); StepZ80(cpu);
  }

#if ALIBABA_DEBUG_LABELS
  static int dbg_cnt = 0;
  if(++dbg_cnt >= 120) {
    dbg_cnt = 0;
    printf("[ALIBABA] VIDEORAM:\n");
    for(int a = 0; a < 1024; a += 32) {
      printf("[ALIBABA] %04x:", a);
      for(int c = 0; c < 32; c++) printf(" %02x", memory[a + c]);
      printf("\n");
    }
    printf("[ALIBABA] COLORRAM:\n");
    for(int a = 0; a < 1024; a += 32) {
      printf("[ALIBABA] %04x:", a);
      for(int c = 0; c < 32; c++) printf(" %02x", memory[0x400 + a + c]);
      printf("\n");
    }
  }
#endif

  // vblank: avanza l'orologio del "mystery" (ogni 64 frame, ~1.05s a 60Hz)
  mystery_prescaler = (mystery_prescaler + 1) & 0x3f;
  if(mystery_prescaler == 0 && (mystery_control & 1))
    mystery_clock = (mystery_clock + 1) & 0x1f;

  if(irq_mask)
    IntZ80(cpu, irq_ptr);
}

// DEBUG bring-up temporaneo: stampa lo stato grezzo di spriteram1/2 una volta
// al secondo, per capire con dati concreti (non ipotesi) perche' gli sprite
// di gioco non compaiono. Mettere a 1, ricompilare, guardare il seriale.
#define ALIBABA_DEBUG_SPRITES 0

// spriteram1 (code/color) vive a CPU 0x4ef0-0x4eff (memory offset 0x0ef0),
// spriteram2 (x/y) a CPU 0x5050-0x505f (memory offset 0x1050) -- ENTRAMBI
// diversi dagli offset standard pacman (0x0ff0/0x1060), per questo va
// ridefinito invece di ereditare pacman::prepare_frame invariato.
void alibaba::prepare_frame(void) {
#if ALIBABA_DEBUG_SPRITES
  static int dbg_cnt = 0;
  if(++dbg_cnt >= 60) {
    dbg_cnt = 0;
    printf("[ALIBABA] spriteram1(0x0ef0-ff):");
    for(int i = 0; i < 16; i++) printf(" %02x", memory[0x0ef0 + i]);
    printf("\n[ALIBABA] spriteram2(0x1050-5f):");
    for(int i = 0; i < 16; i++) printf(" %02x", memory[0x1050 + i]);
    printf("\n");
  }
#endif

  active_sprites = 0;
  for(int idx = 0; idx < 8 && active_sprites < 92; idx++) {
    int off = 2 * (7 - idx);
    struct sprite_S spr;

    spr.code  = memory[0x0ef0 + off] >> 2;
    spr.color = memory[0x0ef1 + off] & 63;
    spr.flags = memory[0x0ef0 + off] & 3;

    // adjust sprite position on screen for upright screen
    spr.x = 255 - 16 - memory[0x1050 + off];
    spr.y = 16 + 256 - memory[0x1051 + off];

    if((spr.code < 64) &&
       (spr.y > -16) && (spr.y < 288) &&
       (spr.x > -16) && (spr.x < 224)) {
      sprite[active_sprites++] = spr;
    }
  }

#if ALIBABA_DEBUG_SPRITES
  static int dbg_cnt2 = 0;
  if(++dbg_cnt2 >= 60) {
    dbg_cnt2 = 0;
    printf("[ALIBABA] active_sprites=%d\n", active_sprites);
    for(int i = 0; i < active_sprites; i++)
      printf("[ALIBABA]  sprite[%d] code=%d color=%d flags=%d x=%d y=%d\n",
             i, sprite[i].code, sprite[i].color, sprite[i].flags, sprite[i].x, sprite[i].y);
  }
#endif
}

const unsigned short *alibaba::tileRom(unsigned short addr) {
  return alibaba_tilemap[memory[addr]];
}

const unsigned short *alibaba::colorRom(unsigned short addr) {
  return alibaba_colormap[addr];
}

const unsigned long *alibaba::spriteRom(unsigned char flags, unsigned char code) {
  return alibaba_sprites[flags][code];
}

// Grafica "mystery" (lampada/orologio premio): 16x24 (ruotata 90 deg dalla
// sorgente ROM 24x16, stesso motivo hardware di tile/sprite -- vedi commento
// in alibaba_rom_convert.py/rotate_cw), 2 toni (pen 0/3 soli, vedi commento
// in alibaba_clockmap.h).
//
// Posizione/colore/logica presi dalla VERA routine MAME
// (E:\Download\pacman_v.cpp, alibaba_state::draw_clock):
//   // inactive half
//   if (m_mystery_clock <= 16)
//     gfx(2)->transpen(bitmap, cliprect, 0x1f, 1, 0,0, 120,112, 0);
//   // active half (sempre disegnata)
//   y = 96 + (m_mystery_clock & 0x10);
//   gfx(2)->transpen(bitmap, cliprect, m_mystery_clock^0x1f, 1, 0,0, 120,y, 0);
// Coordinate MAME in landscape (x=colonna orizzontale 0-287, y=riga verticale
// 0-223, schermo raw 288x224 -- screen.set_raw in pacman.cpp). Trasformate in
// portrait con la STESSA rotazione oraria gia' verificata su HW per i tile
// (vedi rotate_cw in alibaba_rom_convert.py): portrait_x=223-mame_y,
// portrait_y=mame_x. Risultato (in celle da 8px, verificato a mano):
//   riga portrait = 120/8 = 15 (fissa per entrambe le meta')
//   meta' "inactive" (tile fisso 0x1f): colonna 96/8 = 12
//   meta' "active", mystery_clock 0-15 (y=96):  colonna 223-96=127 -> 127/8=15... arrotondato,
//     top-left esatto del rettangolo ruotato e' colonna 14 (vedi derivazione)
//   meta' "active", mystery_clock 16-31 (y=112): colonna 96/8 = 12 (stessa
//     posizione della "inactive", che infatti a quel punto non viene piu'
//     disegnata -- si "scambiano" il posto)
// Colore: colorcode 1 (non 0 come ipotizzato inizialmente).
#define CLOCK_ROW        15  // riga tile (fissa per entrambe le meta')
#define CLOCK_COL_LEFT   12  // colonna "posizione sinistra" (inactive, o active quando clock>=16)
#define CLOCK_COL_RIGHT  14  // colonna "posizione destra" (active quando clock<16)

// Overlay DISATTIVATO (feedback utente su HW 2026-07-10): durante il gioco reale
// l'orologio centrale NON deve vedersi, mentre questa riproduzione fedele di
// MAME lo mostrerebbe come disco rosso al centro appena mystery_control bit1
// si accende (con clock=0 il tile attivo e' 0x1f = meta' disco PIENA, non
// vuota). La stessa emulazione MAME del mystery e' dichiaratamente incerta
// ("this is certainly wrong" in pacman.cpp su mystery_2_r): meglio nascondere
// l'overlay che mostrare un artefatto. La logica dei registri mystery
// (0x5080 W, 0x50c0/0x50c1 R, prescaler in run_frame) resta ATTIVA: serve al
// gameplay del premio. Rimettere a 1 solo se si scopre come appare davvero
// sull'hardware originale.
#define ALIBABA_SHOW_CLOCK 0

static void blit_clock_tile(unsigned short *frame_buffer, short row, short clock_col,
                             unsigned char tile_code, const unsigned short *colors) {
  short local_row = row - CLOCK_ROW;
  if(local_row < 0 || local_row >= 3) return;

  const unsigned long *tile = alibaba_clockmap[tile_code & 0x1f];
  unsigned short *ptr = frame_buffer + 8 * clock_col;

  for(char r = 0; r < 8; r++, ptr += (224 - 16)) {
    unsigned long pix = tile[local_row * 8 + r];
    for(char c = 0; c < 16; c++, pix >>= 2) {
      unsigned char pen = pix & 3;
      if(pen) *ptr = colors[pen];
      ptr++;
    }
  }
}

void alibaba::blit_clock(short row) {
#if !ALIBABA_SHOW_CLOCK
  return;                              // overlay nascosto, vedi commento sopra
#endif
  if(!(mystery_control & 2)) return;   // "show clock" non attivo

  const unsigned short *colors = colorRom(1);

  // inactive half: solo se mystery_clock <= 16, tile fisso, colonna sinistra
  if(mystery_clock <= 16)
    blit_clock_tile(frame_buffer, row, CLOCK_COL_LEFT, 0x1f, colors);

  // active half: sempre disegnata; colonna destra se clock<16, altrimenti
  // si sposta a sinistra (dove la inactive non c'e' piu')
  short active_col = (mystery_clock & 0x10) ? CLOCK_COL_LEFT : CLOCK_COL_RIGHT;
  blit_clock_tile(frame_buffer, row, active_col, mystery_clock ^ 0x1f, colors);
}

void alibaba::render_row(short row) {
  pacman::render_row(row);
  blit_clock(row);
}

const unsigned short *alibaba::logo(void) {
  return alibaba_logo;
}
