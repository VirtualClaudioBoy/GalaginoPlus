#include "fantasy.h"
#include "fantasy_samples.h"

static_assert(sizeof(fantasy_rom) == 0x9000, "Unexpected Fantasy program ROM size");
static_assert(sizeof(fantasy_gfx) == 0x2000, "Unexpected Fantasy graphics ROM size");
static_assert(sizeof(fantasy_proms) == 0x40, "Unexpected Fantasy palette PROM size");
static_assert(sizeof(fantasy_sound_rom) == 0x1800, "Unexpected Fantasy sound ROM size");
static_assert(FANTASY_SAMPLE_COUNT == 12, "Unexpected Fantasy sample count");

// MAME hiscore.dat, shared by the Fantasy sets supported by this hardware.
const hiscore_region_S *fantasy::hiscoreRegions(unsigned char *count) {
  static const hiscore_region_S regions[] = {
    {0x0025,0x0003,0x00,0x00},
    {0x0220,0x0030,0x11,0x30}
  };
  *count=sizeof(regions)/sizeof(regions[0]);
  return regions;
}

const signed char *fantasy::vanguardSample(unsigned char index) {
  if(index < 2 || index - 2 >= FANTASY_SAMPLE_COUNT) return nullptr;
  index -= 2;
  return reinterpret_cast<const signed char *>(fantasy_samples + fantasy_sample_offsets[index]);
}

unsigned long fantasy::vanguardSampleLength(unsigned char index) {
  return index >= 2 && index - 2 < FANTASY_SAMPLE_COUNT ? fantasy_sample_lengths[index - 2] : 0;
}

unsigned char fantasy::vanguardSampleDivider(unsigned char index) {
  return index >= 2 && index - 2 < FANTASY_SAMPLE_COUNT ? 4 : 1;
}

uint16_t fantasy::pen(unsigned char p) const {
  unsigned char r=0x21*(p&1)+0x47*((p>>1)&1)+0x97*((p>>2)&1);
  unsigned char g=0x21*((p>>3)&1)+0x47*((p>>4)&1)+0x97*((p>>5)&1);
  unsigned char b=0x52*((p>>6)&1)+0xad*((p>>7)&1);
  uint16_t rgb=((r&0xf8)<<8)|((g&0xfc)<<3)|(b>>3);
  return (rgb>>8)|(rgb<<8);
}

void fantasy::start() {
  fg_ram=memory+0x400; bg_ram=memory+0x800; color_ram=memory+0xc00; char_ram=memory+0x1000;
  for(int i=0;i<64;i++) palette[i]=pen(fantasy_proms[i]);
  m_cpu.read=main_read; m_cpu.write=main_write; m_cpu.fetch=nullptr; m_cpu.user=this;
  reset();
}

void fantasy::reset() {
  machineBase::reset();
  fg_ram=memory+0x400; bg_ram=memory+0x800; color_ram=memory+0xc00; char_ram=memory+0x1000;
  scroll_x=scroll_y=backcolor=flip_screen=0; charbank=1; coin_down=false;
  music_muted[0]=music_muted[1]=music_muted[2]=true;
  speech_cmd=speech_data_bytes=0; speech_addr=0;
  if(m_cpu.read) m6502_reset(&m_cpu);
}

uint8_t fantasy::main_read(m6502_t *cpu, uint16_t a) {
  fantasy *s=static_cast<fantasy *>(cpu->user);
  if(a<0x2000) return s->memory[a];
  if(a>=0x3000 && a<0xc000) return fantasy_rom[a-0x3000];
  if(a>=0xf000) return fantasy_rom[0x5000+(a&0x0fff)];
  unsigned char k=s->input->buttons_get();
  if(a==0x2104){
    unsigned char v=0;
    if(k&BUTTON_DOWN)v|=0x10; if(k&BUTTON_UP)v|=0x20;
    if(k&BUTTON_RIGHT)v|=0x40; if(k&BUTTON_LEFT)v|=0x80;
    return v;
  }
  if(a==0x2105) return 0;
  if(a==0x2106) return FANTASY_DSW;
  if(a==0x2107) return ((k&BUTTON_COIN)?2:0)|((k&BUTTON_START)?0x80:0);
  return 0xff;
}

void fantasy::set_music_level(unsigned char channel, bool enabled) {
  if(enabled){
    if(music_muted[channel]) soundregs[channel<2 ? 8+channel : 11]++;
    music_muted[channel]=false;
  }else music_muted[channel]=true;
}

void fantasy::speech_write(unsigned char v) {
  if((v&0x30)!=0x30) return;
  static const unsigned long table[12]={
    0x04000,0x04297,0x044b6,0x04682,0x04927,0x04be0,
    0x04cc2,0x04e36,0x05000,0x05163,0x052c9,0x053fd
  };
  unsigned char data=v&0x0f;
  if(speech_cmd==2){
    speech_addr|=((unsigned long)data)<<(speech_data_bytes++*4);
    if(speech_data_bytes==5)speech_cmd=0;
  }else if(speech_cmd==4 || speech_cmd==6 || speech_cmd==8){
    speech_cmd=0;
  }else if(data==2){
    speech_cmd=2;speech_addr=0;speech_data_bytes=0;
  }else if(data==4 || data==6 || data==8){
    speech_cmd=data;
  }else if(data==12 && speech_data_bytes==5){
    for(unsigned char i=0;i<12;i++) if(table[i]==speech_addr){soundregs[3]=i;soundregs[4]++;break;}
  }else if(data==10){
    soundregs[3]=0xff;soundregs[4]++;
  }
}

void fantasy::main_write(m6502_t *cpu, uint16_t a, uint8_t v) {
  fantasy *s=static_cast<fantasy *>(cpu->user);
  if(a<0x2000){s->memory[a]=v;return;}
  if(a>=0x2100 && a<=0x2102){
    unsigned char port=a-0x2100;
    s->soundregs[port]=v;s->soundregs[5+port]++;
    if(port==0){s->set_music_level(0,v&0x08);s->set_music_level(2,v&0x10);}
    else if(port==1)s->set_music_level(1,v&0x08);
    return;
  }
  if(a==0x2103){
    s->soundregs[10]=v;s->soundregs[7]++;
    s->backcolor=v&7;s->charbank=(~v>>3)&1;s->flip_screen=v&0x80;
    return;
  }
  if(a==0x2200){s->scroll_x=v;return;}
  if(a==0x2300){s->scroll_y=v;return;}
  if(a==0x2400){s->speech_write(v);return;}
}

void fantasy::run_frame() {
  unsigned char k=input->buttons_get(); bool coin=(k&BUTTON_COIN)!=0;
  if(coin&&!coin_down)m_cpu.nmi=1; coin_down=coin;
  m6502_exec(&m_cpu,23520);
  m_cpu.irq=1;m6502_exec(&m_cpu,8);m_cpu.irq=0;
  if(!game_started)game_started=1;
}

void fantasy::render_row(short strip) {
  for(int oy=0;oy<8;oy++){
    int py=strip*8+oy-16;if(py<0||py>=256)continue;
    uint16_t *dst=frame_buffer+oy*224;
    int sx=flip_screen?255-py:py, bx=(sx+scroll_x)&255;
    unsigned char fmask=1<<(7-(sx&7)),bmask=1<<(7-(bx&7));
    int last_frow=-1,last_brow=-1;unsigned short bcode=0;
    unsigned char fcode=0,fcolor=0,bcolor=0;
    for(int ox=0;ox<224;ox++){
      int sy=flip_screen?ox:223-ox,by=(sy+scroll_y)&255,brow=by>>3;
      if(brow!=last_brow){
        unsigned short ti=(brow<<5)+(bx>>3);
        bcode=bg_ram[ti]+(charbank<<8);bcolor=(color_ram[ti]>>3)&7;last_brow=brow;
      }
      unsigned short base=(bcode<<3)+(by&7);
      // In MAME's planar decoder plane 0 is the most significant pen bit.
      // Keeping the ROM order as bit 0 swapped palette pens 1 and 2 (for
      // example blue sea became green and cyan clouds became white).
      unsigned char bpix=(fantasy_gfx[base]&bmask?2:0)|(fantasy_gfx[base+0x1000]&bmask?1:0);
      unsigned char pi=bpix?(32+bcolor*4+bpix):(32+backcolor*4);
      int frow=sy>>3;
      if(frow!=last_frow){
        unsigned short ti=(frow<<5)+(sx>>3);
        fcode=fg_ram[ti];fcolor=color_ram[ti]&7;last_frow=frow;
      }
      base=(fcode<<3)+(sy&7);
      unsigned char fpix=(char_ram[base]&fmask?2:0)|(char_ram[base+0x800]&fmask?1:0);
      if(fpix)pi=fcolor*4+fpix;
      dst[ox]=palette[pi];
    }
  }
}
