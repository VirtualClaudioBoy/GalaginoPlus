#ifndef VANGUARD_H
#define VANGUARD_H

#include "vanguard_rom.h"
#include "vanguard_gfx.h"
#include "vanguard_proms.h"
#include "vanguard_sound_rom.h"
#include "vanguard_logo.h"
#include "vanguard_dipswitches.h"
#include "../../cpus/m6502/m6502.h"
#include "../machineBase.h"

class vanguard : public machineBase {
public:
  vanguard() { memset(&m_cpu, 0, sizeof(m_cpu)); }
  signed char machineType() override { return MCH_VANGUARD; }
  void start() override;
  void reset() override;
  void run_frame() override;
  void render_row(short row) override;
  const unsigned short *logo() override { return vanguard_logo; }
  unsigned char vanguardSoundRom(unsigned short addr) override { return vanguard_sound_rom[addr & 0x0fff]; }
  bool vanguardMusic0Muted() override { return music0_muted; }
  void vanguardMusic0Ended() override { music0_muted=true; }
  bool vanguardMusic1Muted() override { return music1_muted; }
  const signed char *vanguardSample(unsigned char index) override;
  unsigned long vanguardSampleLength(unsigned char index) override;
  unsigned char vanguardSampleDivider(unsigned char index) override;
  const char *hiscoreKey() override { return "vanguard"; }
  const hiscore_region_S *hiscoreRegions(unsigned char *count) override;
  unsigned char hiscoreRead(unsigned short addr) override { return memory[addr]; }
  void hiscoreWrite(unsigned short addr,unsigned char value) override { memory[addr]=value; }

private:
  static uint8_t main_read(m6502_t *, uint16_t);
  static void main_write(m6502_t *, uint16_t, uint8_t);
  uint16_t pen(unsigned char prom) const;

  m6502_t m_cpu;
  unsigned char *work_ram = nullptr, *fg_ram = nullptr, *bg_ram = nullptr;
  unsigned char *color_ram = nullptr, *char_ram = nullptr;
  unsigned short palette[64] = {};
  unsigned char scroll_x = 0, scroll_y = 0, backcolor = 0, charbank = 0, flip_screen = 0;
  unsigned char fire_direction = 0;
  volatile bool music0_muted = true, music1_muted = true;
  bool coin_down = false;
  unsigned char speech_cmd = 0, speech_data_bytes = 0;
  unsigned long speech_addr = 0;
};
#endif
