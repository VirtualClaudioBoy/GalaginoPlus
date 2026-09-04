#ifndef MACHINEBASE_H
#define MACHINEBASE_H

#include "Arduino.h"
#include "../cpus/z80/Z80.h"
#include "../cpus/i8048/i8048.h"
#include "../emulation/input.h"

#ifdef LED_PIN
#include <FastLED.h>
#define NUM_LEDS  7

#define LED_BLACK    CRGB::Black
#define LED_RED      CRGB::Red
#define LED_GREEN    CRGB::Green
#define LED_BLUE     CRGB::Blue
#define LED_YELLOW   CRGB::Yellow
#define LED_MAGENTA  CRGB::Magenta
#define LED_CYAN     CRGB::Cyan
#define LED_WHITE    CRGB::White
#endif

#define RAMSIZE     20480 // max usage is Xevious with 16.384 (8 finestre RAM da 0x800: work+3 sprite block+fg/bg videoram/colorram), 4096 byte di margine

// Persistent high score support. Regions are taken from MAME's hiscore.dat:
// addr/len describe a CPU-address range holding the high score table.
// start_byte/end_byte are the values the first/last byte of the range hold
// once the game has finished initializing it (used to know when it is safe
// to inject the saved scores).
struct hiscore_region_S {
  unsigned short addr;
  unsigned short len;
  unsigned char start_byte;
  unsigned char end_byte;
};

struct sprite_S {
  int x, y;
  unsigned char code;
  unsigned char color;
  unsigned char color_block;
  unsigned char flags;
  unsigned char priority;

  // flags
  unsigned char is_32x32  : 1; 
  unsigned char flip_x    : 1;
  unsigned char flip_y    : 1;
  unsigned char reserved  : 5;
}; 

enum {
  MCH_MENU = 0,
  MCH_1942,
  MCH_ALIBABA,
  MCH_AMIDAR,
  MCH_ANTEATER,
  MCH_BAGMAN,
  MCH_BNJ,
  MCH_BOMBJACK,
  MCH_BTIME,
  MCH_CIRCUSC,
  MCH_CRUSH,
  MCH_DIGDUG,
  MCH_DKONG,
  MCH_DKONGJR,
  MCH_DKONG3,
  MCH_EYES,
  MCH_FANTASY,
  MCH_FROGGER,
  MCH_GALAGA,
  MCH_GAPLUS,
  MCH_GALAXIAN,
  MCH_GYRUSS,
  MCH_LADYBUG,
  MCH_LIZWIZ,
  MCH_MOONCRESTA,
  MCH_MRTNT,
  MCH_MAPPY,
  MCH_MRDO,
  MCH_MSPACMAN,
  MCH_NIBBLER,
  MCH_PACMAN,
  MCH_PENGO,
  MCH_PHOENIX,
  MCH_PBACTION,
  MCH_POOYAN,
  MCH_ROCNROPE,
  MCH_SCRAMBLE,
  MCH_SPACE,
  MCH_STARFORCE,
  MCH_SUPERCOBRA,
  MCH_THEGLOB,
  MCH_TIMEPLT,
  MCH_TODRUAGA,
  MCH_TURTLES,
  MCH_TUTANKHM,
  MCH_VANVAN,
  MCH_XEVIOUS,
  MCH_SCREGG,
  MCH_VANGUARD
};

// one inst at 3Mhz ~ 500k inst/sec = 500000/60 inst per frame
#define INST_PER_FRAME 300000/60/4 //=1250

#ifdef LED_PIN
  typedef const CRGB (*MenuLedType)[12][NUM_LEDS];
#endif

class machineBase
{
public:
    machineBase() { }
    virtual ~machineBase() { }

    virtual void init(Input *input, unsigned short *framebuffer, sprite_S *spritebuffer, unsigned char *memorybuffer) {
      this->input = input;
      this->frame_buffer = framebuffer; 
      this->sprite = spritebuffer;
      this->memory = memorybuffer;
      memset(soundregs, 0, sizeof(soundregs)); 
     }

    virtual void start() { }
    virtual bool audioAttractMute() { return false; }
    virtual void reset() {
      for(current_cpu = 0; current_cpu < sizeof(cpu) / sizeof(Z80); current_cpu++) {
        ResetZ80(&cpu[current_cpu]);
        irq_enable[current_cpu] = 0;
      }

      memset(memory, 0, RAMSIZE);
      memset(soundregs, 0, sizeof(soundregs)); 

      for (int chip = 0; chip < 3; chip++) {
        for (int c = 0; c < 4; c++) {
          sn_period[chip][c] = 0;
          sn_raw_period[chip][c] = 0;
          sn_volume[chip][c] = 15; // Muto
          sn_hold[chip][c] = 0;
          sn_min_volume[chip][c] = 15;
        }
      }
      current_cpu = 0;
      game_started = 0;
    }

    virtual signed char machineType() { return MCH_MENU; } 
    virtual signed char videoFlipY() { return 0; } 
    virtual signed char videoFlipX() { return 0; } 
    virtual signed char useVideoHalfRate() { return 0; } 
    
    virtual unsigned char rdZ80(unsigned short Addr) { return 0xff; }
    virtual void wrZ80(unsigned short Addr, unsigned char Value) { };
    virtual void outZ80(unsigned short Port, unsigned char Value) { };
    virtual unsigned char opZ80(unsigned short Addr) { return 0x00; }
    virtual unsigned char inZ80(unsigned short Port) { return 0x00; }

    virtual void wrI8048_port(struct i8048_state_S *state, unsigned char port, unsigned char pos) { }
    virtual unsigned char rdI8048_port(struct i8048_state_S *state, unsigned char port) { return 0x00; };
    virtual unsigned char rdI8048_xdm(struct i8048_state_S *state, unsigned char addr)  { return 0x00; };
    virtual unsigned char rdI8048_rom(struct i8048_state_S *state, unsigned short addr) { return 0x00; };

    virtual unsigned char m6809_read(m6809_state *s, uint16_t addr) { return 0x00; }
    virtual void m6809_write(m6809_state *s, uint16_t addr, uint8_t val) { }
    virtual unsigned char m6809_read_opcode(m6809_state *s, uint16_t addr) { return 0x00; }

    virtual void run_frame(void) { };
    virtual void prepare_frame(void) { };
    virtual void render_row(short row) { };
    
    // persistent high scores (see hiscore_region_S above).
    // hiscoreKey: NVS key of the game, NULL = high scores not supported.
    // hiscoreRead/Write access the CPU address space; the Z80 default fits
    // most machines, others (e.g. M6809 based) override them.
    virtual const char *hiscoreKey() { return 0; }
    virtual const struct hiscore_region_S *hiscoreRegions(unsigned char *count) { *count = 0; return 0; }
    virtual unsigned char hiscoreRead(unsigned short addr) { return rdZ80(addr); }
    virtual void hiscoreWrite(unsigned short addr, unsigned char value) { wrZ80(addr, value); }
    // called after the saved scores have been injected. For games that draw
    // the top score only once at boot (before the injection), redraw it here.
    virtual void hiscoreRestored() { }

    virtual const signed char *waveRom(unsigned char value) { return 0; }
    virtual unsigned char vanguardSoundRom(unsigned short addr) { return 0xff; }
    virtual bool vanguardMusic0Muted() { return true; }
    virtual void vanguardMusic0Ended() { }
    virtual bool vanguardMusic1Muted() { return true; }
    virtual bool vanguardMusic2Muted() { return true; }
    virtual const signed char *vanguardSample(unsigned char index) { return 0; }
    virtual unsigned long vanguardSampleLength(unsigned char index) { return 0; }
    virtual unsigned char vanguardSampleDivider(unsigned char index) { return 1; }
    virtual const unsigned short *logo(void) { return 0; };
    virtual bool hasNamcoAudio() { return false; }
    // WSG 15XX a 8 voci (Mappy): layout registri diverso dal WSG 3 voci
    // pacman/galaga. namcoSoundEnabled = mainlatch Q3 (0 = muto).
    virtual bool hasNamco15xxAudio() { return false; }
    virtual bool namcoSoundEnabled() { return true; }
    // Sample-MCU drums (Gyruss i8039): called once per 24kHz output sample by
    // the audio renderer; steps the MCU and returns the centered DAC value
    // (-128..127), 0 = silence. Virtual so audio.cpp does not need to include
    // the machine header (machines.h/gyruss.h can only live in main.cpp).
    virtual int renderDrumSample() { return 0; }
#ifdef LED_PIN
    virtual void menuLeds(CRGB *leds) { memcpy(leds, menu_leds, NUM_LEDS*sizeof(CRGB)); };
    virtual void gameLeds(CRGB *leds) { memcpy(leds, menu_leds, NUM_LEDS*sizeof(CRGB)); };
#endif
    char game_started;	
    unsigned char soundregs[80];
    
    //Mr.Do!
    int sn_period[3][4] = {{0, 0, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}};    // 4 canali per chip (3 tono + 1 rumore)
    int sn_volume[3][4] = {{0, 0, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}};

    //Ladybug
    int sn_min_volume[3][4]; // latched min volume per audio render cycle
    int sn_hold[3][4];       // hold counter: keep sound active for N render cycles

    // Circus Charlie: periodo grezzo a 10 bit del SN76489 (i write arrivano
    // in due meta': serve l'ultimo valore NON riscalato per ricomporlo,
    // perche' sn_period viene salvato gia' scalato al clock del chip)
    int sn_raw_period[3][4] = {{0, 0, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}};
    
protected:
    virtual void blit_tile(short row, char col) { }
    virtual void blit_sprite(short row, unsigned char s) { }
	
    Input *input;
    Z80 cpu[3];
    char irq_enable[3];
    char current_cpu;
    unsigned char irq_ptr;

    int active_sprites;
    sprite_S *sprite;
    unsigned short *frame_buffer;
    unsigned char *memory;

private:	
#ifdef LED_PIN
    const CRGB menu_leds[7] = { LED_BLACK, LED_BLACK, LED_BLACK, LED_BLACK, LED_BLACK, LED_BLACK, LED_BLACK };
#endif
};

#endif
