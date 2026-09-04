#ifndef FANTASY_H
#define FANTASY_H

#include "fantasy_rom.h"
#include "fantasy_gfx.h"
#include "fantasy_proms.h"
#include "fantasy_sound_rom.h"
#include "fantasy_logo.h"
#include "fantasy_dipswitches.h"
#include "../../cpus/m6502/m6502.h"
#include "../machineBase.h"

class fantasy : public machineBase {
public:
  fantasy() { memset(&m_cpu, 0, sizeof(m_cpu)); }
  signed char machineType() override { return MCH_FANTASY; }
  void start() override;
  void reset() override;
  void run_frame() override;
  void render_row(short row) override;
  const unsigned short *logo() override { return fantasy_logo; }
  unsigned char vanguardSoundRom(unsigned short addr) override {
    return addr < sizeof(fantasy_sound_rom) ? fantasy_sound_rom[addr] : 0xff;
  }
  bool vanguardMusic0Muted() override { return music_muted[0]; }
  bool vanguardMusic1Muted() override { return music_muted[1]; }
  bool vanguardMusic2Muted() override { return music_muted[2]; }
  const signed char *vanguardSample(unsigned char index) override;
  unsigned long vanguardSampleLength(unsigned char index) override;
  unsigned char vanguardSampleDivider(unsigned char index) override;
  const char *hiscoreKey() override { return "fantasy"; }
  const hiscore_region_S *hiscoreRegions(unsigned char *count) override;
  unsigned char hiscoreRead(unsigned short addr) override { return memory[addr]; }
  void hiscoreWrite(unsigned short addr,unsigned char value) override { memory[addr]=value; }

private:
  static uint8_t main_read(m6502_t *, uint16_t);
  static void main_write(m6502_t *, uint16_t, uint8_t);
  void set_music_level(unsigned char channel, bool enabled);
  void speech_write(unsigned char value);
  uint16_t pen(unsigned char prom) const;

  m6502_t m_cpu;
  unsigned char *fg_ram = nullptr, *bg_ram = nullptr, *color_ram = nullptr, *char_ram = nullptr;
  unsigned short palette[64] = {};
  unsigned char scroll_x = 0, scroll_y = 0, backcolor = 0, charbank = 0, flip_screen = 0;
  volatile bool music_muted[3] = {true, true, true};
  bool coin_down = false;
  unsigned char speech_cmd = 0, speech_data_bytes = 0;
  unsigned long speech_addr = 0;
};

#endif
