#include "vanguard.h"
#include "vanguard_samples.h"

static_assert(sizeof(vanguard_rom) == 0x8000, "Unexpected Vanguard program ROM size");
static_assert(sizeof(vanguard_gfx) == 0x1000, "Unexpected Vanguard graphics ROM size");
static_assert(sizeof(vanguard_proms) == 0x40, "Unexpected Vanguard palette PROM size");
static_assert(sizeof(vanguard_sound_rom) == 0x1000, "Unexpected Vanguard sound ROM size");
static_assert(VANGUARD_SAMPLE_COUNT == 18, "Unexpected Vanguard sample count");

// MAME hiscore.dat, shared by the Vanguard sets.
const hiscore_region_S *vanguard::hiscoreRegions(unsigned char *count) {
  static const hiscore_region_S regions[] = {
    {0x0220,0x0070,0x11,0x30},
    {0x0025,0x0003,0x00,0x00}
  };
  *count=sizeof(regions)/sizeof(regions[0]);
  return regions;
}

const signed char *vanguard::vanguardSample(unsigned char index) {
  return index < VANGUARD_SAMPLE_COUNT ?
    reinterpret_cast<const signed char *>(vanguard_samples + vanguard_sample_offsets[index]) : nullptr;
}

unsigned long vanguard::vanguardSampleLength(unsigned char index) {
  return index < VANGUARD_SAMPLE_COUNT ? vanguard_sample_lengths[index] : 0;
}

unsigned char vanguard::vanguardSampleDivider(unsigned char index) {
  return index < VANGUARD_SAMPLE_COUNT ? vanguard_sample_dividers[index] : 1;
}

uint16_t vanguard::pen(unsigned char p) const {
  unsigned char r = 0x21*(p&1) + 0x47*((p>>1)&1) + 0x97*((p>>2)&1);
  unsigned char g = 0x21*((p>>3)&1) + 0x47*((p>>4)&1) + 0x97*((p>>5)&1);
  unsigned char b = 0x52*((p>>6)&1) + 0xad*((p>>7)&1);
  uint16_t rgb = ((r&0xf8)<<8) | ((g&0xfc)<<3) | (b>>3);
  return (rgb>>8) | (rgb<<8);
}

void vanguard::start() {
  work_ram=memory; fg_ram=memory+0x400; bg_ram=memory+0x800;
  color_ram=memory+0xc00; char_ram=memory+0x1000;
  for (int i=0;i<64;i++) palette[i]=pen(vanguard_proms[i]);
  m_cpu.read=main_read; m_cpu.write=main_write; m_cpu.fetch=nullptr; m_cpu.user=this;
  reset();
}

void vanguard::reset() {
  machineBase::reset();
  work_ram=memory; fg_ram=memory+0x400; bg_ram=memory+0x800;
  color_ram=memory+0xc00; char_ram=memory+0x1000;
  scroll_x=scroll_y=backcolor=flip_screen=0; charbank=1; fire_direction=0; coin_down=false;
  music0_muted=music1_muted=true;
  speech_cmd=speech_data_bytes=0; speech_addr=0;
  if (m_cpu.read) m6502_reset(&m_cpu);
}

uint8_t vanguard::main_read(m6502_t *cpu, uint16_t a) {
  vanguard *s=static_cast<vanguard*>(cpu->user);
  if (a<0x2000) return s->memory[a];
  if (a>=0x4000 && a<0xc000) return vanguard_rom[a-0x4000];
  if (a>=0xf000) return vanguard_rom[0x4000+(a&0xfff)];
  unsigned char k=s->input->buttons_get();
  if (a==0x3104) {
    unsigned char v=0;
    if(k&BUTTON_DOWN)v|=0x10; if(k&BUTTON_UP)v|=0x20;
    if(k&BUTTON_RIGHT)v|=0x40; if(k&BUTTON_LEFT)v|=0x80;
    // The original cabinet has four directional fire buttons.  Asserting
    // them together makes the game prioritise fire-down, so the single
    // Galagino FIRE button cycles through the four independent lines.
    if(k&BUTTON_FIRE) v|=(1u << s->fire_direction);
    return v;
  }
  if (a==0x3105) return 0;
  if (a==0x3106) return VANGUARD_DSW;
  if (a==0x3107) return ((k&BUTTON_COIN)?2:0)|((s->music0_muted)?0x10:0)|((k&BUTTON_START)?0x80:0);
  return 0xff;
}

void vanguard::main_write(m6502_t *cpu, uint16_t a, uint8_t v) {
  vanguard *s=static_cast<vanguard*>(cpu->user);
  if(a<0x2000){s->memory[a]=v;return;}
  if(a>=0x3100&&a<=0x3102){
    s->soundregs[a-0x3100]=v;
    s->soundregs[5+(a-0x3100)]++; // preserve write strobes for the asynchronous audio task
    if(a==0x3100){
      if(v&0x08)s->music0_muted=true;
      if(v&0x10){
        // The original unmute_channel() restarts the 256-byte tune only when
        // the channel was muted. Repeated unmute writes must not rewind it.
        bool restart=s->music0_muted;
        s->music0_muted=false;
        if(restart)s->soundregs[8]++;
      }
    }else if(a==0x3101){
      // Channel 1 uses bit 3 as a level. Preserve an unmute event even if a
      // later mute write arrives before the asynchronous audio task runs.
      if(v&0x08){
        if(s->music1_muted)s->soundregs[9]++;
        s->music1_muted=false;
      }else s->music1_muted=true;
    }
    return;
  }
  if(a==0x3103){s->backcolor=v&7;s->charbank=(~v>>3)&1;s->flip_screen=v&0x80;return;}
  if(a==0x3200){s->scroll_x=v;return;} if(a==0x3300){s->scroll_y=v;return;}
  if(a==0x3400 && (v&0x30)==0x30) {
    static const unsigned long speech_table[16] = {
      0x04000,0x04325,0x044a2,0x045b7,0x046ee,0x04838,0x04984,0x04b01,
      0x04c38,0x04de6,0x04f43,0x05048,0x05160,0x05289,0x0539e,0x054ce
    };
    unsigned char data=v&0x0f;
    if(s->speech_cmd==2) { // HD38880 ADSET: five address nibbles, least significant first
      s->speech_addr|=((unsigned long)data)<<(s->speech_data_bytes++*4);
      if(s->speech_data_bytes==5)s->speech_cmd=0;
    } else if(s->speech_cmd==4 || s->speech_cmd==6 || s->speech_cmd==8) {
      s->speech_cmd=0; // INT1, INT2 and SYSPD each consume one parameter nibble
    } else if(data==2) {
      s->speech_cmd=2;s->speech_addr=0;s->speech_data_bytes=0;
    } else if(data==4 || data==6 || data==8) {
      s->speech_cmd=data;
    } else if(data==12 && s->speech_data_bytes==5) { // START
      for(unsigned char i=0;i<16;i++)if(speech_table[i]==s->speech_addr){
        s->soundregs[3]=i;s->soundregs[4]++;break;
      }
    } else if(data==10) { // STOP
      s->soundregs[3]=0xff;s->soundregs[4]++;
    }
    return;
  }
}

void vanguard::run_frame() {
  unsigned char k=input->buttons_get(); bool coin=(k&BUTTON_COIN)!=0;
  if(input->fire_raw()) fire_direction=(fire_direction+1)&3;
  else fire_direction=0;
  if(coin&&!coin_down)m_cpu.nmi=1; coin_down=coin;
  m6502_exec(&m_cpu,23520); // 11.289 MHz / 8 / 60 Hz
  m_cpu.irq=1; m6502_exec(&m_cpu,8); m_cpu.irq=0;
  if(!game_started)game_started=1;
}

void vanguard::render_row(short strip) {
  for(int oy=0;oy<8;oy++){
    int py=strip*8+oy-16; if(py<0||py>=256)continue;
    uint16_t *dst=frame_buffer+oy*224;
    int sx=flip_screen?255-py:py;
    int bx=(sx+scroll_x)&255;
    unsigned char fmask=1<<(7-(sx&7)), bmask=1<<(7-(bx&7));
    int last_frow=-1,last_brow=-1;
    unsigned char fcode=0,fcolor=0,bcode=0,bcolor=0;
    for(int ox=0;ox<224;ox++){
      // Vanguard is ROT90 in MAME.  The previous mapping used the opposite
      // cabinet orientation, producing an image rotated by 180 degrees.
      int sy=flip_screen?ox:223-ox, by=(sy+scroll_y)&255;
      int brow=by>>3;
      if(brow!=last_brow){
        unsigned short ti=(brow<<5)+(bx>>3);
        bcode=bg_ram[ti];bcolor=(color_ram[ti]>>3)&7;last_brow=brow;
      }
      unsigned short base=(bcode<<3)+(by&7);
      unsigned char bpix=(vanguard_gfx[base]&bmask?1:0)|(vanguard_gfx[base+0x800]&bmask?2:0);
      unsigned char pi=bpix?(32+bcolor*4+bpix):(32+backcolor*4);

      int frow=sy>>3;
      if(frow!=last_frow){
        unsigned short ti=(frow<<5)+(sx>>3);
        fcode=fg_ram[ti];fcolor=color_ram[ti]&7;last_frow=frow;
      }
      base=(fcode<<3)+(sy&7);
      unsigned char fpix=(char_ram[base]&fmask?1:0)|(char_ram[base+0x800]&fmask?2:0);
      if(fpix)pi=fcolor*4+fpix;
      dst[ox]=palette[pi];
    }
  }
}
