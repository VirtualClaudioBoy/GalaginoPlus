#include "audio.h"
#include <math.h>

void Audio::init() {
  // 24 kHz @ 16 bit = 48000 bytes/sec = 800 bytes per 60hz game frame =
  // 1600 bytes per 30hz screen update = ~177 bytes every four tile rows
  const i2s_config_t i2s_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX | I2S_MODE_DAC_BUILT_IN),
    .sample_rate = 24000,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
#ifdef SND_DIFF
    .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
#elif defined(SND_LEFT_CHANNEL) // For devices using the left channel (e.g. CYD)
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
#else
    .channel_format = I2S_CHANNEL_FMT_ONLY_RIGHT,
#endif
    .intr_alloc_flags = 0,
    .dma_buf_count = 4,
    .dma_buf_len = 64,   // 64 samples
    // APLL usage is broken in ESP-IDF 4.4.5
#ifdef WORKAROUND_I2S_APLL_PROBLEM
    .use_apll = false
#else
    .use_apll = true
#endif
  };

  i2s_driver_install(I2S_NUM_0, &i2s_config, 0, NULL);

#ifdef SND_DIFF
  i2s_set_dac_mode(I2S_DAC_CHANNEL_BOTH_EN);
#elif defined(SND_LEFT_CHANNEL) // For devices using the left channel (e.g. CYD)
  i2s_set_dac_mode(I2S_DAC_CHANNEL_LEFT_EN); 
#else
  i2s_set_dac_mode(I2S_DAC_CHANNEL_RIGHT_EN);
#endif  

  generateSinusWave(256, sinusWaveBuffer, sizeof(sinusWaveBuffer)  / 2 );
}

void Audio::start(machineBase *machineBase) {
  currentMachine = machineBase;
  machineType = currentMachine->machineType();

  AY = 0;
  if (machineType == MCH_FROGGER)         { AY = 1; AY_INC = 9; AY_VOL = 11; }
  else if (machineType == MCH_1942)       { AY = 2; AY_INC = 8; AY_VOL = 5;  }
  else if (machineType == MCH_ANTEATER)   { AY = 2; AY_INC = 9; AY_VOL = 5;  }
  else if (machineType == MCH_BOMBJACK)   { AY = 3; AY_INC = 8; AY_VOL = 4;  }
  else if (machineType == MCH_BTIME)      { AY = 2; AY_INC = 8; AY_VOL = 7;  }
  else if (machineType == MCH_BNJ)        { AY = 2; AY_INC = 8; AY_VOL = 7;  }
  else if (machineType == MCH_GYRUSS)     { AY = 5; AY_INC = 9; AY_VOL = 3;  }
  else if (machineType == MCH_TIMEPLT)    { AY = 2; AY_INC = 9; AY_VOL = 5;  }
  else if (machineType == MCH_PBACTION)   { AY = 3; AY_INC = 8; AY_VOL = 4;  }
  else if (machineType == MCH_TUTANKHM)   { AY = 2; AY_INC = 7; AY_VOL = 7;  }
  else if (machineType == MCH_SCRAMBLE)   { AY = 2; AY_INC = 8; AY_VOL = 7;  }
  else if (machineType == MCH_SUPERCOBRA) { AY = 2; AY_INC = 8; AY_VOL = 7;  }
  else if (machineType == MCH_TURTLES)    { AY = 2; AY_INC = 8; AY_VOL = 7;  }
  else if (machineType == MCH_AMIDAR)    { AY = 2; AY_INC = 8; AY_VOL = 7;  }
  else if (machineType == MCH_ROCNROPE)  { AY = 2; AY_INC = 8; AY_VOL = 7;  }
  else if (machineType == MCH_POOYAN)    { AY = 2; AY_INC = 9; AY_VOL = 5;  }
  else if (machineType == MCH_SCREGG)    { AY = 2; AY_INC = 8; AY_VOL = 7;  }

  for(char ay = 0; ay < NUM_AY_CHIPS; ay++) {
    for (int c = 0; c < 4; c++) {
      audio_cnt[ay][c] = 1;
    }

    for (int c = 0; c < 3; c++) {
      audio_toggle[ay][c] = 1;
      ay_envelope[ay][c] = 0;
    }
    ay_noise_rng[ay] = 1;

    ay_envelope_period[ay] = 0;
    ay_envelope_shape[ay] = 0;
    ay_envelope_counter[ay] = 0;
    ay_envelope_step[ay] = 0;
    ay_envelope_holding[ay] = 0;
  }

  for (int sn = 0; sn < NUM_SN_CHIPS; sn++) {
    for (int c = 0; c < 4; c++) {
      sn_counter[sn][c] = 0;
      sn_toggle[sn][c] = 1;
    }
  }
  vg_phase[0] = vg_phase[1] = vg_phase[2] = 0;
  vg_offset[0] = vg_offset[1] = vg_offset[2] = 0;
  vg_music_clock = 0;
  vg_noise = 0x1ffff;
  vg_last_port0 = 0;
  vg_port_sequence[0] = currentMachine->soundregs[5];
  vg_port_sequence[1] = currentMachine->soundregs[6];
  vg_music0_restart = currentMachine->soundregs[8];
  vg_music1_restart = currentMachine->soundregs[9];
  vg_music2_restart = currentMachine->soundregs[11];
  vg_music0_command_muted = currentMachine->vanguardMusic0Muted();
  vg_music1_command_muted = currentMachine->vanguardMusic1Muted();
  vg_music2_command_muted = currentMachine->vanguardMusic2Muted();
  vg_muted[0] = vg_music0_command_muted;
  vg_muted[1] = vg_music1_command_muted;
  vg_muted[2] = vg_music2_command_muted;
  for (int i=0;i<3;i++) {
    vg_sample_pos[i]=vg_sample_len[i]=0;vg_sample_ptr[i]=nullptr;
    vg_adpcm_predictor[i]=vg_adpcm_step[i]=vg_sample_repeat[i]=0;vg_sample_divider[i]=1;
  }
  vg_speech_sequence = currentMachine->soundregs[4];
#ifndef WORKAROUND_I2S_APLL_PROBLEM
  // The audio CPU of donkey kong runs at 6Mhz. A full bus
  // cycle needs 15 clocks which results in 400k cycles
  // per second. The sound CPU typically needs 34 instruction
  // cycles to write an updated audio value to the external
  // DAC connected to port 0.

  // The effective sample rate thus is 6M/15/34 = 11764.7 Hz
  i2s_set_sample_rates(I2S_NUM_0, machineType == MCH_DKONG ? 11765 : 24000);
#endif
}

void Audio::volumeUpDown(bool up, bool down) {
  if (up && !volumeUpLast) {
    if (volumeSetting > 1)
      volumeSetting--;
  }
  volumeUpLast = up;

  if (down && !volumeDownLast) {
    if (volumeSetting < 30)
      volumeSetting++;
  }
  volumeDownLast = down;
}

void Audio::mute(bool m) {
  muted = m;
}

void Audio::transmit() {
  // (try to) transmit as much audio data as possible. Since we
  // write data in exact the size of the DMA buffers we can be sure
  // that either all or nothing is actually being written
  size_t bytesOut = 0;
  do {
    // copy data in i2s dma buffer if possible
    i2s_write(I2S_NUM_0, snd_buffer, sizeof(snd_buffer), &bytesOut, 0);
    if (!bytesOut)
      return;

    // render the next audio chunk if data has actually been sent
    if (AY > 0)
      ay_render_buffer();
    else if (currentMachine->hasNamcoAudio())
      namco_render_buffer();
    else if (currentMachine->hasNamco15xxAudio())
      namco_15xx_render_buffer();
    else if (machineType == MCH_MRDO || machineType == MCH_LADYBUG || machineType == MCH_STARFORCE || machineType == MCH_VANVAN || machineType == MCH_CIRCUSC)
      sn76489_render_buffer();
    else if (machineType == MCH_DKONG || machineType == MCH_DKONGJR)
      i8048_render_buffer();
    else if (machineType == MCH_DKONG3)
      dkong3_render_buffer();
    else if (machineType == MCH_BAGMAN)
      bagman_render_buffer();
    else if(machineType == MCH_SPACE)
      spaceinvaders_render_buffer();
    else if(machineType == MCH_GALAXIAN || machineType == MCH_MOONCRESTA)
      galaxian_render_buffer();
    else if(machineType == MCH_PHOENIX)
      phoenix_render_buffer();
    else if(machineType == MCH_VANGUARD || machineType == MCH_FANTASY || machineType == MCH_NIBBLER)
      vanguard_render_buffer();
  } while(bytesOut);
}

void Audio::vanguard_render_buffer(void) {
  const bool is_fantasy = machineType == MCH_FANTASY || machineType == MCH_NIBBLER;
  const uint8_t p0 = currentMachine->soundregs[0];
  const uint8_t p1 = currentMachine->soundregs[1];
  const uint8_t shapes = currentMachine->soundregs[2];
  const uint8_t p3 = currentMachine->soundregs[10];

  // These controls are evaluated on a CPU write, not continuously from the
  // latched level. Continuously resetting offset 0 made the opening tune stick.
  if(currentMachine->soundregs[5]!=vg_port_sequence[0]){
    vg_port_sequence[0]=currentMachine->soundregs[5];
    if (!is_fantasy && (p0 & 0x20) && !(vg_last_port0 & 0x20)) {
      vg_sample_ptr[0]=currentMachine->vanguardSample(0);
      vg_sample_len[0]=currentMachine->vanguardSampleLength(0);vg_sample_pos[0]=0;
      vg_sample_divider[0]=currentMachine->vanguardSampleDivider(0);vg_sample_repeat[0]=0;
    } else if (!(p0&0x20) && (vg_last_port0&0x20)) vg_sample_len[0]=0;
    if (!is_fantasy && (p0 & 0x80) && !(vg_last_port0 & 0x80)) {
      vg_sample_ptr[1]=currentMachine->vanguardSample(1);
      vg_sample_len[1]=currentMachine->vanguardSampleLength(1);vg_sample_pos[1]=0;
      vg_sample_divider[1]=currentMachine->vanguardSampleDivider(1);vg_sample_repeat[1]=0;
    }
    vg_last_port0=p0;
  }
  if(currentMachine->soundregs[8]!=vg_music0_restart){
    vg_music0_restart=currentMachine->soundregs[8];vg_offset[0]=0;vg_muted[0]=false;
  }
  bool command_muted=currentMachine->vanguardMusic0Muted();
  if(command_muted!=vg_music0_command_muted){
    vg_music0_command_muted=command_muted;
    if(command_muted){vg_muted[0]=true;vg_offset[0]=0;}
    else {vg_muted[0]=false;vg_offset[0]=0;}
  }
  vg_port_sequence[1]=currentMachine->soundregs[6];
  if(currentMachine->soundregs[9]!=vg_music1_restart){
    vg_music1_restart=currentMachine->soundregs[9];vg_offset[1]=0;vg_muted[1]=false;
  }
  bool command_muted1=currentMachine->vanguardMusic1Muted();
  if(command_muted1!=vg_music1_command_muted){
    vg_music1_command_muted=command_muted1;
    if(command_muted1){vg_muted[1]=true;vg_offset[1]=0;}
    else {vg_muted[1]=false;vg_offset[1]=0;}
  }
  if(currentMachine->soundregs[11]!=vg_music2_restart){
    vg_music2_restart=currentMachine->soundregs[11];vg_offset[2]=0;vg_muted[2]=false;
  }
  bool command_muted2=currentMachine->vanguardMusic2Muted();
  if(command_muted2!=vg_music2_command_muted){
    vg_music2_command_muted=command_muted2;
    if(command_muted2){vg_muted[2]=true;vg_offset[2]=0;}
    else {vg_muted[2]=false;vg_offset[2]=0;}
  }

  uint8_t speech_sequence=currentMachine->soundregs[4];
  if(speech_sequence!=vg_speech_sequence){
    vg_speech_sequence=speech_sequence;
    uint8_t phrase=currentMachine->soundregs[3];
    if(phrase==0xff)vg_sample_len[2]=0;
    else if(vg_sample_pos[2]>=vg_sample_len[2]){
      vg_sample_ptr[2]=currentMachine->vanguardSample(2+phrase);
      vg_sample_len[2]=currentMachine->vanguardSampleLength(2+phrase);vg_sample_pos[2]=0;
      vg_sample_divider[2]=currentMachine->vanguardSampleDivider(2+phrase);vg_sample_repeat[2]=0;
    }
  }

  // Exact resistor-weighted waveform construction used by the SNK circuit.
  int8_t form[3][16];
  for(int ch=0;ch<(is_fantasy?3:2);ch++){
    if(ch==2){
      for(int pos=0;pos<16;pos++)form[ch][pos]=(pos&8)?7:-8;
      continue;
    }
    uint8_t mask=ch?(shapes>>4):(is_fantasy ?
      ((shapes&9)|((shapes&2)<<1)|((shapes&4)>>1)) :
      ((shapes&3)|((shapes&4)<<1)|((shapes&8)>>1)));
    int bit3=(mask&3)?8:((mask&4)?4:((mask&8)?2:0));
    int bit2=(mask&4)?8:((mask&0x0a)?4:0);
    int bit1=(mask&8)?8:((mask&4)?4:((mask&2)?2:0));
    int bit0=bit1>>1;
    if(bit0+bit1+bit2+bit3<16){bit0<<=1;bit1<<=1;bit2<<=1;bit3<<=1;}
    int base=(bit0+bit1+bit2+bit3+1)>>1;
    for(int pos=0;pos<16;pos++)
      form[ch][pos]=((pos&1)?bit0:0)+((pos&2)?bit1:0)+
                    ((pos&4)?bit2:0)+((pos&8)?bit3:0)-base;
  }

  for (int i = 0; i < 64; i++) {
    int sample = 0;
    for (int ch = 0; ch < (is_fantasy ? 3 : 2); ch++) {
      if (vg_muted[ch]) continue;
      unsigned short base;
      if(ch==2)base=0x1000|((p3&0x70)<<4);
      else base=(ch ? 0x0800 : 0)|((ch ? p1 : p0)&7)*0x100;
      uint8_t note = currentMachine->vanguardSoundRom(base + vg_offset[ch]);
      if (note == 0xff) continue;
      uint32_t step = 939349UL / (256 - note); // 43000*8/24000 in Q16
      uint32_t next_phase = vg_phase[ch] + step;
      int previous = form[ch][(vg_phase[ch] >> 16) & 15];
      int current = form[ch][(next_phase >> 16) & 15];
      uint32_t fraction = next_phase & 0xffff;
      sample += ((previous * (int)(0x10000 - fraction) +
                  current * (int)fraction) >> 16) * 16;
      vg_phase[ch] = next_phase;
    }

    // IMA-ADPCM samples: effects at 8 kHz, speech at 6 kHz, held to 24 kHz.
    static const int ima_step_table[89]={
      7,8,9,10,11,12,13,14,16,17,19,21,23,25,28,31,34,37,41,45,50,55,60,66,
      73,80,88,97,107,118,130,143,157,173,190,209,230,253,279,307,337,371,
      408,449,494,544,598,658,724,796,876,963,1060,1166,1282,1411,1552,
      1707,1878,2066,2272,2499,2749,3024,3327,3660,4026,4428,4871,5358,
      5894,6484,7132,7845,8630,9493,10442,11487,12635,13899,15289,16818,
      18500,20350,22385,24623,27086,29794,32767};
    static const int8_t ima_index_table[16]={-1,-1,-1,-1,2,4,6,8,-1,-1,-1,-1,2,4,6,8};
    for(int voice=0;voice<3;voice++)if(vg_sample_ptr[voice] && vg_sample_pos[voice]<vg_sample_len[voice]){
      if(!vg_sample_repeat[voice]){
        if(!vg_sample_pos[voice]){
          const uint8_t *encoded=(const uint8_t *)vg_sample_ptr[voice];
          vg_adpcm_predictor[voice]=(int16_t)(encoded[0]|(encoded[1]<<8));vg_adpcm_step[voice]=0;
        }else{
          uint32_t nibble=vg_sample_pos[voice]-1;
          uint8_t packed=(uint8_t)vg_sample_ptr[voice][2+(nibble>>1)];
          uint8_t code=(nibble&1)?(packed>>4):(packed&15);
          int step=ima_step_table[vg_adpcm_step[voice]],delta=step>>3;
          if(code&1)delta+=step>>2;if(code&2)delta+=step>>1;if(code&4)delta+=step;
          int predictor=vg_adpcm_predictor[voice]+((code&8)?-delta:delta);
          if(predictor>32767)predictor=32767;else if(predictor<-32768)predictor=-32768;
          vg_adpcm_predictor[voice]=predictor;
          int index=vg_adpcm_step[voice]+ima_index_table[code];
          vg_adpcm_step[voice]=index<0?0:(index>88?88:index);
        }
        vg_sample_pos[voice]++;vg_sample_repeat[voice]=vg_sample_divider[voice];
      }
      sample+=(vg_adpcm_predictor[voice]>>8);vg_sample_repeat[voice]--;
    }

    // Shot B is the cabinet's second SN76477 and remains synthesized noise.
    uint32_t feedback = (vg_noise ^ (vg_noise >> 3)) & 1;
    vg_noise = (vg_noise >> 1) | (feedback << 16);
    int noise = (vg_noise & 1) ? 1 : -1;
    if (!is_fantasy && (p0 & 0x40)) sample += noise * 70;
    if (is_fantasy && (p0 & 0x80)) sample += noise * 55;

    vg_music_clock += is_fantasy ? 416 : 3899;
    uint32_t music_threshold=is_fantasy ? 240000 : 2400000;
    if (vg_music_clock >= music_threshold) {
      vg_music_clock -= music_threshold;
      for (int ch=0;ch<(is_fantasy?3:2);ch++) vg_offset[ch]=(vg_offset[ch]+1)&0xff;
      if (!is_fantasy && vg_offset[0] == 0) {
        vg_muted[0]=true;
        vg_music0_command_muted=true;
        currentMachine->vanguardMusic0Ended();
      }
    }
    if (sample > 500) sample = 500; else if (sample < -500) sample = -500;
    valueToBuffer(i, sample);
  }
}

void Audio::ay_render_buffer(void) {
  for(char ay = 0; ay < AY; ay++) {
    int ay_off = 16 * ay;
    // three tone channels
    for(char c = 0; c < 3; c++) {
      ay_period[ay][c] = currentMachine->soundregs[ay_off + (2 * c)] + (256 * (currentMachine->soundregs[ay_off + (2 * c) + 1] & 0x0f)); // 12bit
      ay_enable[ay][c] = (((currentMachine->soundregs[ay_off + 7] >> c) & 1) | ((currentMachine->soundregs[ay_off + 7] >> (c + 2)) & 2)) ^ 3; // 1=Tone; 2=Noise
      ay_volume[ay][c] = currentMachine->soundregs[ay_off + 8 + c];
      // envelope is used by Anteater and Tutankhm. Gyruss envelope not working, because it is updated multiple during one vblank.  
      ay_envelope[ay][c] = ((ay_volume[ay][c] & 0x10) == 0x10) && machineType != MCH_GYRUSS; 
    }

    // R6 noise channel. Noise is used by 1942, Anteater and Bombjack
    ay_period[ay][3] = currentMachine->soundregs[ay_off + 6] & 0x1f; // 5bit
    
    // --- LOGICA INVILUPPO: Leggi registri R11, R12 ---
    ay_envelope_period[ay] = currentMachine->soundregs[ay_off + 11] + 256 * currentMachine->soundregs[ay_off + 12]; //16bit

    // Rileva un cambio di forma d'onda (R13) per triggerare l'inviluppo
    uint8_t new_shape = currentMachine->soundregs[ay_off + 13];
    if (new_shape != ay_envelope_shape[ay]) {
      ay_envelope_shape[ay] = new_shape & 0x0F;
      ay_envelope_counter[ay] = 0; // Reset contatore
      // Imposta lo step iniziale in base alla forma d'onda (attacco: 0, decadimento: 15)
      ay_envelope_step[ay] = (ay_envelope_shape[ay] < 4 || (ay_envelope_shape[ay] >= 8 && ay_envelope_shape[ay] < 12)) ? 0 : 15;
      ay_envelope_holding[ay] = 0; // Non in stato di "hold"
    }
  }

  // render first buffer contents
  for(int i = 0; i < 64; i++) {
    short value = 0; // silence

    for(char ay = 0; ay < AY; ay++) {
      // --- LOGICA INVILUPPO: Esegui un passo di emulazione ---
      if (!ay_envelope_holding[ay] && ay_envelope_period[ay] > 0) {
        ay_envelope_counter[ay] += AY_INC;
        if (ay_envelope_counter[ay] >= ay_envelope_period[ay]) {
          ay_envelope_counter[ay] -= ay_envelope_period[ay];
          // Avanza lo step del volume dell'inviluppo in base alla forma
          if (ay_envelope_shape[ay] < 8) { // Forme d'attacco (volume cresce da 0 a 15)
            ay_envelope_step[ay]++;
            if (ay_envelope_step[ay] > 15) {
              // Se la forma è "alternata" (bit 0 settato), riparte da 0, altrimenti rimane a 15
              ay_envelope_step[ay] = (ay_envelope_shape[ay] & 1) ? 0 : 15; 
              // Se la forma è "hold" (bit 1 settato), si ferma qui
              if (ay_envelope_shape[ay] & 2) ay_envelope_holding[ay] = 1; 
            }
          } 
          else { // Forme di decadimento (volume decresce da 15 a 0)
            ay_envelope_step[ay]--;
            if (ay_envelope_step[ay] < 0) {
              // Se la forma è "alternata" (bit 0 settato), riparte da 15, altrimenti rimane a 0
              ay_envelope_step[ay] = (ay_envelope_shape[ay] & 1) ? 15 : 0; 
              // Se la forma è "hold" (bit 1 settato), si ferma qui
              if (ay_envelope_shape[ay] & 2) ay_envelope_holding[ay] = 1; 
            }
          }
        }
      }
     
      // Elabora il generatore di rumore (R6)
      if(ay_period[ay][3]) {
        audio_cnt[ay][3] += AY_INC;
        if(audio_cnt[ay][3] > ay_period[ay][3]) {
          audio_cnt[ay][3] -= ay_period[ay][3];
          // progress rng
	        ay_noise_rng[ay] ^= (((ay_noise_rng[ay] & 1) ^ ((ay_noise_rng[ay] >> 3) & 1)) << 17);
	        ay_noise_rng[ay] >>= 1;
        }
      }
  
      // Elabora i 3 canali di tono e li mixa con il rumore
      for(char c = 0; c < 3; c++) {
        // For a tone to be heard, the corresponding channel must have its volume set, and the tone must be enabled in the Mixer R7
        if((ay_period[ay][c] || ay_envelope[ay][c]) && ay_volume[ay][c] && ay_enable[ay][c]) {
          // --- LOGICA INVILUPPO: Scegli il volume corretto ---
          int current_channel_volume = 0;
          if (ay_volume[ay][c] & 0x10) { // Se il bit 4 del registro volume è 1, usa l'inviluppo
            current_channel_volume = ay_envelope_step[ay];
          } 
          else { // Altrimenti, usa il volume fisso (bit 0-3)
            current_channel_volume = ay_volume[ay][c] & 0x0F;
          }

          if (current_channel_volume > 0) { // Solo se il volume non è zero
            short bit = 1;
            // Applica il mixing Tono/Rumore in base ai bit di ay_enable (ottenuti da R7)
            if(ay_enable[ay][c] & 1) bit &= (audio_toggle[ay][c] > 0) ? 1 : 0; // Bit 0 di ay_enable -> Tono
            if(ay_enable[ay][c] & 2) bit &= (ay_noise_rng[ay] & 1) ? 1 : 0;    // Bit 1 di ay_enable -> Rumore
    
            // Se il bit risultante è 0, il segnale è invertito per l'onda quadra
            if(bit == 0) bit = -1;
            value += AY_VOL * bit * current_channel_volume;
          }
    
          // Avanza il contatore del tono (R0-R5)
          audio_cnt[ay][c] += AY_INC;
          if(audio_cnt[ay][c] > ay_period[ay][c]) {
            audio_cnt[ay][c] -= ay_period[ay][c];
            audio_toggle[ay][c] = -audio_toggle[ay][c];
          }
        }
      }
    }

    // Drums Gyruss: somma al mix dei 5 AY un campione DAC dell'i8039 preso
    // dal ring riempito da run_frame sul core emulazione — NON steppare
    // l'8039 qui: sul loopTask congela gyruss su HW (isolato con stub
    // 2026-07-11). renderDrumSample() e' una virtual di machineBase (default
    // 0): evita di includere gyruss.h qui — ENABLE_GYRUSS non e' visibile
    // fuori da main.cpp e l'include duplicherebbe le ROM const in flash.
    // On/off e volume si regolano SOLO in gyruss.h (GYRUSS_ENABLE_DRUMS,
    // GYRUSS_DRUMS_VOLUME): il campione arriva gia' scalato.
    // DC blocker (high-pass ~40Hz) per togliere l'offset di riposo dell'8039
    // -> niente pop/distorsione costante.
    if (machineType == MCH_GYRUSS) {
      static int dc = 0;
      int s = currentMachine->renderDrumSample();  // gia' scalato (volume in gyruss.h)
      dc += (s - dc) >> 7;                         // media lenta = livello DC
      value += s - dc;                             // componente AC
    }

    valueToBuffer(i, value);
  }
}

void Audio::i8048_render_buffer(void) {
  dkong *dkongMachine = static_cast<dkong*>(currentMachine);

  // render first buffer contents
  for(int i = 0; i < 64; i++) {
    short value = 0; // silence

    // no buffer available
    if(dkongMachine->dkong_audio_rptr != dkongMachine->dkong_audio_wptr)
      // copy data from dkong buffer into tx buffer
      // 8048 sounds gets 50% of the available volume range
#ifdef WORKAROUND_I2S_APLL_PROBLEM
      value = dkongMachine->dkong_audio_transfer_buffer[dkongMachine->dkong_audio_rptr][(dkongMachine->dkong_obuf_toggle ? 32 : 0) + (i / 2)];
#else
      value = dkongMachine->dkong_audio_transfer_buffer[dkongMachine->dkong_audio_rptr][i];
#endif

    // include sample sounds
    for(char j = 0; j < sizeof(dkongMachine->dkong_sample_cnt) / 2; j++) {
      if(dkongMachine->dkong_sample_cnt[j]) {
#ifdef WORKAROUND_I2S_APLL_PROBLEM
        value += *dkongMachine->dkong_sample_ptr[j]; 
        if(i & 1) { // advance read pointer every second sample
          dkongMachine->dkong_sample_ptr[j]++;
          dkongMachine->dkong_sample_cnt[j]--;
        }
#else
        value += *dkongMachine->dkong_sample_ptr[j]++; 
        dkongMachine->dkong_sample_cnt[j]--;
#endif
      }
    }
#ifdef WORKAROUND_I2S_APLL_PROBLEM
    if (i == 63) {
      // advance write pointer. The buffer is a ring
      if(dkongMachine->dkong_obuf_toggle)
        dkongMachine->dkong_audio_rptr = (dkongMachine->dkong_audio_rptr + 1) & DKONG_AUDIO_QUEUE_MASK;
        
      dkongMachine->dkong_obuf_toggle = !dkongMachine->dkong_obuf_toggle;
    }
#endif
    value = value << 1;
    if (value > 384)
      value = 384;
    else if (value < -384)
      value = -384;
      
    valueToBuffer(i, value);
  }
}

void Audio::sn76489_render_buffer(void) {
  const int sn_inc = 11;  // SN_CLOCK / SAMPLE_RATE
  
  // Volumi con hold
  int vol[NUM_SN_CHIPS][4];
  for (int chip = 0; chip < NUM_SN_CHIPS; chip++) {
    for (int c = 0; c < 4; c++) {
      if (currentMachine->sn_hold[chip][c] > 0) {
        vol[chip][c] = currentMachine->sn_min_volume[chip][c];
        currentMachine->sn_hold[chip][c]--;
        if (currentMachine->sn_hold[chip][c] == 0)
          currentMachine->sn_min_volume[chip][c] = currentMachine->sn_volume[chip][c];
        } 
        else {
          vol[chip][c] = currentMachine->sn_volume[chip][c];
          currentMachine->sn_min_volume[chip][c] = currentMachine->sn_volume[chip][c];
        }
      }
    }

    for (int i = 0; i < 64; i++) {
      short sample = 0;

      for (int chip = 0; chip < NUM_SN_CHIPS; chip++) {
        for (int c = 0; c < 4; c++) {
          int period = currentMachine->sn_period[chip][c];

          if (vol[chip][c] < 15 && period > 0) {
            sn_counter[chip][c] -= sn_inc;

          while (sn_counter[chip][c] <= 0) {
            if (c == 3) {  // Noise channel
              uint32_t feedback = (noise_lfsr[chip] & 0x01) ^ ((noise_lfsr[chip] & 0x02) >> 1);
              noise_lfsr[chip] = (noise_lfsr[chip] >> 1) | (feedback << 14);
              sn_toggle[chip][c] = (noise_lfsr[chip] & 0x01) ? 1 : -1;
              sn_counter[chip][c] += (period << 3);  // Rallenta noise
            } 
            else {  // Tone
              sn_counter[chip][c] += period;
              sn_toggle[chip][c] = -sn_toggle[chip][c];
            }
          }
          sample += sn_toggle[chip][c] * (15 - vol[chip][c]) * 6;
        }
      }
    }

    // Circus Charlie: mix del DAC 8 bit (effetti) sopra i due SN76489A.
    // Come i drums di Gyruss: pop dal ring riempito in run_frame sul core
    // emulazione (MAI steppare CPU qui) + DC blocker per l'offset di riposo.
    if (machineType == MCH_CIRCUSC) {
      static int dc = 0;
      int s = currentMachine->renderDrumSample();  // gia' scalato (volume in circusc.h)
      dc += (s - dc) >> 7;
      sample += s - dc;
    }

    valueToBuffer(i, sample);
  }
}

void Audio::namco_render_buffer(void) {
  // parse all three wsg channels
  for(char ch = 0; ch < 3; ch++) {  
    snd_wave[ch] = currentMachine->waveRom(currentMachine->soundregs[ch * 5 + 0x05] & 0x07);
    snd_freq[ch] = (ch == 0) ? currentMachine->soundregs[0x10] : 0; //5050-5054, 5056-5059, 505b-505e
    snd_freq[ch] += currentMachine->soundregs[ch * 5 + 0x11] << 4;
    snd_freq[ch] += currentMachine->soundregs[ch * 5 + 0x12] << 8;
    snd_freq[ch] += currentMachine->soundregs[ch * 5 + 0x13] << 12;
    snd_freq[ch] += currentMachine->soundregs[ch * 5 + 0x14] << 16;        
    snd_volume[ch] = currentMachine->soundregs[ch * 5 + 0x15]; //5055, 505a, 505f
  }

  // render first buffer contents
  for(int i = 0; i < 64; i++) {
    short value = 0;

    // add up to three wave signals
    if(snd_volume[0]) value += snd_volume[0] * snd_wave[0][(snd_cnt[0] >> 13) & 0x1f];
    if(snd_volume[1]) value += snd_volume[1] * snd_wave[1][(snd_cnt[1] >> 13) & 0x1f];
    if(snd_volume[2]) value += snd_volume[2] * snd_wave[2][(snd_cnt[2] >> 13) & 0x1f];

    snd_cnt[0] += snd_freq[0];
    snd_cnt[1] += snd_freq[1];
    snd_cnt[2] += snd_freq[2];
    
    if(machineType == MCH_GALAGA) {
      galaga *galagaMachine = static_cast<galaga*>(currentMachine);

      if(galagaMachine->snd_boom_cnt) {
        value += *galagaMachine->snd_boom_ptr * 3;

        if(galagaMachine->snd_boom_cnt & 1)
          galagaMachine->snd_boom_ptr++;

        galagaMachine->snd_boom_cnt--;
      }
    }
    else if(machineType == MCH_XEVIOUS) {
      xevious *xeviousMachine = static_cast<xevious*>(currentMachine);

      // campioni gia' a 24kHz = sample rate galagino: 1 byte per campione
      // (piena velocita', stile gaplus::snd_bang — lo schema galaga sopra
      // avanza ogni 2 campioni perche' il suo WAV storico e' a 12kHz)
      if(xeviousMachine->snd_boom_cnt) {
        value += *xeviousMachine->snd_boom_ptr * 3;
        xeviousMachine->snd_boom_ptr++;
        xeviousMachine->snd_boom_cnt--;
      }
    }
    valueToBuffer(i, value);
  }
}

// Namco WSG 15XX (Mappy): 8 voci, registri in soundregs[0x00-0x3F] (RAM
// condivisa 0x4000-0x403F). Layout per canale ch (base ch*8, da MAME
// sound/namco.cpp): +3 volume, +4/+5 freq bit 0-15, +6 = wave select
// (bit 4-6) + freq bit 16-19. Clock chip = 18.432MHz/768 = 24000 Hz ESATTI
// = sample rate galagino: 1 tick per campione, counter a 15 bit frazionari
// -> posizione onda = (cnt >> 15) & 0x1f (32 campioni per forma).
void Audio::namco_15xx_render_buffer(void) {
  bool enabled = currentMachine->namcoSoundEnabled();

  for (char ch = 0; ch < 8; ch++) {
    snd15_wave[ch] = currentMachine->waveRom((currentMachine->soundregs[ch * 8 + 6] >> 4) & 0x07);
    snd15_freq[ch] = currentMachine->soundregs[ch * 8 + 4] +
                     (currentMachine->soundregs[ch * 8 + 5] << 8) +
                     ((unsigned long)(currentMachine->soundregs[ch * 8 + 6] & 0x0f) << 16);
    snd15_vol[ch] = enabled ? (currentMachine->soundregs[ch * 8 + 3] & 0x0f) : 0;
  }

  for (int i = 0; i < 64; i++) {
    short value = 0;

    for (char ch = 0; ch < 8; ch++) {
      if (snd15_vol[ch])
        value += snd15_vol[ch] * snd15_wave[ch][(snd15_cnt[ch] >> 15) & 0x1f];
      snd15_cnt[ch] += snd15_freq[ch];
    }

    if (machineType == MCH_GAPLUS) {
      gaplus *gaplusMachine = static_cast<gaplus*>(currentMachine);

      if (gaplusMachine->snd_bang_cnt) {
        value += *gaplusMachine->snd_bang_ptr * 3;
        gaplusMachine->snd_bang_ptr++;
        gaplusMachine->snd_bang_cnt--;
      }
    }

    // 8 voci x vol 15 x onda +/-8 = +/-960: dimezza per stare nel
    // contratto +/-512 di valueToBuffer
    valueToBuffer(i, value >> 1);
  }
}

void Audio::generateSinusWave(int32_t amplitude, short* buffer, uint16_t length) {
  for (int i=0; i<length; ++i) {
    buffer[i] = int32_t(float(amplitude) * sin(2.0 * PI * (1.0 / length) * i));
  }
}

void Audio::bagman_render_buffer() {
  unsigned short duration = currentMachine->soundregs[0] + (currentMachine->soundregs[1] << 8);
  if (duration > 0)
    duration--;

  currentMachine->soundregs[0] = duration & 0x00ff;
  currentMachine->soundregs[1] = (duration & 0xff00) > 8;

  float frequency;
  switch (currentMachine->soundregs[2]) {
    case 0x3: frequency = A5_3; break;
    case 0x4: frequency = C6_4; break;
    case 0x5: frequency = F5_5; break;
    case 0x6: frequency = G5_6; break;
    case 0x7: frequency = E6_7; break;
    case 0x8: frequency = B6_8; break;
    case 0xE: frequency = D6_E; break;
    case 0xF: frequency = B5_F; break;
    case 0xB: frequency = XX_B; break;
  }

  unsigned short pause = currentMachine->soundregs[3];
  if (pause > 0)
    currentMachine->soundregs[3]--;

  float delta = 0;
  if (duration != 0 && pause == 0)
    delta = (frequency * (sizeof(sinusWaveBuffer) / 2)) / float(24000);
  
  for(int i = 0; i < 64; i++) {
    uint16_t pos = uint32_t(((i + 1) * delta) + positionLast) % (sizeof(sinusWaveBuffer) / 2);
    short value = sinusWaveBuffer[pos];

    if (i == 63)
      positionLast = pos;

    valueToBuffer(i, value);
  }
}

void Audio::spaceinvaders_render_buffer(void) {
  // Space Invaders discrete audio (based on MAME mw8080bw_a.cpp)
  uint8_t p3 = currentMachine->soundregs[0]; // port 3: UFO(0) Shot(1) Explosion(2) InvaderDie(3) ExtPlay(4)
  uint8_t p5 = currentMachine->soundregs[1]; // port 5: Fleet1(0) Fleet2(1) Fleet3(2) Fleet4(3) UFOhit(4)

  // Fleet: pick highest active bit → tone frequency (Hz)
  // Original hardware: 555 timer ~33-55Hz, doubled for small speaker audibility
  // const int fleet_freq[4] = { 66, 110, 80, 74 };
  const int fleet_freq[4] = { 37, 55, 48, 41};
  int fleet_f = 0;
  for(int b = 3; b >= 0; b--) {
    if(p5 & (1 << b)) { fleet_f = fleet_freq[b]; break; }
  }

  for(int i = 0; i < 64; i++) {
    short value = 0;

    // ── Advance noise LFSR: 17-bit, taps 4+16, clock 7515 Hz ──
    si_noise_clock += 7515;
    while(si_noise_clock >= 24000) {
      si_noise_clock -= 24000;
      int bit = ((si_noise_rng >> 4) ^ (si_noise_rng >> 16)) & 1;
      si_noise_rng = ((si_noise_rng << 1) | bit) & 0x1FFFF;
      si_noise_out = (si_noise_rng >> 12) & 1;
    }

    // ── UFO: SN76477 – SLF triangle ~5.3Hz modulates VCO 1220-3700Hz ──
    if(p3 & 0x01) {
      // SLF triangle: full cycle = 24000/5.3 ≈ 4528 samples
      si_ufo_sweep = (si_ufo_sweep + 1) % 4528;
      int slf_pos = (si_ufo_sweep < 2264) ? si_ufo_sweep : (4528 - si_ufo_sweep);
      int vco_freq = 1220 + (int)((long)slf_pos * 2480 / 2264);
      // VCO square wave: counter += freq, toggle at 12000 (= 24kHz/2)
      si_ufo_cnt += vco_freq;
      if(si_ufo_cnt >= 12000) {
        si_ufo_cnt -= 12000;
        si_ufo_toggle = -si_ufo_toggle;
      }
      value += si_ufo_toggle * 60;
    } 
    else {
      si_ufo_sweep = 0;
    }

    // ── SHOT: original sample playback (12kHz samples, play each twice for 24kHz) ──
    if(p3 & 0x02) {
      if(!si_shot_playing) { si_shot_playing = 1; si_shot_pos = 0; }
      if((si_shot_pos >> 1) < si_sample_shot_LEN) {
        value += si_sample_shot[si_shot_pos >> 1] * 3;
        si_shot_pos++;
      }
    } 
    else {
      si_shot_playing = 0;
    }

    // ── COIN INSERT: metallic clink (triggered via soundregs[2]) ──
    if(currentMachine->soundregs[2] && si_coin_timer == 0) {
      si_coin_timer = 360;  // ~15ms
      si_coin_env = 120;
      currentMachine->soundregs[2] = 0;
    }
    if(si_coin_timer > 0) {
      // Primary metallic tone: 4500Hz
      si_coin_cnt += 4500;
      if(si_coin_cnt >= 12000) {
        si_coin_cnt -= 12000;
        si_coin_toggle = -si_coin_toggle;
      }
      // Overtone for metallic ring: 9500Hz
      si_coin_cnt2 += 9500;
      if(si_coin_cnt2 >= 12000) {
        si_coin_cnt2 -= 12000;
        si_coin_toggle2 = -si_coin_toggle2;
      }
      value += (si_coin_toggle * si_coin_env + si_coin_toggle2 * (si_coin_env / 2)) / 2;
      si_coin_timer--;
      if((si_coin_timer % 12) == 0 && si_coin_env > 5) si_coin_env--;
    }

    // ── EXPLOSION: noise burst with slow decay (RC ~2.7s) ──
    if(p3 & 0x04) {
      if(si_explo_env == 0) si_explo_env = 120;  // init on trigger
      int noise = si_noise_out ? 1 : -1;
      value += noise * si_explo_env;
      // Slow decay: decrease envelope every ~50 samples (~2ms)
      si_explo_cnt++;
      if(si_explo_cnt >= 50) {
        si_explo_cnt = 0;
        if(si_explo_env > 15) si_explo_env--;
      }
    } 
    else {
      si_explo_env = 0;
      si_explo_cnt = 0;
    }

    // ── INVADER DIE: original sample playback (12kHz samples, play each twice for 24kHz) ──
    if(p3 & 0x08) {
      if(!si_invhit_playing) { si_invhit_playing = 1; si_invhit_pos = 0; }
      if((si_invhit_pos >> 1) < si_sample_invhit_LEN) {
        value += si_sample_invhit[si_invhit_pos >> 1] * 3;
        si_invhit_pos++;
      }
    } 
    else {
      si_invhit_playing = 0;
    }

    // ── FLEET MOVEMENT: low bass tone while any fleet bit set ──
    if(fleet_f > 0) {
      si_fleet_cnt += fleet_f;
      if(si_fleet_cnt >= 12000) {
        si_fleet_cnt -= 12000;
        si_fleet_toggle = -si_fleet_toggle;
      }
      value += si_fleet_toggle * 100;
    }

    // ── UFO HIT: descending warble tone ~2000Hz with ~15Hz modulation ──
    if(p5 & 0x10) {
      if(si_ufohit_freq == 0) si_ufohit_freq = 2000;  // init on trigger
      // Warble modulation at ~15Hz: amplitude ±200Hz
      si_ufohit_warble = (si_ufohit_warble + 1) % 1600;  // 24000/15 = 1600
      int warble_pos = (si_ufohit_warble < 800) ?
        (int)si_ufohit_warble : (int)(1600 - si_ufohit_warble);
      int mod_freq = si_ufohit_freq + (warble_pos * 400 / 800 - 200);
      if(mod_freq < 100) mod_freq = 100;
      si_ufohit_cnt += mod_freq;
      if(si_ufohit_cnt >= 12000) {
        si_ufohit_cnt -= 12000;
        si_ufohit_toggle = -si_ufohit_toggle;
      }
      value += si_ufohit_toggle * 100;
      // Descend (~2000→300 over ~1.5s = 36000 samples)
      if(si_ufohit_freq > 300) si_ufohit_freq--;
    } 
    else {
      si_ufohit_freq = 0;
      si_ufohit_warble = 0;
    }

    // Clamp
    if(value > 500) value = 500;
    if(value < -500) value = -500;

    valueToBuffer(i, value);
  }
}

void Audio::galaxian_render_buffer(void) {
  // Galaxian discrete sound hardware emulation (MAME galaxian_a.cpp)
  // SOUND_CLOCK = 18.432MHz/6/2 = 1.536MHz
  //
  // soundregs[0]    = VCO pitch (8-bit, written at 0x7800)
  // soundregs[1-4]  = LFO DAC bits (4-bit, 0x6004-0x6007)
  // soundregs[8-10] = FS1/FS2/FS3 background tone enables (0x6800-0x6802)
  // soundregs[11]   = HIT noise enable (0x6803)
  // soundregs[12]   = (unused, offset 4 not wired)
  // soundregs[13]   = FIRE shoot enable (0x6805, offset 5)
  // soundregs[14]   = VOL1 (0x6806, offset 6)
  // soundregs[15]   = VOL2 (0x6807, offset 7)
  // NOTE: No BGEN register — VCO is always active when pitch is audible
    
  // VOL1/VOL2 control VCO output volume via resistor network
  // Volumcontrol not needed - every sound has its own volume setting here
  unsigned char vol1On = currentMachine->soundregs[14];  // offset 6
  unsigned char vol2On = currentMachine->soundregs[15];  // offset 1

  // VCO half-period: freq = 1.536MHz / (16*(256-pitch))
  // At 24kHz: half_period = (256-pitch) / 8
  unsigned char vco_pitch = currentMachine->soundregs[0];
  unsigned char half_period = (256 - vco_pitch);

  // Detect pitch sweeps (credit sound): VCO plays through R34 base path
  // when pitch is actively changing even without VOL1/VOL2
  static int gal_last_pitch = 0xFF;
  static int gal_pitch_active = 0;
  if(vco_pitch != gal_last_pitch) {
    gal_pitch_active = 500;  // sustain ~20ms (just over 1 frame)
    gal_last_pitch = vco_pitch;
  }
  if(gal_pitch_active > 0) gal_pitch_active--;

  // VCO plays when: VOL is on (normal sounds) OR pitch is sweeping (credit sound)
  char vco_on = (half_period > 1) && (vol1On || vol2On || gal_pitch_active > 0);

  // FS1/FS2/FS3: 555 timer tones (frequencies from RC values)
  // FS1 ~139Hz, FS2 ~190Hz, FS3 ~267Hz // {86, 63, 44}
  // 24.000Hz / 130Hz = 184 / 2 = 92
  // Half-periods at 24kHz sample rate
  static const unsigned char fs_period[3] = {86, 63, 44}; 
  unsigned char lfo_val = ((currentMachine->soundregs[1] & 0x01) << 0);
  lfo_val |= ((currentMachine->soundregs[2] & 0x01) << 1);
  lfo_val |= ((currentMachine->soundregs[3] & 0x01) << 2);
  lfo_val |= ((currentMachine->soundregs[4] & 0x01) << 3);

  if (lfo_val > 2) {
    lfo_counter++;
    if ((lfo_counter % (lfo_val * 3)) == 0) {
      lfo++;
      if (lfo > 10)
        lfo = 0;
    }
  }
  else {
    lfo = 0;  
  }

  for(int i = 0; i < 64; i++) {
    short value = 0;

    // === VCO tone ===
    if(vco_on) {
      for (int i=0; i < 8; i++) {
        gal_tone_cnt++;
        if(gal_tone_cnt >= half_period) {
          gal_tone_cnt = 0;
          gal_tone_toggle = -gal_tone_toggle;
        }
      }
      value += gal_tone_toggle * 100;
    }

    // === FS1, FS2, FS3: background march tones (independent volume) ===
    for(int fs = 0; fs < 3; fs++) {
      if(currentMachine->soundregs[8 + fs]) {
        gal_fs_cnt[fs]++;
        if(gal_fs_cnt[fs] >= fs_period[fs] + lfo) {
          gal_fs_cnt[fs] = 0;
          gal_fs_toggle[fs] = -gal_fs_toggle[fs];
        }
        value += gal_fs_toggle[fs] * 25;
      }
    }

    // === HIT: explosion noise (LFSR, bandpass ~470Hz) ===
    if(currentMachine->soundregs[11]) {
      uint32_t b = ((gal_noise_rng >> 0) ^ (gal_noise_rng >> 3)) & 1;
      gal_noise_rng = (gal_noise_rng >> 1) | (b << 16);
      value += ((gal_noise_rng & 1) ? 90 : -90);
    }

    // === FIRE: shooting sound (offset 5, 555 astable ~2.7kHz + noise) ===
    if(currentMachine->soundregs[13]) {
      gal_fire_cnt++;
      if(gal_fire_cnt >= 9) {
        gal_fire_cnt = 0;
        uint32_t b = ((gal_fire_rng >> 0) ^ (gal_fire_rng >> 3)) & 1;
        gal_fire_rng = (gal_fire_rng >> 1) | (b << 16);
      }
      value += ((gal_fire_rng & 1) ? 70 : -70);
    }

    valueToBuffer(i, value);
  }
}

// ============================================================================
// PHOENIX discrete audio @ 24 kHz mono — porting FEDELE da MAME
// src/mame/phoenix/phoenix_a.cpp (phoenix_sound_device + phoenix_discrete netlist).
// Ogni stadio replica la fisica RC/555 esatta del circuito reale (non uno square
// wave approssimato). Sostituisce la precedente approssimazione "Galaxian-style".
//
// Mappa registri (raw latch, settati da Phoenix::wrZ80):
//   soundregs[0] = sound A (0x6000): bit0-3=PHOENIX_EFFECT_2_DATA, bit4-5=EFFECT_2_FREQ,
//                  bit6-7=noise gen (C24/C25 charge/discharge, "future effect 3/4")
//   soundregs[1] = sound B (0x6800): bit0-3=PHOENIX_EFFECT_1_DATA, bit4=EFFECT_1_FREQ,
//                  bit5=EFFECT_1_FILT, bit6-7=melody tune select (MM6221AA)
//
// Catena Effect 1 (comment originale: "shield, bird explode, level 3&4 siren,
// level 5 spaceship"): NODE_20 RCDISC4 (inviluppo pitch pilotato da FREQ bit) ->
// NODE_21 555 CV -> NODE_22 NOTE (LS163+preload DATA) -> switch/filtro (FILT bit).
//
// Catena Effect 2 (comment originale: "bird flying, bird/phoenix/spaceship hit,
// phoenix wing hit"): NODE_30 selezione capacita' (FREQ 2-bit) -> NODE_33/34 due
// 555 fissi -> mixer resistivo -> NODE_37 filtro lento (~0.3s) -> NODE_38 mixer ->
// NODE_39 555 CV -> NODE_40 NOTE (preload DATA) -> * livello ampiezza (FREQ bit alto).
//
// Rumore (bit 6-7 sound A, MAI usato dall'approssimazione precedente): NE555 con
// doppio inviluppo RC (C24 ~0.136s discharge, C25 ~0.32s discharge) che modula la
// frequenza di un LFSR 18-bit tra 588-6325 Hz (porting diretto di noise()/update_c24/
// update_c25 da phoenix_a.cpp, invariato).
//
// Melody (MM6221AA, bit 6-7 sound B): approssimazione mantenuta da SPINNERINO (4 tune
// brevi, chip melodia reale non disponibile come sorgente separato).
//
// NOTE: alcune costanti MAME non verificabili da qui (DEFAULT_TTL_V_LOGIC_1=3.4,
// OP_AMP_VP_RAIL_OFFSET=1.5) sono valori standard assunti: influenzano solo il
// bilanciamento fine dei livelli, non la frequenza/tempistica (che e' esatta).
// Guadagno finale PHOENIX_MASTER_SCALE tarabile se il volume risultasse squilibrato.
//
// PERFORMANCE (fix task watchdog su HW): tutta la catena e' in float con gli
// esponenziali RC del passo pieno PRECALCOLATI (le combinazioni R/C sono note a
// priori). L'ESP32 ha la FPU solo single-precision: la prima stesura in double
// (exp/log software, ~migliaia di cicli l'una, ~10 per sample a 24kHz) superava
// il budget dell'intero core -> transmit() non riempiva mai il DMA piu' veloce
// dell'I2S, loopTask restava nel do/while e scattava il task watchdog (reboot).
// exp/log runtime sopravvivono solo nella correzione sub-sample dei crossing
// (~1-3 logf per sample) e nel raro fallback di ph_exp1m.
// ============================================================================

// Parametri precalcolati di un 555 astabile (R/C fissi per ogni istanza).
struct ph555Params {
  float exp_charge;        // 1-exp(-dt/tau) per il passo pieno da 1/24000s
  float exp_discharge;
  float tau_charge;        // (r1+r2)*c
  float tau_discharge;     // r2*c
  float inv_tau_charge;
  float inv_tau_discharge;
};

static ph555Params ph_make_555(float r1, float r2, float c) {
  const float dt = 1.0f / 24000.0f;
  ph555Params p;
  p.tau_charge        = (r1 + r2) * c;
  p.tau_discharge     = r2 * c;
  p.inv_tau_charge    = 1.0f / p.tau_charge;
  p.inv_tau_discharge = 1.0f / p.tau_discharge;
  p.exp_charge        = 1.0f - expf(-dt * p.inv_tau_charge);
  p.exp_discharge     = 1.0f - expf(-dt * p.inv_tau_discharge);
  return p;
}

// 1-exp(-x): serie troncata al 5° ordine per x<=1 (errore <0.1%), usata per il
// tempo residuo dopo un crossing (dt < passo pieno). Fallback expf per x>1
// (solo il 555 CV di Effect2 in scarica puo' arrivarci, tau < dt_full).
static inline float ph_exp1m(float x) {
  if (x > 1.0f) return 1.0f - expf(-x);
  return x * (1.0f - 0.5f*x*(1.0f - 0.333333f*x*(1.0f - 0.25f*x*(1.0f - 0.2f*x))));
}

// -- 555 astabile (dsd_555_astbl): RC charge/discharge esatto con correzione
// dell'overshoot in tempo continuo. ctrlv<0 => soglie fisse (v_pos*2/3, v_pos/3);
// ctrlv>=0 => CV-modulato (threshold=ctrlv, trigger=ctrlv/2).
// energy_mode=false -> ritorna count_f+x_time (DISC_555_OUT_COUNT_F_X, per NOTE);
// energy_mode=true  -> ritorna v_out_high*(duty) (DISC_555_OUT_ENERGY, per i mixer).
static float ph_555_step(float &cap_v, uint8_t &ff, const ph555Params &p,
                         float v_pos, float v_charge, float ctrlv,
                         bool energy_mode, float v_out_high) {
  const float dt_full = 1.0f / 24000.0f;
  if (ctrlv >= 0 && ctrlv < 0.25f)
    return energy_mode ? (ff ? 0.0f : v_out_high) : 0.0f;

  float threshold = (ctrlv >= 0) ? ctrlv          : (v_pos * (2.0f / 3.0f));
  float trigger   = (ctrlv >= 0) ? (0.5f * ctrlv) : (v_pos * (1.0f / 3.0f));
  int count_f = 0;

  if (ctrlv >= 0) {
    if (cap_v >= threshold)      { ff = 0; count_f++; }
    else if (cap_v <= trigger)   { ff = 1; }
  }

  float dt     = dt_full;
  float v_cap  = cap_v;
  float x_time = 0;
  bool  full_step = true;   // primo giro: esponente precalcolato del passo pieno
  int   guard = 8;          // rete di sicurezza: mai piu' di 8 crossing per sample

  do {
    if (ff) {
      float exponent   = full_step ? p.exp_charge : ph_exp1m(dt * p.inv_tau_charge);
      float v_cap_next = v_cap + (v_charge - v_cap) * exponent;
      dt = 0;
      if (v_cap_next >= threshold) {
        float denom = v_charge - v_cap;
        if (denom > 1e-9f) {
          float f = (v_cap_next - threshold) / denom;
          if (f > 0.999f) f = 0.999f;
          dt = p.tau_charge * logf(1.0f / (1.0f - f));
        }
        x_time = dt;
        v_cap_next = threshold;
        ff = 0;
        count_f++;
      }
      v_cap = v_cap_next;
    } else {
      float exponent   = full_step ? p.exp_discharge : ph_exp1m(dt * p.inv_tau_discharge);
      float v_cap_next = v_cap - v_cap * exponent;
      dt = 0;
      if (v_cap_next <= trigger) {
        if (v_cap_next < trigger && v_cap > 1e-9f) {
          float f = (trigger - v_cap_next) / v_cap;
          if (f > 0.999f) f = 0.999f;
          dt = p.tau_discharge * logf(1.0f / (1.0f - f));
        }
        x_time = dt;
        v_cap_next = trigger;
        ff = 1;
      }
      v_cap = v_cap_next;
    }
    full_step = false;
  } while (dt > 0 && --guard);

  cap_v = v_cap;
  float x_frac = x_time * 24000.0f;   // = x_time / dt_full

  if (energy_mode) {
    float xt = (x_frac == 0) ? 1.0f : x_frac;
    return v_out_high * (ff ? xt : (1.0f - xt));
  }
  return count_f ? (count_f + x_frac) : 0.0f;
}

// -- NOTE generator (dss_note, DISC_CLK_BY_COUNT | DISC_OUT_IS_ENERGY): contatore
// LS163 che conta da 'data' (preload) a 15 poi ribalta count2 (0/1) -- la nota
// suonata e' il toggle di count2. clock_in = output combinato del 555 (count_f+x_time).
static float ph_note_step(int &count1, int &count2, float clock_in, int data) {
  int   clock  = (int)clock_in;
  float x_time = clock_in - clock;
  int   last_count2 = count2;

  if (data != 15) {
    for (int k = 0; k < clock; k++) {
      count1++;
      if (count1 > 15) { count1 = data; count2 += 1; if (count2 > 1) count2 = 0; }
    }
  }

  float v_out = count2;
  if (count2 != last_count2) {
    if (x_time == 0) x_time = 1.0f;
    v_out = last_count2;
    if (count2 > last_count2) v_out += (count2 - last_count2) * x_time;
    else                      v_out -= (last_count2 - count2) * x_time;
  }
  return v_out;
}

// -- RCDISC4 tipo 1 (NODE_20): inviluppo di tensione per il pitch-bend di Effect 1,
// pilotato dal bit FREQ. Costanti precalcolate da R22=470,R23=100k,R24=33k,C7=6.8uF,VP=12V.
static float ph_rcdisc4_step(float &vc1, int freq_bit) {
  static const float dt   = 1.0f / 24000.0f;
  static const float V    = 12.0f - 0.5f;                                    // VP - diode drop
  static const float r1p3 = (470.0f * 33000.0f) / (470.0f + 33000.0f);       // R1||R3
  static const float rT1  = 100000.0f + r1p3;                                // R2+r
  static const float m_v1 = (V / rT1) * r1p3 + 0.5f;
  static const float rT1b = (100000.0f * r1p3) / (100000.0f + r1p3);         // R2||r
  static const float exp1 = 1.0f - expf(-dt / (rT1b * 6.8e-6f));
  static const float rT0  = 100000.0f + 33000.0f;                            // R2+R3
  static const float m_v0 = (V / rT0) * 33000.0f + 0.5f;
  static const float rT0b = (100000.0f * 33000.0f) / (100000.0f + 33000.0f); // R2||R3
  static const float exp0 = 1.0f - expf(-dt / (rT0b * 6.8e-6f));

  float target   = freq_bit ? m_v1 : m_v0;
  float exponent = freq_bit ? exp1 : exp0;
  vc1 += (target - vc1) * exponent;
  if (vc1 > 10.5f) vc1 = 10.5f;   // max_out = VP(12) - OP_AMP_VP_RAIL_OFFSET(1.5)
  if (vc1 < 0)     vc1 = 0;
  return vc1;
}

// -- RC filter a un polo (dst_rcfilter): v += (vin-v)*exponent, exponent precalcolato dal chiamante.
static inline float ph_rcfilter_step(float &v_out, float vin, float exponent) {
  v_out += (vin - v_out) * exponent;
  return v_out;
}

// -- Noise generator NE555 + doppio inviluppo RC (porting diretto di update_c24/
// update_c25/noise da phoenix_a.cpp, invariato). Pilotato da bit 6/7 del sound A latch.
static int32_t ph_update_c24(int32_t &level, long &counter, bool bit40) {
  static const double C24 = 6.8e-6, R49 = 1000, R51 = 330, R52 = 20000;
  if (bit40) {
    if (level > 0) {
      counter -= (long)((level - 0) / (R52 * C24));
      if (counter <= 0) {
        long n = -counter / 24000 + 1;
        counter += n * 24000;
        level -= n; if (level < 0) level = 0;
      }
    }
  } else {
    if (level < 32767) {
      counter -= (long)((32767 - level) / ((R51 + R49) * C24));
      if (counter <= 0) {
        long n = -counter / 24000 + 1;
        counter += n * 24000;
        level += n; if (level > 32767) level = 32767;
      }
    }
  }
  return 32767 - level;
}

static int32_t ph_update_c25(int32_t &level, long &counter, bool bit80) {
  static const double C25 = 6.8e-6, R50 = 1000, R53 = 330, R54 = 47000;
  if (bit80) {
    if (level < 32767) {
      counter -= (long)((32767 - level) / ((R50 + R53) * C25));
      if (counter <= 0) {
        long n = -counter / 24000 + 1;
        counter += n * 24000;
        level += n; if (level > 32767) level = 32767;
      }
    }
  } else {
    if (level > 0) {
      counter -= (long)((level - 0) / (R54 * C25));
      if (counter <= 0) {
        long n = -counter / 24000 + 1;
        counter += n * 24000;
        level -= n; if (level < 0) level = 0;
      }
    }
  }
  return level;
}

void Audio::phoenix_render_buffer(void) {
  static const uint16_t TUNE_ROMANCE[] = {
    440, 0, 440, 0, 440, 0,        // A4 A4 A4
    440, 0, 392, 0, 349, 0,        // A4 G4 F4
    349, 0, 330, 0, 294, 0,        // F4 E4 D4
    294, 0, 349, 0, 440, 0,        // D4 F4 A4
    587, 0,   0, 0,   0, 0,        // D5 [sustain lungo]
    587, 0, 523, 0, 466, 0,        // D5 C5 Bb4
    466, 0, 440, 0, 392, 0,        // Bb4 A4 G4
    392, 0, 440, 0, 466, 0,        // G4 A4 Bb4
    440, 0, 466, 0, 440, 0,        // A4 Bb4 A4
    554, 0, 466, 0, 440, 0,        // C#5 Bb4 A4
    440, 0, 392, 0, 349, 0,        // A4 G4 F4
    349, 0, 330, 0, 294, 0,        // F4 E4 D4
    330, 0, 330, 0, 330, 0,        // E4 E4 E4
    330, 0, 349, 0, 330, 0,        // E4 F4 E4
    294, 0, 349, 0, 440, 0,        // D4 F4 A4
    587, 0,   0, 0,   0, 0,        // D5 [sustain finale]
    0xFFFF
  };
  // TUNE_ROMANCE dura 96 voci * 0.2s = 19.2s e (confermato dall'utente) suona per
  // intero senza tagli al livello 1 — corretto per confronto con MAME. FUR_ELISE e
  // WARNING erano invece placeholder brevi (~1.6-1.8s, singola ripetizione del
  // motivo) che si interrompevano molto prima della melodia reale: il motivo viene
  // ripetuto qui per raggiungere una durata paragonabile (~18-19s), NON in loop a
  // runtime (nessuna ripetizione oltre il terminatore 0xFFFF, e' un unico array
  // piu' lungo scritto per intero, coerente con "un brano lungo suonato una volta").
  static const uint16_t TUNE_FUR_ELISE[] = {
    659, 622, 659, 622, 659, 494, 587, 523, 440,   // ripetuto 10x = 90 voci = 18.0s
    659, 622, 659, 622, 659, 494, 587, 523, 440,
    659, 622, 659, 622, 659, 494, 587, 523, 440,
    659, 622, 659, 622, 659, 494, 587, 523, 440,
    659, 622, 659, 622, 659, 494, 587, 523, 440,
    659, 622, 659, 622, 659, 494, 587, 523, 440,
    659, 622, 659, 622, 659, 494, 587, 523, 440,
    659, 622, 659, 622, 659, 494, 587, 523, 440,
    659, 622, 659, 622, 659, 494, 587, 523, 440,
    659, 622, 659, 622, 659, 494, 587, 523, 440,
    0xFFFF
  };
  static const uint16_t TUNE_WARNING[] = {
    523, 784, 523, 784, 523, 784, 523, 784,        // ripetuto 12x = 96 voci = 19.2s
    523, 784, 523, 784, 523, 784, 523, 784,
    523, 784, 523, 784, 523, 784, 523, 784,
    523, 784, 523, 784, 523, 784, 523, 784,
    523, 784, 523, 784, 523, 784, 523, 784,
    523, 784, 523, 784, 523, 784, 523, 784,
    523, 784, 523, 784, 523, 784, 523, 784,
    523, 784, 523, 784, 523, 784, 523, 784,
    523, 784, 523, 784, 523, 784, 523, 784,
    523, 784, 523, 784, 523, 784, 523, 784,
    523, 784, 523, 784, 523, 784, 523, 784,
    523, 784, 523, 784, 523, 784, 523, 784,
    0xFFFF
  };
  static const uint16_t* TUNES[4] = {
    TUNE_ROMANCE, TUNE_WARNING, TUNE_FUR_ELISE, TUNE_ROMANCE
  };

  // Guadagno finale: catena analogica reale ~x40000 (gain del mixer MAME) * 0.6
  // (route gain del device "discrete" nel machine_config) rescalato /64 per il
  // contratto +/-512 di valueToBuffer(). Unico numero da ritarare se serve.
  const float PHOENIX_MASTER_SCALE = 40000.0f * 0.6f / 64.0f;

  uint8_t a = currentMachine->soundregs[0];   // sound A raw latch (0x6000)
  uint8_t b = currentMachine->soundregs[1];   // sound B raw latch (0x6800)

  // Melody trigger: MAME chiama mm6221aa_tune_w(data>>6) ad OGNI scrittura di
  // control_b_w, ma quello stesso latch porta anche Effect1 (bit0-5), riscritto
  // di continuo durante il gioco (es. esplosione navetta) — verificato che
  // questo produce letture transitorie con bit6-7=0 non legate alla melodia
  // (il gioco NON mantiene sempre una shadow-copy coerente). Il chip MM6221AA
  // reale suona il brano scelto UNA VOLA SOLA fino alla fine (confermato: non
  // va in loop, ~10s+, vedi TUNE_ROMANCE) e non si riavvia/interrompe per una
  // fluttuazione di un frame su un latch condiviso. Quindi:
  //  - (ri)parte da capo SOLO se il valore non-zero richiesto e' DIVERSO dalla
  //    tune ATTUALMENTE IN RIPRODUZIONE (non dall'ultimo valore letto: una
  //    rilettura dello stesso numero, anche dopo uno zero spurio, non tocca
  //    l'indice e lascia proseguire la riproduzione in corso);
  //  - un valore 0 non ferma mai nulla: la melodia si ferma SOLO da sola al
  //    proprio terminatore 0xFFFF (gestito piu' sotto).
  uint8_t new_tune = (b >> 6) & 0x03;
  if (new_tune != 0 && (!ph_mel_active || ph_mel_tune != new_tune)) {
    ph_mel_tune = new_tune; ph_mel_idx = 0; ph_mel_timer = 1; ph_mel_active = true;
  }

  int   e1_data = b & 0x0F;
  int   e1_freq = (b >> 4) & 0x01;
  int   e1_filt = (b >> 5) & 0x01;
  int   e2_data = a & 0x0F;
  int   e2_freq = (a >> 4) & 0x03;
  bool  noise_b40 = (a & 0x40) != 0;
  bool  noise_b80 = (a & 0x80) != 0;

  // NODE_30: capacita' selezionata per Effect2 (COMP_ADDER, cDefault=C18=0.01uF)
  float node30_c = 0.01e-6f;
  if (e2_freq & 1) node30_c += 0.47e-6f;   // C16
  if (e2_freq & 2) node30_c += 1.0e-6f;    // C17
  // NODE_31/32: bit alto di FREQ -> livello ampiezza Effect2 (SWITCH)
  float node32_level = (e2_freq & 2) ? (3.4f / 2.0f) : 3.4f;

  // Parametri 555 precalcolati. e1/e2b/e2cv hanno R/C fissi -> calcolati una volta
  // sola (static). e2a dipende da node30_c (cambia con e2_freq) -> ricalcolato ad
  // ogni chiamata (poche volte al secondo, non per-sample).
  static const ph555Params p_e1_555   = ph_make_555(47000.0f, 47000.0f, 1e-9f);
  static const ph555Params p_e2_555b  = ph_make_555(510000.0f, 510000.0f, 1e-6f);
  static const ph555Params p_e2_555cv = ph_make_555(20000.0f, 20000.0f, 1e-9f);
  const ph555Params p_e2_555a = ph_make_555(47000.0f, 100000.0f, node30_c);

  // Precalcolati (fissi): RCFILTER NODE_25 (Effect1 filtro), NODE_37 (Effect2 lento),
  // e i 3 canali del mixer finale (DC-block).
  static const float dt = 1.0f / 24000.0f;
  static const float exp_node25 = 1.0f - expf(-dt / ((1.0f/(1.0f/10000.0f+1.0f/100000.0f)) * 0.047e-6f));
  static const float exp_node37 = 1.0f - expf(-dt / (3051.98f * 100e-6f));
  static const float exp_mix1   = 1.0f - expf(-dt / (8507.46f * 10e-6f));   // canale Effect1
  static const float exp_mix2   = 1.0f - expf(-dt / (7500.0f  * 10e-6f));   // canale Effect2
  static const float exp_camp   = 1.0f - expf(-dt / (6628.0f  * 10e-6f));   // cAmp finale

  for (int i = 0; i < 64; i++) {
    // ── Rumore NE555 (bit 6-7 sound A) ──
    int32_t vc24 = ph_update_c24(ph_c24_level, ph_c24_counter, noise_b40);
    int32_t vc25 = ph_update_c25(ph_c25_level, ph_c25_counter, noise_b80);
    int32_t noise_level = (vc24 < vc25) ? (vc24 + (vc25 - vc24) / 2) : (vc25 + (vc24 - vc25) / 2);
    int32_t noise_freq  = 588 + 6325 * noise_level / 32768;
    ph_noise_counter -= noise_freq;
    if (ph_noise_counter <= 0) {
      long n = (-ph_noise_counter / 24000) + 1;
      ph_noise_counter += n * 24000;
      for (long k = 0; k < n; k++) {
        int fb = (((ph_noise_shiftreg >> 16) & 1) == ((ph_noise_shiftreg >> 17) & 1)) ? 1 : 0;
        ph_noise_shiftreg = (ph_noise_shiftreg << 1) | fb;
      }
      ph_noise_polybit = ph_noise_shiftreg & 1;
    }
    int32_t noise_sum = 0;
    if (!ph_noise_polybit) noise_sum += vc24;
    ph_noise_lp_counter -= 400;
    if (ph_noise_lp_counter <= 0) { ph_noise_lp_counter += 24000; ph_noise_lp_polybit = ph_noise_polybit; }
    if (!ph_noise_lp_polybit) noise_sum += vc25;
    float noise_out = ((float)(noise_sum / 2) - 16384.0f) / 64.0f;   // centrato + rescale

    // ── Effect 1 (sound B): RCDISC4 -> 555 CV -> NOTE -> switch/filtro ──
    ph_rcdisc4_step(ph_e1_vc1, e1_freq);
    float node21 = ph_555_step(ph_e1_555_cap, ph_e1_555_ff, p_e1_555,
                                5.0f, 5.0f, ph_e1_vc1, false, 0.0f);
    float node22 = ph_note_step(ph_e1_note_c1, ph_e1_note_c2, node21, e1_data);
    float node23 = e1_filt ? (3.4f * 100000.0f / 110000.0f) : 3.4f;
    float node24 = node22 * node23;
    float node25 = ph_rcfilter_step(ph_e1_rcfilt, node24, exp_node25);
    float effect1_snd = e1_filt ? node25 : node24;

    // ── Effect 2 (sound A): doppio 555 -> mixer -> filtro lento -> 555 CV -> NOTE ──
    float node33 = ph_555_step(ph_e2_555a_cap, ph_e2_555a_ff, p_e2_555a,
                                5.0f, 5.0f, -1.0f, true, 4.0f);
    float node34 = ph_555_step(ph_e2_555b_cap, ph_e2_555b_ff, p_e2_555b,
                                5.0f, 5.0f, -1.0f, true, 4.0f);
    float node35 = (node33/10000.0f + node34/10200.0f + 5.0f/5000.0f) /
                    (1.0f/10000.0f + 1.0f/10200.0f + 1.0f/5000.0f);
    float node36 = (node34 + node35) / 2.0f;   // mixer2, R45=R46 -> media semplice
    float node37 = ph_rcfilter_step(ph_e2_rcfilt, node36, exp_node37);
    float node38 = (node33/10000.0f + node37/5100.0f + 5.0f/5000.0f) /
                    (1.0f/10000.0f + 1.0f/5100.0f + 1.0f/5000.0f);
    float node39 = ph_555_step(ph_e2_555cv_cap, ph_e2_555cv_ff, p_e2_555cv,
                                5.0f, 5.0f, node38, false, 0.0f);
    float node40 = ph_note_step(ph_e2_note_c1, ph_e2_note_c2, node39, e2_data);
    float effect2_snd = node40 * node32_level;

    // ── Mixer finale (DISCRETE_MIXER4): DC-block per canale + Millman + cAmp + gain ──
    float vt1 = effect1_snd; ph_mix_vcap1 += (vt1 - ph_mix_vcap1) * exp_mix1; vt1 -= ph_mix_vcap1;
    float vt2 = effect2_snd; ph_mix_vcap2 += (vt2 - ph_mix_vcap2) * exp_mix2; vt2 -= ph_mix_vcap2;
    float mix_i = vt1/57000.0f + vt2/30000.0f;
    float mix_v = mix_i * 6628.0f;
    ph_mix_vcamp += (mix_v - ph_mix_vcamp) * exp_camp;
    mix_v -= ph_mix_vcamp;

    float value = mix_v * PHOENIX_MASTER_SCALE + noise_out;

    // ── Melodia MM6221AA (approssimazione, invariata da SPINNERINO) ──
    // Il chip reale suona il brano in loop continuo finche' resta selezionato
    // (viene fermato solo dal trigger sopra, quando il gioco seleziona tune=0).
    // I nostri array sono approssimazioni brevi (~1.6-1.8s): al terminatore
    // 0xFFFF: NON e' un loop (confermato via confronto diretto con MAME dall'utente
    // — la melodia reale e' un brano lungo (~10s+) che suona una volta sola, non si
    // ripete). Il vero bug era che i nostri array TUNE_WARNING/TUNE_FUR_ELISE erano
    // placeholder troppo corti (~1.6-1.8s): allungati per coprire l'intera durata.
    if (ph_mel_active) {
      if (--ph_mel_timer == 0) {
        const uint16_t* t = TUNES[ph_mel_tune & 0x03];
        uint16_t f = t[ph_mel_idx];
        if (f == 0xFFFF) { ph_mel_active = false; ph_mel_freq = 0; }
        else {
          if (f != 0) ph_mel_freq = f;
          ph_mel_idx++;
          if (ph_mel_idx >= 100) ph_mel_idx = 99;
          ph_mel_timer = 24000 / 5;
        }
      }
      if (ph_mel_freq) {
        ph_mel_phase += (uint32_t)ph_mel_freq * 65536U / 24000U;
        int16_t m  = (ph_mel_phase & 0x8000) ? 28 : -28;
        int16_t m2 = ((ph_mel_phase << 1) & 0x8000) ? 14 : -14;
        value += m + m2;
      }
    }

    if (value > 512)  value = 512;
    if (value < -512) value = -512;
    valueToBuffer(i, (short)value);
  }
}

void Audio::dkong3_render_buffer(void) {
  dkong3 *dk3 = static_cast<dkong3*>(currentMachine);
  for (int i = 0; i < 64; i++) {
    short value = 0;
    if (dk3->dk3_rptr != dk3->dk3_wptr) {
      value = dk3->dk3_samples[dk3->dk3_rptr];
      dk3->dk3_rptr = (dk3->dk3_rptr + 1) & (dkong3::DK3_SAMPLES - 1);
    }
    valueToBuffer(i, value);
  }
}

void Audio::valueToBuffer(int index, short value) {
  if (muted) value = 0;

  // value is now in the range of +/- 512, so expand to +/- 15 bit
  value = value * 64;

#ifdef SND_DIFF
  // generate differential output
  snd_buffer[2 * index]   = 0x8000 + (value / volumeSetting);    // positive signal on GPIO26
  snd_buffer[2 * index + 1] = 0x8000 - (value / volumeSetting);    // negatve signal on GPIO25 
#else
  // work-around weird byte order bug, see 
  // https://github.com/espressif/arduino-esp32/issues/8467#issuecomment-1656616015
  snd_buffer[index ^ 1]   = 0x8000 + (value / volumeSetting); 
#endif
}
