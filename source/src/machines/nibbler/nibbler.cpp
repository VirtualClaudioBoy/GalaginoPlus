#include "nibbler.h"

static_assert(sizeof(nibbler_rom)==0x9000,"Unexpected Nibbler program ROM size");
static_assert(sizeof(nibbler_gfx)==0x2000,"Unexpected Nibbler graphics ROM size");
static_assert(sizeof(nibbler_proms)==0x40,"Unexpected Nibbler palette PROM size");
static_assert(sizeof(nibbler_sound_rom)==0x1800,"Unexpected Nibbler sound ROM size");

// MAME hiscore.dat, shared by Nibbler revisions including revision 6.
const hiscore_region_S *nibbler::hiscoreRegions(unsigned char *count) {
  static const hiscore_region_S regions[] = {
    {0x0290,0x0028,0x00,0x00},
    {0x02d0,0x001e,0x13,0x14}
  };
  *count=sizeof(regions)/sizeof(regions[0]);
  return regions;
}

uint16_t nibbler::pen(unsigned char p) const {
  unsigned char r=0x21*(p&1)+0x47*((p>>1)&1)+0x97*((p>>2)&1);
  unsigned char g=0x21*((p>>3)&1)+0x47*((p>>4)&1)+0x97*((p>>5)&1);
  unsigned char b=0x52*((p>>6)&1)+0xad*((p>>7)&1);
  uint16_t rgb=((r&0xf8)<<8)|((g&0xfc)<<3)|(b>>3);
  return (rgb>>8)|(rgb<<8);
}

void nibbler::start() {
  fg_ram=memory+0x400;bg_ram=memory+0x800;color_ram=memory+0xc00;char_ram=memory+0x1000;
  for(int i=0;i<64;i++)palette[i]=pen(nibbler_proms[i]);
  m_cpu.read=main_read;m_cpu.write=main_write;m_cpu.fetch=nullptr;m_cpu.user=this;
  reset();
}

void nibbler::reset() {
  machineBase::reset();
  fg_ram=memory+0x400;bg_ram=memory+0x800;color_ram=memory+0xc00;char_ram=memory+0x1000;
  scroll_x=scroll_y=backcolor=flip_screen=0;charbank=1;coin_down=false;
  music_muted[0]=music_muted[1]=music_muted[2]=true;
  if(m_cpu.read)m6502_reset(&m_cpu);
}

uint8_t nibbler::main_read(m6502_t *cpu,uint16_t a) {
  nibbler *s=static_cast<nibbler *>(cpu->user);
  if(a<0x2000)return s->memory[a];
  if(a>=0x3000&&a<0xc000)return nibbler_rom[a-0x3000];
  if(a>=0xf000)return nibbler_rom[0x5000+(a&0x0fff)];
  unsigned char k=s->input->buttons_get();
  if(a==0x2104){
    unsigned char v=0;
    if(k&BUTTON_DOWN)v|=0x10;if(k&BUTTON_UP)v|=0x20;
    if(k&BUTTON_RIGHT)v|=0x40;if(k&BUTTON_LEFT)v|=0x80;
    return v;
  }
  if(a==0x2105)return 0;
  if(a==0x2106)return NIBBLER_DSW;
  if(a==0x2107)return ((k&BUTTON_COIN)?2:0)|((k&BUTTON_START)?0x80:0);
  return 0xff;
}

void nibbler::set_music_level(unsigned char channel,bool enabled) {
  if(enabled){
    if(music_muted[channel])soundregs[channel<2?8+channel:11]++;
    music_muted[channel]=false;
  }else music_muted[channel]=true;
}

void nibbler::main_write(m6502_t *cpu,uint16_t a,uint8_t v) {
  nibbler *s=static_cast<nibbler *>(cpu->user);
  if(a<0x2000){s->memory[a]=v;return;}
  if(a>=0x2100&&a<=0x2102){
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
}

void nibbler::run_frame() {
  unsigned char k=input->buttons_get();bool coin=(k&BUTTON_COIN)!=0;
  if(coin&&!coin_down)m_cpu.nmi=1;coin_down=coin;
  m6502_exec(&m_cpu,23520);
  m_cpu.irq=1;m6502_exec(&m_cpu,8);m_cpu.irq=0;
  if(!game_started)game_started=1;
}

void nibbler::render_row(short strip) {
  for(int oy=0;oy<8;oy++){
    int py=strip*8+oy-16;if(py<0||py>=256)continue;
    uint16_t *dst=frame_buffer+oy*224;
    int sx=flip_screen?255-py:py,bx=(sx+scroll_x)&255;
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
      unsigned char bpix=(nibbler_gfx[base]&bmask?2:0)|(nibbler_gfx[base+0x1000]&bmask?1:0);
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
