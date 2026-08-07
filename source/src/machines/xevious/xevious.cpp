#include "xevious.h"

// ============================================================================
// CPU dispatch — ogni CPU ha una ROM di dimensione diversa (16K/8K/4K),
// a differenza di galaga.cpp che assume tutte e tre almeno 16K
// ============================================================================
unsigned char xevious::opZ80(unsigned short Addr) {
  if (current_cpu == 0) return (Addr < 0x4000) ? rom_cpu1[Addr] : 0xff;
  if (current_cpu == 1) return (Addr < 0x2000) ? rom_cpu2[Addr] : 0xff;
  return (Addr < 0x1000) ? rom_cpu3[Addr] : 0xff;
}

// ============================================================================
// Planet-map lookup (schematic 9B) — formula esatta da xevious_bb_r()/
// xevious_bs_w() (E:\Download\xevious.cpp, letto per intero). rom2a/rom2b/
// rom2c sono puntatori nel blob xevious_planetmap.h (0x4000 byte: rom2a@0
// rom2b@0x1000 rom2c@0x3000, stesso layout della region "gfx4" reale).
// ============================================================================
void xevious::xevious_bs_w(unsigned char offset, unsigned char value) {
  xevious_bs[offset & 1] = value;
}

unsigned char xevious::xevious_bb_r(unsigned char offset) {
  const unsigned char *rom2a = planetmap;
  const unsigned char *rom2b = planetmap + 0x1000;
  const unsigned char *rom2c = planetmap + 0x3000;

  int adr_2b = ((xevious_bs[1] & 0x7e) << 6) | ((xevious_bs[0] & 0xfe) >> 1);
  int dat1;
  if (adr_2b & 1)
    dat1 = ((rom2a[adr_2b >> 1] & 0xf0) << 4) | rom2b[adr_2b];
  else
    dat1 = ((rom2a[adr_2b >> 1] & 0x0f) << 8) | rom2b[adr_2b];

  int adr_2c = ((dat1 & 0x1ff) << 2) | ((xevious_bs[1] & 1) << 1) | (xevious_bs[0] & 1);
  if (dat1 & 0x400) adr_2c ^= 1;
  if (dat1 & 0x200) adr_2c ^= 2;

  if (offset & 1)
    return rom2c[adr_2c | 0x800];   // BB1

  unsigned char dat2 = rom2c[adr_2c];
  dat2 = (dat2 & 0x3f) | ((dat2 & 0x80) >> 1) | ((dat2 & 0x40) << 1);  // swap bit6/7
  if (dat1 & 0x400) dat2 ^= 0x40;
  if (dat1 & 0x200) dat2 ^= 0x80;
  return dat2;   // BB0
}

// Voce hiscore.dat MAME (gruppo battles/sxevious/xevios/xevious/xeviousa):
//   @:maincpu,program,8510,4d,00,24   tabella high score (best-5 + iniziali)
//   @:maincpu,program,8024,3,00,00    top score (3 byte)
//   @:maincpu,program,8025,1,40,40    byte centrale del top score (check
//                                     init: default 0x8024-26 = 00 40 00)
// Trascrizione 1:1 come galaga::hiscoreRegions. Indirizzi tutti nella
// finestra RAM 0x8000-0x87ff, servita da rdZ80/wrZ80 indipendentemente da
// current_cpu -> i default hiscoreRead/Write vanno bene.
const hiscore_region_S *xevious::hiscoreRegions(unsigned char *count) {
  static const hiscore_region_S regions[] = {
    { 0x8510, 0x4d, 0x00, 0x24 },
    { 0x8024, 3,    0x00, 0x00 },
    { 0x8025, 1,    0x40, 0x40 },
  };
  *count = 3;
  return regions;
}

// ============================================================================
// Memoria condivisa (vedi xevious.h per la mappa completa) + IO custom
// Namco 06xx/50xx/51xx (shortcut adattato da galaga.cpp: risposta diretta
// invece del protocollo byte-per-byte reale, stessa famiglia di chip)
// ============================================================================
unsigned char xevious::rdZ80(unsigned short Addr) {
  if (current_cpu == 0 && Addr < 0x4000) return rom_cpu1[Addr];
  if (current_cpu == 1 && Addr < 0x2000) return rom_cpu2[Addr];
  if (current_cpu == 2 && Addr < 0x1000) return rom_cpu3[Addr];

  // RAM per prima (bring-up #27, perf): e' il grosso degli accessi e le
  // finestre sono disgiunte da tutti gli IO sotto (0x68xx/0x70xx/0x7100/
  // 0xd0xx/0xf000+), quindi l'ordine non cambia la semantica.
  if (Addr >= 0x7800 && Addr <= 0x7fff) return memory[RAM_WORK    + (Addr - 0x7800)];
  if (Addr >= 0x8000 && Addr <= 0x87ff) return memory[RAM_SR1     + (Addr - 0x8000)];
  if (Addr >= 0x9000 && Addr <= 0x97ff) return memory[RAM_SR2     + (Addr - 0x9000)];
  if (Addr >= 0xa000 && Addr <= 0xa7ff) return memory[RAM_SR3     + (Addr - 0xa000)];
  if (Addr >= 0xb000 && Addr <= 0xb7ff) return memory[RAM_FGCOLOR + (Addr - 0xb000)];
  if (Addr >= 0xb800 && Addr <= 0xbfff) return memory[RAM_BGCOLOR + (Addr - 0xb800)];
  if (Addr >= 0xc000 && Addr <= 0xc7ff) return memory[RAM_FGVIDEO + (Addr - 0xc000)];
  if (Addr >= 0xc800 && Addr <= 0xcfff) return memory[RAM_BGVIDEO + (Addr - 0xc800)];

  // dsw: formula ESATTA di bosco_dsw_r() (galaga.cpp), NON lo shortcut
  // bit-reversed/invertito di galaga::rdZ80 — quello aveva causato un
  // hang di boot permanente (vedi harness_cpu1.py/harness_cpu12.py):
  // bit0=(DSWB>>offset)&1, bit1=(DSWA>>offset)&1, nessuna inversione
  if ((Addr & 0xfff8) == 0x6800) {
    unsigned char bit = Addr & 7;
    unsigned char b0 = (XEVIOUS_DSWB >> bit) & 1;
    unsigned char b1 = (XEVIOUS_DSWA >> bit) & 1;
    // la BOMBA (secondo pulsante) NON passa dal 51xx: in MAME e' PORT_BIT
    // 0x01 IPT_BUTTON2 ACTIVE-LOW dentro DSWB (galaga.cpp riga 1228). Il
    // gioco la campiona ogni frame nella ISR (ROM 0x0117 legge 0x6800-07 e
    // impacchetta DSWB in 0x8016) e la consuma a 0x188b: RRCA; RET c
    // (carry=bit0=1 -> niente bomba). Come Scramble (laser+bomba sullo
    // stesso tasto, richiesta utente bring-up #28): FIRE spara e bombarda
    // insieme; il gioco ha comunque il suo cooldown interno (0x7940).
    if (bit == 0 && (input->buttons_get() & BUTTON_FIRE))
      b0 = 0;
    return b0 | (b1 << 1);
  }

  // namco 06xx: control port (status). Bit 0xe0 = trasferimento in corso
  // ("busy"): il gioco, vedendolo, ACCODA i comandi concorrenti invece di
  // sovrascrivere cs_ctrl a meta' trasferimento (0x00e9 AND 0xe0 -> path coda
  // 0x00f8). Bit 0x10 = idle (compat con eventuali busy-wait su bit4).
  // xfer_busy: 1 dal comando di trasferimento fino al deselect (0x10) che il
  // gioco scrive a fine trasferimento (handler NMI 0x006f, BC'=0). Vedi
  // project_xevious.md bring-up #20.
  if (Addr == 0x7100)
    return xfer_busy ? 0xe0 : 0x10;

  // namco 06xx: data port — shortcut identico a galaga::rdZ80 (ignora
  // l'offset reale, serve i byte in sequenza via namco_cnt)
  if (Addr >= 0x7000 && Addr <= 0x70ff) {
    if (cs_ctrl == 0x74) {      // 50xx: lettura protezione (4 byte, il 4o
                                // conta). Vedi prot_param in xevious.h.
      unsigned char seq = namco_cnt < 4 ? namco_cnt : 4;
      namco_cnt++;
      if (seq == 3)
        return (prot_param == 0x80 || prot_param == 0x10) ? 0x05 : 0x95;
      return 0xff;
    }
    if (cs_ctrl & 1) {          // 51xx selezionato: joystick/credito
      if (!credit_mode) {
        unsigned char map71[] = { 0b11111111, 0xff, 0xff };
        if (namco_cnt > 2) return 0xff;
        return map71[namco_cnt++];
      } else {
        static unsigned char prev_mask = 0;
        static unsigned char fire_timer = 0;
        unsigned char keymask = input->buttons_get();

        // Joystick Xevious: il 51xx passa un CODICE di direzione 0..8 nel nibble
        // basso di 0x8019. Derivato DISASSEMBLANDO la ROM reale (guess-free,
        // bring-up #25). Routine nave a 0x1600:
        //   1611 LD A,(0x8019) / 1612 AND 0x0F / 1614 LD HL,0x1633 / 1617 RST 08
        //   (HL+=A*2) / 1618 LD A,(HL)=dx -> (0x7ac6) / 161D LD A,(HL+1)=dy ->
        //   (0x7bc6).  NB: 0x7ac6/0x7bc6 sono RAM di DESTINAZIONE, non le tabelle
        //   (il fix #24 le leggeva come sorgente prendendo 0xFF: era sbagliato).
        // Tabella VERA a 0x1633 (byte dx,dy letti dalla ROM):
        //   idx0 f0 00 (-16,  0) SX      idx1 f0 f0 (-16,-16) SU-SX
        //   idx2 00 e8 (  0,-24) SU      idx3 10 f0 (+16,-16) SU-DX
        //   idx4 10 00 (+16,  0) DX      idx5 10 10 (+16,+16) GIU-DX
        //   idx6 00 18 (  0,+24) GIU     idx7 f0 10 (-16,+16) GIU-SX
        //   idx8 00 00 (  0,  0) NEUTRO  (byte a 0x1643).
        // Quindi i codici vanno 0..8, NON 1..9: il fix #24 era shiftato di +1 e
        // mandava 9 al riposo -> indice 9 = 0x1645 (ed 5b) = SPAZZATURA -> la nave
        // derivava sempre (causa vera del "parte in alto a destra e va da sola").
        // Conferme incrociate dalla ROM: 0x1236 CP 2(SU)/CP 6(GIU); 0x02b5 tratta
        // 0x8019==0xFF come errore 51xx (percio' il neutro DEVE essere 8, non 0xFF).
        // ORIENTAMENTO (bring-up #26/#27): la componente verticale a schermo
        // e' il dx gioco (codici 0/4), quella orizzontale il dy gioco (codici
        // 2/6). Col fix #27 dell'ancora sprite (my = sr-15, prima specchiata
        // 223-sr) il verso orizzontale RESO a schermo si inverte, quindi
        // rispetto al #26 le coppie con componente orizzontale sono scambiate
        // (2<->6, 1<->7, 3<->5):
        //   SU->0  GIU->4  SX->6  DX->2
        //   diagonali: SU-SX->7  SU-DX->1  GIU-SX->5  GIU-DX->3.
        unsigned char up = keymask & BUTTON_UP,   dn = keymask & BUTTON_DOWN;
        unsigned char lf = keymask & BUTTON_LEFT, rt = keymask & BUTTON_RIGHT;
        unsigned char dir = 8;                    // 8 = neutro (0x1633+16 = 00,00)
        if      (up && lf) dir = 7;
        else if (up && rt) dir = 1;
        else if (dn && rt) dir = 3;
        else if (dn && lf) dir = 5;
        else if (lf)       dir = 6;
        else if (up)       dir = 0;
        else if (rt)       dir = 2;
        else if (dn)       dir = 4;

        // FUOCO (zapper), semantica derivata dalla ROM (bring-up #27):
        // - bit 0x20 = livello ACTIVE-LOW: la routine di sparo 0x1787 fa
        //   BIT 5,(HL); finche' e' 0 il gioco spara da solo a cadenza
        //   (contatore 0x8033 ricaricato da 0x8032) -> auto-repeat INTERNO.
        //   I fix #25/#26 non impostavano mai bit5 -> sempre 0 -> autofire
        //   perpetuo, con QUALSIASI polarita' di bit4.
        // - bit 0x10 = ACTIVE-LOW a IMPULSO: usato solo dal name-entry
        //   (0x1207 BIT 4: quando va a 0 conferma la lettera), quindi va
        //   dato solo sul fronte di pressione (a livello confermerebbe
        //   tutte le lettere di fila). Il fronte va rilevato sul tasto
        //   FISICO (input->fire_raw(), pre-autofire): l'autofire globale
        //   pulsa il bit del mask e ogni impulso sarebbe un fronte nuovo
        //   -> una lettera confermata ogni ~80ms tenendo premuto. Zapper
        //   (livello, auto-repeat interno del gioco) e bomba restano sul
        //   mask autofired: comportamento in gioco invariato.
        static unsigned char prev_fire_phys = 0;
        unsigned char fire_phys = input->fire_raw();
        unsigned char fire = 0x30;                    // bit4|bit5 = 1 a riposo
        if (keymask & BUTTON_FIRE) fire &= ~0x20;     // livello (zapper)
        if (fire_phys && !prev_fire_phys) {
          fire &= ~0x10; fire_timer = 1;              // impulso (name-entry)
        } else if (fire_timer) {
          fire &= ~0x10; fire_timer--;
        }
        prev_fire_phys = fire_phys;

        // 3 byte letti dal 51xx in 0x8018-0x801a: credito BCD, P1(dir+fuoco),
        // P2(centro, non usato in 1P ma mai 0xFF)
        unsigned char mapb1[] = {
          (unsigned char)(16 * (credit / 10) + credit % 10),
          (unsigned char)(dir | fire),
          0x38 };   // P2: neutro (dir 8) + bit4/bit5 a 1 (active-low), mai 0xFF

        if ((keymask & BUTTON_START) && !(prev_mask & BUTTON_START) && credit) credit -= 1;
        if ((keymask & BUTTON_COIN) && !(prev_mask & BUTTON_COIN) && (credit < 99)) credit += 1;

        prev_mask = keymask;
        if (namco_cnt > 2) return 0xff;
        return mapb1[namco_cnt++];
      }
    } else if (cs_ctrl & 4) {   // 50xx selezionato: RNG (xorshift, non ciclo-esatto)
      rng_state ^= rng_state << 7;
      rng_state ^= rng_state >> 9;
      rng_state ^= rng_state << 8;
      return (unsigned char)(rng_state & 0xff);
    }
    return 0xff;
  }

  if (Addr >= 0xf000) return xevious_bb_r(Addr & 1);

  return 0xff;
}

void xevious::wrZ80(unsigned short Addr, unsigned char Value) {
  if (Addr < 0x4000) return;   // ROM

  // RAM per prima (bring-up #27, perf): finestre disgiunte da tutti gli IO
  // sotto, l'ordine non cambia la semantica.
  if (Addr >= 0x7800 && Addr <= 0x7fff) { memory[RAM_WORK    + (Addr - 0x7800)] = Value; return; }
  if (Addr >= 0x8000 && Addr <= 0x87ff) { memory[RAM_SR1     + (Addr - 0x8000)] = Value; return; }
  if (Addr >= 0x9000 && Addr <= 0x97ff) { memory[RAM_SR2     + (Addr - 0x9000)] = Value; return; }
  if (Addr >= 0xa000 && Addr <= 0xa7ff) { memory[RAM_SR3     + (Addr - 0xa000)] = Value; return; }
  if (Addr >= 0xb000 && Addr <= 0xb7ff) { memory[RAM_FGCOLOR + (Addr - 0xb000)] = Value; return; }
  if (Addr >= 0xb800 && Addr <= 0xbfff) { memory[RAM_BGCOLOR + (Addr - 0xb800)] = Value; return; }
  if (Addr >= 0xc000 && Addr <= 0xc7ff) { memory[RAM_FGVIDEO + (Addr - 0xc000)] = Value; return; }
  if (Addr >= 0xc800 && Addr <= 0xcfff) { memory[RAM_BGVIDEO + (Addr - 0xc800)] = Value; return; }

  // WSG (suono, 3 voci) — stesso schema di galaga::wrZ80
  if ((Addr & 0xffe0) == 0x6800) {
    soundregs[Addr - 0x6800] = Value & 0x0f;
    return;
  }

  // latch ls259 0x6820-0x6827: bit0=irq1(main) bit1=irq2(sub) bit2=nmi(sub2)
  // bit3=reset sub+sub2 (invertito) + reset 50xx/51xx/54xx
  if ((Addr & 0xfff8) == 0x6820) {
    unsigned char bit = Addr & 7;
    if (bit == 0 || bit == 1 || bit == 2) {
      irq_enable[bit] = Value;
    } else if (bit == 3) {
      sub_cpu_reset = !Value;
      credit_mode = 0;   // reset anche 50xx/51xx (shortcut)
      namco_cnt = 0;
      xfer_busy = 0;     // 06xx idle (evita "busy" bloccato dopo un reset)
      coincredMode = 0;  // contatore argomenti comando coinage 51xx
      if (sub_cpu_reset) {
        // BUG FIX: run_frame() puo' ancora avere StepZ80(&cpu[0]) da eseguire
        // per la stessa iterazione 'i' quando questa scrittura arriva da CPU1
        // (succede prestissimo nel boot, vedi memoria project_xevious.md
        // bring-up #6): senza salvare/ripristinare current_cpu, quegli
        // StepZ80 successivi leggerebbero opcode da xevious_rom_cpu3 invece
        // che cpu1, mandando il PC fuori rotta.
        char saved_cpu = current_cpu;
        current_cpu = 1; ResetZ80(&cpu[1]);
        current_cpu = 2; ResetZ80(&cpu[2]);
        current_cpu = saved_cpu;
      }
    }
    return;
  }

  if (Addr == 0x6830) return;   // watchdog, no-op

  // namco 06xx control port: seleziona il chip (bit0=51xx bit2=50xx
  // bit3=54xx) e arma il busy-delay, stesso schema di galaga::wrZ80
  if (Addr == 0x7100) {
    namco_cnt = 0;
    cs_ctrl = Value;
    // "busy" del 06xx: 1 finche' un trasferimento e' in corso. Un comando di
    // trasferimento (bit 0xe0) lo alza; il deselect 0x10 che il gioco scrive a
    // fine trasferimento (handler NMI 0x006f) lo abbassa. NON serve il conteggio
    // byte (il tentativo di leggerlo dal reg BC era sbagliato: dopo l'EXX del
    // gioco a 0x00f3 il conteggio e' nel BC1 shadow, non nel BC principale —
    // vedi bring-up #20). Questo modello combacia con la semantica reale.
    xfer_busy = (Value & 0xe0) ? 1 : 0;
    nmi_cnt = 0;   // riparte il timing del burst NMI a ogni nuovo comando (#21)
    namco_busy = 5000;
    n54_skip = 0;  // ogni control-write apre un nuovo pacchetto 06xx: il
                   // prossimo byte dati per il 54xx e' un comando, non un
                   // parametro residuo (vedi parser cs_ctrl==0x68 sotto)
    return;
  }

  // namco 06xx data port: comandi 51xx (protocollo reale, dal commento in
  // namco51.cpp): ogni byte-dato e' un comando 0-7 (data & 0x07):
  //   00 nop; 01 = set coinage + N argomenti; 02 = ENTRA in "credit mode" e
  //   abilita i tasti start; 03/04 = remap joystick (ignorato); 05 = "switch
  //   mode"; 06/07 nop.
  // Il commento MAME avverte "xevious ... e' diverso": il suo comando 01
  // consuma 6 argomenti (non 4). Verificato dalla ROM: il setup a108 (0x0430)
  // manda 8 byte 0x8560-0x8567 = "01 <6 valori coinage> 02" (template ROM
  // 0x3d2 = 01 01 01 01 01 04 02 02, posizioni 3-6 sovrascritte coi valori
  // coinage in 0x03ae-0x03c5, byte7=0x02 MAI toccato) -> con 6 argomenti il
  // byte7=0x02 viene letto come comando ed attiva il credito. Il setup a106
  // (0x0237) manda "05 05 05 05 05 05" = switch mode (fase boot/selftest).
  // NB: coincredMode (ereditato da galaga.h) qui e' il contatore argomenti.
  if (Addr >= 0x7000 && Addr <= 0x70ff) {
    if (cs_ctrl == 0x64) {      // 50xx: scrittura parametro protezione
      prot_param = Value;       // (l'ultimo vince; vedi rdZ80 cs_ctrl==0x74)
      namco_cnt++;              // conta il byte per far scadere il "busy" 06xx
      return;
    }
    if (cs_ctrl == 0x68) {      // 54xx: generatore di rumore (esplosioni).
      // Protocollo reale (namco54.cpp MAME): comando a nibble alto —
      // 1x/2x/5x = play suono tipo A/B/C, 3x/4x = set parametri A/B
      // (seguono 4 byte), 6x = set parametri C (seguono 5 byte), 7x =
      // volume C, 0x/8x-Fx = nop. Xevious manda in tutto 3 pacchetti
      // (template in ROM CPU1): 0x15ba/0x28b9 = "30 <4 param> 10 10"
      // (play tipo A, esplosioni nave/grandi) e 0x1a36 = "40 40 40 01 ff
      // 20 20" (play tipo B) inviato dal loop oggetti-a-terra 0x1a00 —
      // UNO PER BERSAGLIO distrutto, quindi anche a raffica ravvicinata.
      namco_cnt++;
      if (n54_skip) { n54_skip--; return; }   // byte parametro, non comando
      switch (Value >> 4) {
        case 1: trigger_sound_explosion(1); break;   // play A -> explo2 (nave)
        case 2: trigger_sound_explosion(0); break;   // play B -> explo1 (a terra)
        case 3:
        case 4: n54_skip = 4; break;
        case 6: n54_skip = 5; break;
        default: break;   // 0x, 5x (tipo C mai parametrizzato qui), 7x, 8x-Fx
      }
      return;
    }
    if (cs_ctrl & 1) {          // 51xx selezionato: interpreta i comandi 0-7
      namco_cnt++;
      if (coincredMode > 0) {   // sto consumando gli argomenti del comando 01
        coincredMode--;
        return;
      }
      switch (Value & 0x07) {
        case 1: coincredMode = 6; break;   // set coinage (XEVIOUS: 6 argomenti)
        case 2: credit_mode = 1; break;    // credit mode + abilita start
        case 5: credit_mode = 0; break;    // switch mode
        default: break;                    // 0,3,4,6,7: nop / remap (ignorati)
      }
      return;
    }
    return;
  }

  // scroll/flip latch 0xd000-0xd07f
  if ((Addr & 0xff80) == 0xd000) {
    unsigned char off = Addr & 0x7f;
    unsigned char reg = (off >> 4) & 0xf;
    unsigned short scroll = Value | ((off & 1) << 8);
    switch (reg) {
      case 0: bg_scrollx = scroll; break;
      case 1: fg_scrollx = scroll; break;
      case 2: bg_scrolly = scroll; break;
      case 3: fg_scrolly = scroll; break;
      default: break;   // reg7 (flip screen) ignorato: nessun supporto cocktail
    }
    return;
  }

  if (Addr >= 0xf000) { xevious_bs_w(Addr & 1, Value); }
}

void xevious::start(void) {
  game_started = 1;

  // Lazy cache DRAM (bring-up #27, ricetta mappy — vedi mappy::reset()):
  // ROM cpu1/2/3 (fetch opZ80/rdZ80), planet-map (CPU2 la martella in gioco
  // via bb_r) e gfx tile + colormap (render). ~58KB in due blocchi; sprite
  // (160KB) volutamente esclusi. Fallback trasparente sulla flash se
  // l'alloc fallisce. Mai liberata (come mappy/phoenix).
  if (!rom_cached) {
    const uint32_t CAPS = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
    const size_t sz_rom = sizeof(xevious_rom_cpu1) + sizeof(xevious_rom_cpu2)
                        + sizeof(xevious_rom_cpu3) + sizeof(xevious_planetmap);
    const size_t sz_gfx = sizeof(xevious_fgtilemap) + sizeof(xevious_bgtilemap)
                        + sizeof(xevious_colormap_fg) + sizeof(xevious_colormap_bg)
                        + sizeof(xevious_colormap_sprites);
    unsigned char *r = (unsigned char *)heap_caps_malloc(sz_rom, CAPS);
    unsigned char *g = (unsigned char *)heap_caps_malloc(sz_gfx, CAPS);
    if (r && g) {
      unsigned char *p = r;
      memcpy(p, xevious_rom_cpu1,  sizeof(xevious_rom_cpu1));  rom_cpu1  = p; p += sizeof(xevious_rom_cpu1);
      memcpy(p, xevious_rom_cpu2,  sizeof(xevious_rom_cpu2));  rom_cpu2  = p; p += sizeof(xevious_rom_cpu2);
      memcpy(p, xevious_rom_cpu3,  sizeof(xevious_rom_cpu3));  rom_cpu3  = p; p += sizeof(xevious_rom_cpu3);
      memcpy(p, xevious_planetmap, sizeof(xevious_planetmap)); planetmap = p;
      p = g;
      memcpy(p, xevious_fgtilemap, sizeof(xevious_fgtilemap));
      fgtiles = (const unsigned char (*)[8])p;   p += sizeof(xevious_fgtilemap);
      memcpy(p, xevious_bgtilemap, sizeof(xevious_bgtilemap));
      bgtiles = (const unsigned short (*)[8])p;  p += sizeof(xevious_bgtilemap);
      memcpy(p, xevious_colormap_fg, sizeof(xevious_colormap_fg));
      cmap_fg = (const unsigned short (*)[2])p;  p += sizeof(xevious_colormap_fg);
      memcpy(p, xevious_colormap_bg, sizeof(xevious_colormap_bg));
      cmap_bg = (const unsigned short (*)[4])p;  p += sizeof(xevious_colormap_bg);
      memcpy(p, xevious_colormap_sprites, sizeof(xevious_colormap_sprites));
      cmap_spr = (const unsigned short (*)[8])p;
      rom_cached = true;
      Serial.printf("[XEVIOUS] ROM+gfx in DRAM (%u+%u byte)\n",
                    (unsigned)sz_rom, (unsigned)sz_gfx);
    } else {
      if (r) heap_caps_free(r);
      if (g) heap_caps_free(g);
      Serial.println(F("[XEVIOUS] alloc DRAM fallita: ROM/gfx dalla flash"));
    }
  }
}

// ============================================================================
// Interleave 3 CPU — scheletro identico a galaga::run_frame() (wiring
// ls259 IDENTICO tra le due macchine: irq1/irq2/nmion su bit0/1/2, stessa
// NMI periodica su CPU1 pilotata da cs_ctrl, stessa NMI periodica su CPU3
// a INST_PER_FRAME/4 e 3*INST_PER_FRAME/4)
// ============================================================================
void xevious::run_frame(void) {
  for (int i = 0; i < INST_PER_FRAME; i++) {
    current_cpu = 0;
    StepZ80(&cpu[0]); StepZ80(&cpu[0]); StepZ80(&cpu[0]); StepZ80(&cpu[0]);
    if (!sub_cpu_reset) {
      current_cpu = 1;
      StepZ80(&cpu[1]); StepZ80(&cpu[1]); StepZ80(&cpu[1]); StepZ80(&cpu[1]);
      current_cpu = 2;
      StepZ80(&cpu[2]); StepZ80(&cpu[2]); StepZ80(&cpu[2]); StepZ80(&cpu[2]);
    }

    if (namco_busy) namco_busy--;

    // NMI di CPU1 = motore di trasferimento del 06xx (handler 0x0066: EXX+LDI,
    // un byte per NMI). Il vero 06xx, alla ricezione di un comando, spara un
    // BURST rapido di NMI (una per byte) cosi' il trasferimento completa PRIMA
    // che la task cooperativa che l'ha richiesto riprenda e legga il risultato.
    // La protezione (0x3b9e) emette cmd 0x74, fa UN solo yield, poi legge subito
    // 0x8063 a 0x3bbc: con la vecchia NMI periodica lenta (periodo 192 iter con
    // cs_ctrl=0x74) i 4 byte non facevano in tempo -> 0x8063 restava 0x00 ->
    // CP 5 fallisce -> reset (bring-up #21). Quindi: mentre un trasferimento e'
    // in corso (xfer_busy) sparo la NMI ogni 8 iterazioni (~32 StepZ80,
    // abbastanza perche' l'handler 0x0066 completi un byte prima del prossimo),
    // altrimenti NMI periodica normale.
    if (xfer_busy) {
      if (nmi_cnt < 8) {
        nmi_cnt++;
      } else {
        current_cpu = 0;
        IntZ80(&cpu[0], INT_NMI);
        nmi_cnt = 0;
      }
    } else if ((cs_ctrl & 0xe0) != 0) {
      if (nmi_cnt < (cs_ctrl >> 5) * 64) {
        nmi_cnt++;
      } else {
        current_cpu = 0;
        IntZ80(&cpu[0], INT_NMI);
        nmi_cnt = 0;
      }
    }

    if (!sub_cpu_reset && !irq_enable[2] &&
        ((i == INST_PER_FRAME / 4) || (i == 3 * INST_PER_FRAME / 4))) {
      current_cpu = 2;
      IntZ80(&cpu[2], INT_NMI);
    }
  }

  if (irq_enable[0]) {
    current_cpu = 0;
    IntZ80(&cpu[0], INT_RST38);
  }
  if (!sub_cpu_reset && irq_enable[1]) {
    current_cpu = 1;
    IntZ80(&cpu[1], INT_RST38);
  }
}

// ============================================================================
// Sprite — 3 blocchi RAM paralleli (sr1=Y/X, sr2=size/flip, sr3=code/color),
// formule di posizione/composizione ESATTE da draw_sprites() (xevious.cpp).
// Rotazione ROT90: Xevious e' ROT90 come Galaga (il framebuffer raw e'
// landscape e MAME applica ROT90): asse MAME corto (224, "sy" nel sorgente
// reale) -> X portrait RIBALTATO (XP = C - sy, stessa forma della tilemap,
// ancorata dal testo leggibile su HW); asse MAME largo (288, "sx") -> Y
// portrait diretto (solo offset additivo, gia' incluso nella formula mx).
// ============================================================================
void xevious::prepare_frame(void) {
  active_sprites = 0;
  unsigned char *sr1 = memory + RAM_SR1 + 0x780;
  unsigned char *sr2 = memory + RAM_SR2 + 0x780;
  unsigned char *sr3 = memory + RAM_SR3 + 0x780;

  for (int offs = 0; offs < 0x80 && active_sprites < 120; offs += 2) {
    if (sr3[offs + 1] & 0x40) continue;   // sprite disabilitato

    unsigned short code = (sr2[offs] & 0x80) ? ((sr3[offs] & 0x3f) + 0x100) : sr3[offs];
    unsigned char color = sr3[offs + 1] & 0x7f;
    bool flipx = sr2[offs] & 4;
    bool flipy = sr2[offs] & 8;
    short mx = sr1[offs + 1] - 40 + 0x100 * (sr2[offs + 1] & 1);   // asse MAME largo (288)
    // asse MAME corto (224): la tilemap (ancorata dal testo leggibile su HW)
    // implementa XP = C - Yraw (ROT90 come MAME); MAME disegna lo sprite a
    // Yraw = sy = 223 - sr, quindi il box in portrait parte da
    // XP = 223 - (sy+15) = sr - 15. La vecchia formula 223 - sr era la forma
    // OPPOSTA (+Yraw): posizioni sprite SPECCHIATE sull'asse corto rispetto
    // al terreno (contenuto del singolo sprite invece gia' corretto, percio'
    // invisibile sugli sprite simmetrici) e quadranti dei doppi scambiati
    // (bring-up #27, verificato offline componendo gli sprite reali:
    // romconv/xevious/explo_truth_vs_current.png).
    short my = sr1[offs] - 15;

    unsigned char szflags = sr2[offs];

    // push di un singolo blocco 16x16, con rotazione X<-my Y<-mx
    #define XEV_PUSH(c, pmx, pmy) do { \
      if (active_sprites < 120) { \
        short px = (pmy), py = (pmx); \
        if (px > -16 && px < 224 && py > -16 && py < 288) { \
          sprite_S s; \
          s.x = px; s.y = py; \
          s.code = (c) & 0xff; s.color_block = ((c) >> 8) & 1; \
          s.color = color; \
          s.flags = (flipx ? 1 : 0) | (flipy ? 2 : 0); \
          sprite[active_sprites++] = s; \
        } \
      } \
    } while (0)

    if (szflags & 2) {               // doppia altezza
      unsigned short base = code;
      if (szflags & 1) {              // + doppia larghezza -> 2x2
        base &= ~3;
        XEV_PUSH(base + 3, flipx ? mx : mx + 16, flipy ? my + 16 : my);
        XEV_PUSH(base + 1, flipx ? mx : mx + 16, flipy ? my : my + 16);
      }
      base &= ~2;
      XEV_PUSH(base + 2, flipx ? mx + 16 : mx, flipy ? my + 16 : my);
      XEV_PUSH(base,     flipx ? mx + 16 : mx, flipy ? my : my + 16);
    } else if (szflags & 1) {        // doppia larghezza soltanto
      unsigned short base = code & ~1;
      XEV_PUSH(base,     flipx ? mx + 16 : mx, flipy ? my + 16 : my);
      XEV_PUSH(base + 1, flipx ? mx : mx + 16, flipy ? my + 16 : my);
    } else {                          // normale
      XEV_PUSH(code, mx, my);
    }
    #undef XEV_PUSH
  }
}

void xevious::blit_sprite(short row, unsigned char s) {
  unsigned short code = sprite[s].code | ((unsigned short)sprite[s].color_block << 8);
  const unsigned long *spr = xevious_sprites[sprite[s].flags & 3][code];
  const unsigned short *colors = cmap_spr[sprite[s].color & 0x3f];

  short y_offset = sprite[s].y - 8 * row;
  unsigned char lines2draw = 8;
  if (y_offset < -8) lines2draw = 16 + y_offset;
  unsigned short startline = 0;
  if (y_offset > 0) { startline = y_offset; lines2draw = 8 - y_offset; }
  if (y_offset < 0) spr += 2 * (-y_offset);

  short x = sprite[s].x;
  unsigned char c0 = (x < 0) ? -x : 0;
  unsigned char c1 = (x > 224 - 16) ? 224 - x : 16;

  unsigned short *ptr = frame_buffer + x + 224 * startline;
  for (unsigned char r = 0; r < lines2draw; r++, ptr += 224, spr += 2) {
    for (unsigned char c = c0; c < c1; c++) {
      unsigned short col = colors[(spr[c >> 3] >> (4 * (c & 7))) & 0x0f];
      if (col) ptr[c] = col;
    }
  }
}

// ============================================================================
// Tilemap fg/bg — scroll a 2 assi indipendenti. Convenzione ROT90 identica
// agli sprite: asse riga-banda (portrait Y-pixel, 0-287) <- scrollx MAME
// (asse largo 288, ricalcolato per-scanline, Phoenix-style); asse colonna
// (portrait X-pixel, 0-223) <- scrolly MAME (asse corto 224, avanzamento
// incrementale coarse/fine per pixel, mappy-style). scrolldx/dy fissi da
// video_start() (xevious.cpp): bg(-20,-16) fg(-32,-18), SOLO caso non-flip
// (nessun supporto cocktail in questo progetto).
// ============================================================================
void xevious::blit_tilemap_row(short row, bool bg) {
  unsigned short scrollx = bg ? bg_scrollx : fg_scrollx;
  unsigned short scrolly = bg ? bg_scrolly : fg_scrolly;
  // offset MAME (scrolldx/scrolldy da video_start()). Convenzione MAME:
  // tile = schermo + scroll - scrolldx, quindi si SOTTRAGGONO (dx/dy sotto
  // sono gia' i valori scrolldx/scrolldy di MAME: bg(-20,-16) fg(-32,-18)).
  short dx = bg ? -20 : -32;
  // asse corto (X portrait) RIBALTATO (mame_y decresce con col). Il termine
  // scrolldy di MAME va SOTTRATTO come per dx (tile = schermo + scroll - dy):
  // il gioco tiene scrolly=0 fisso (CPU2 0x02c6-0x02d0 scrive sempre 0 in
  // 0x800e), quindi senza il -dy tutto il quadro risulta spostato di 16px
  // (18 per l'fg) verso sinistra rispetto al layout pensato per l'hardware
  // reale (bring-up #27). Il fix #13b lo aveva tolto basandosi sul dump della
  // schermata di boot, dove le righe tile 28-31 vuote mascheravano l'offset:
  // la banda nera di 16px sul bordo in quella schermata e' fedele a MAME
  // (overscan del CRT originale).
  short dy = bg ? -16 : -18;
  const unsigned char *videoram = memory + (bg ? RAM_BGVIDEO : RAM_FGVIDEO);
  const unsigned char *colorram = memory + (bg ? RAM_BGCOLOR : RAM_FGCOLOR);

  for (char ry = 0; ry < 8; ry++) {
    // asse riga-banda: fisso per l'intera scanline (mame_x = schermo - scrolldx)
    unsigned short mame_x = (unsigned short)(row * 8 + ry + scrollx - dx) & 0x1ff;
    unsigned char tile_col = (mame_x >> 3) & 63;
    unsigned char x_fine = mame_x & 7;

    unsigned short *ptr = frame_buffer + ry * 224;

    // asse colonna (portrait X) ribaltato: mame_y parte da scrolly - dy + 223
    // (dy negativo -> +16 bg / +18 fg) e decresce con col.
    unsigned short mame_y = (unsigned short)(scrolly - dy + 223) & 0xff;
    unsigned char tile_row = (mame_y >> 3) & 31;
    unsigned char y_fine = mame_y & 7;

    // Loop a RUN per-tile (bring-up #27, perf: prima faceva 2 lookup flash
    // 2D per PIXEL): fetch del bitmap e del colormap UNA volta per tile,
    // poi 1..8 pixel con solo shift+mask. Semantica identica al vecchio
    // loop per-pixel (y_fine scende da yf a 0, poi tile successivo verso
    // mame_y decrescente). Il bitmap e' pre-ruotato dal converter
    // (rot_galagino: out[y][x]=mame[N-1-x][y]) — si legge riga=sx (da
    // x_fine, costante su tutta la scanline), bit=sy=7-y_fine (senso di
    // flip_y invertito di conseguenza).
    short col = 0;
    while (col < 224) {
      unsigned short tile_index = tile_row * 64 + tile_col;
      unsigned char code = videoram[tile_index];
      unsigned char attr = colorram[tile_index];
      unsigned char sx = (attr & 0x40) ? (7 - x_fine) : x_fine;   // flip_x
      bool flip_y = attr & 0x80;
      unsigned char run = y_fine + 1;             // pixel restanti nel tile
      if (run > 224 - col) run = 224 - col;
      col += run;

      if (bg) {
        unsigned short tile_code = code | ((attr & 1) << 8);
        unsigned char color = ((attr & 0x3c) >> 2) | ((code & 0x80) >> 3) | ((attr & 3) << 5);
        unsigned short packed = bgtiles[tile_code][sx];
        const unsigned short *cmap = cmap_bg[color & 0x7f];
        if (!flip_y) {                            // sy = 7-y_fine, cresce
          unsigned char sh = 2 * (7 - y_fine);
          for (unsigned char i = 0; i < run; i++, sh += 2)
            *ptr++ = cmap[(packed >> sh) & 3];
        } else {                                  // sy = y_fine, decresce
          unsigned char sh = 2 * y_fine;
          for (unsigned char i = 0; i < run; i++, sh -= 2)
            *ptr++ = cmap[(packed >> sh) & 3];
        }
      } else {
        unsigned char bits = fgtiles[code][sx];
        if (!bits) {
          ptr += run;   // char vuoto su questa scanline (pen0 trasparente)
        } else {
          unsigned char color = ((attr & 3) << 4) | ((attr & 0x3c) >> 2);
          unsigned short c1 = cmap_fg[color & 0x3f][1];
          // c1==0 possibile (pen1 nero->trasparente): identico al vecchio
          // "if (c)", il pixel resta com'e'
          if (!flip_y) {
            unsigned char sh = 7 - y_fine;
            for (unsigned char i = 0; i < run; i++, sh++, ptr++)
              if (c1 && ((bits >> sh) & 1)) *ptr = c1;
          } else {
            unsigned char sh = y_fine;
            for (unsigned char i = 0; i < run; i++, sh--, ptr++)
              if (c1 && ((bits >> sh) & 1)) *ptr = c1;
          }
        }
      }

      // cella successiva (verso mame_y decrescente), riparte dal bit alto
      tile_row = (tile_row - 1) & 31;
      y_fine = 7;
    }
  }
}

void xevious::render_row(short row) {
  blit_tilemap_row(row, true);    // sfondo, opaco

  for (unsigned char s = 0; s < active_sprites; s++)
    if ((sprite[s].y < 8 * (row + 1)) && ((sprite[s].y + 16) > 8 * row))
      blit_sprite(row, s);

  blit_tilemap_row(row, false);   // testo, trasparente (pen0)
}

// ============================================================================
// Suono — WSG 3 voci gia' gestito genericamente da audio.cpp
// (namco_render_buffer(), hasNamcoAudio()). Esplosioni: campioni PCM
// (explo1/explo2.wav, gli stessi che MAME usa per il bootleg Battles privo
// del 54xx), innescati dai comandi "play" reali del 54xx decodificati in
// wrZ80 (cs_ctrl==0x68). Come in Battles (noise_sound_w, xevious_m.cpp:
// riparte solo quando il rumore precedente e' spento) un'esplosione in
// corso NON viene fatta ripartire: i bersagli a terra esplodono a raffica
// (un pacchetto 54xx per oggetto, e ogni pacchetto contiene DUE play) e
// riavviare il campione a ogni play produce solo gracchi. L'esplosione
// nave (tipo A, rara) ha priorita' e puo' interrompere quella a terra.
// ============================================================================
void xevious::trigger_sound_explosion(unsigned char ship) {
  if (snd_boom_cnt && (snd_boom_ship || !ship)) return;   // gia' in corso
  snd_boom_ship = ship;
  if (ship) {
    snd_boom_cnt = sizeof(xevious_sample_boom2);   // 1 byte/campione a 24kHz
    snd_boom_ptr = (const signed char*)xevious_sample_boom2;
  } else {
    snd_boom_cnt = sizeof(xevious_sample_boom);
    snd_boom_ptr = (const signed char*)xevious_sample_boom;
  }
}

const signed char *xevious::waveRom(unsigned char value) {
  return xevious_wavetable[value];
}

const unsigned short *xevious::logo(void) {
  return xevious_logo;
}
