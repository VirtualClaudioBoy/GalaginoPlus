/*
 * Galagino Plus - Mini arcade for ESP32 and Platformio
 *
 * (c) 2026 VirtualClaudioBoy
 *
 * This is a port of Speckhoiler's Galagino
 * https://github.com/speckhoiler/galagino
 * which in turn is a port of Till Harbaum's Galagino ported to platformio.
 * https://github.com/harbaum/galagino
 *
 * Published under GPLv3
 *
 */
#include <Arduino.h>
#include "config.h"
#include "machines.h"
#include "machines/machineBase.h"
#include "emulation/audio.h"
#include "emulation/video.h"
#include "emulation/input.h"
#include "emulation/menu.h"
#include "emulation/emulation.h"
#include "emulation/hiscore.h"
#ifdef LED_PIN
  #include "emulation/led.h"
#endif

signed char machinesCount = (signed char)(sizeof(machines) / sizeof(unsigned short*));

machineBase *currentMachine;

// the hardware supports 64 sprites
struct sprite_S *sprite_buffer;

// buffer space for one row of 28 characters
unsigned short *frame_buffer;

// RAM
unsigned char *memory;

#ifdef AUTOFIRE_ENABLED
// Autofire whitelist: only games where repeated fire is useful are enabled.
static bool machineHasAutofire(machineBase *machine) {
  switch (machine->machineType()) {
    case MCH_1942:
    case MCH_EYES:
    case MCH_GALAGA:
    case MCH_GALAXIAN:
    case MCH_GAPLUS:
    case MCH_GYRUSS:
    case MCH_MOONCRESTA:
    case MCH_PHOENIX:
    case MCH_SCRAMBLE:
    case MCH_SPACE:
    case MCH_STARFORCE:
    case MCH_SUPERCOBRA:
    case MCH_TIMEPLT:
    case MCH_TUTANKHM:
    case MCH_VANGUARD:
    case MCH_XEVIOUS:
      return true;
    default:
      return false;
  }
}
#endif

Audio audio = Audio();
Video video = Video();
Input input = Input();
Menu menu = Menu();
Hiscore hiscore = Hiscore();
#ifdef LED_PIN
  Led led = Led();
#endif

void updateAudioVideo(void);
void renderRow(short row, bool isMenu);
void onDoAttractReset();
void onVolumeUpDown(bool up, bool down);
void onDoReset();
bool doReset = false;

void setup() {
  Serial.begin(115200);
  Serial.println("Galagino"); 

  Serial.print("ESP-IDF "); 
  Serial.println(ESP_IDF_VERSION, HEX); 

#ifdef WORKAROUND_I2S_APLL_PROBLEM
  Serial.println("I2S APLL workaround active"); 
#endif
  // this should not be needed as the CPU runs by default on 240Mht nowadays
  setCpuFrequencyMhz(240);

  Serial.print("Free heap: "); Serial.println(ESP.getFreeHeap());
  Serial.print("Main core: "); Serial.println(xPortGetCoreID());
  Serial.print("Main priority: "); Serial.println(uxTaskPriorityGet(NULL));  

  // allocate memory for a single tile/character row
  frame_buffer = (unsigned short*)malloc(240 * 8 * 2);
  sprite_buffer = (sprite_S*)malloc(128 * sizeof(sprite_S));
  memory = (uint8_t *)malloc(RAMSIZE);
  currentMachine = machines[0];
  
  for (int i = 0; i < machinesCount; i++)
    machines[i]->init(&input, frame_buffer, sprite_buffer, memory);

  audio.init();
  audio.start(currentMachine);

  input.init(machinesCount == 1);
  input.onVolumeUpDown(onVolumeUpDown);
  input.onDoReset(onDoReset);
  input.onDoAttractReset(onDoAttractReset);
#ifdef AUTOFIRE_ENABLED
  input.setAutofireDisabled(!machineHasAutofire(currentMachine));
#endif

  menu.init(&input, machines, machinesCount, frame_buffer);
  hiscore.init();
#ifdef LED_PIN
  led.init();
#endif

  video.begin();
  Serial.print("Free heap: "); Serial.println(ESP.getFreeHeap());
}

void loop(void) {  
  // run video in main task. This will send signals to the emulation task in the background to synchronize video
  updateAudioVideo(); 

#ifdef LED_PIN
  led.update(machines, menu.machineIndexPreselection(), menu.machineIndexSelected());
#endif
}

void updateAudioVideo(void) {
  uint32_t t0 = micros();

  bool isMenu = menu.machineIndexIsMenu();
#ifdef MASTER_ATTRACT_MUTE_AUDIO
  audio.mute(!isMenu && menu.isAttractPlay());
#endif
  if(isMenu) {
    menu.handle();
  }
  else {
    if (menu.startMachine()) {
      currentMachine = machines[menu.machineIndexSelected()];
      audio.start(currentMachine);
#ifdef AUTOFIRE_ENABLED
      input.setAutofireDisabled(!machineHasAutofire(currentMachine));
#endif
      video.flip(currentMachine->videoFlipY(), currentMachine->videoFlipX());
        
      // start new machine
      emulation_start();
      hiscore.start(currentMachine);
    }
    currentMachine->prepare_frame();
    hiscore.frame();
  }

  if (doReset || menu.attract_gameTimeout()) {
    // stop current machine. Save high scores first: stopping resets the machine RAM
    hiscore.stop();
    emulation_stop();
    video.flipReset(currentMachine->videoFlipY(), currentMachine->videoFlipX());

    menu.show_menu();
    doReset = false;
  }

  bool videoHalfRate = true;
#ifndef VIDEO_HALF_RATE
  videoHalfRate = currentMachine->useVideoHalfRate() && !isMenu;
#endif
  const unsigned short renderWidth = (!isMenu && currentMachine->machineType() == MCH_SCREGG) ? 240 : 224;
  video.setViewport(renderWidth);

  if (!videoHalfRate) {
    // render and transmit screen at once as the display running at 80Mhz can update at full 60 hz game frame
    for(int c = 0; c < 36; c += 6) {
      for (int i = 0; i < 6; i++) {
        renderRow(c + i, isMenu); video.write(frame_buffer, renderWidth * 8);
      }     

      // audio is updated 6 times per 60 Hz frame
      audio.transmit();
    } 
 
    emulation_videoRendered();

    // one screen at 60 Hz is 16.6ms
    unsigned long t1 = (micros() - t0) / 1000;  // calculate time in milliseconds
    if(t1 < 16) 
      vTaskDelay(16 - t1);
    else
      vTaskDelay(1);    // at least 1 ms delay to prevent watchdog timeout

    // physical refresh is 60Hz. So send vblank trigger once a frame
    emulation_notifyGive();
  }
  else {
    // render and transmit screen in two halfs as the display running at 40Mhz can only update every second 60 hz game frame
    for(int half = 0; half < 2; half++) {
      for(int c = 18 * half; c < 18 * (half + 1); c += 3) {
        renderRow(c + 0, isMenu); video.write(frame_buffer, renderWidth * 8);
        renderRow(c + 1, isMenu); video.write(frame_buffer, renderWidth * 8);
        renderRow(c + 2, isMenu); video.write(frame_buffer, renderWidth * 8);

        // audio is refilled 6 times per screen update. The screen is updated
        // every second frame. So audio is refilled 12 times per 30 Hz frame.
        // Audio registers are udated by CPU3 two times per 30hz frame.
        audio.transmit();
      } 
    
      emulation_videoRendered();

      // one screen at 60 Hz is 16.6ms
      unsigned long t1 = (micros() - t0) / 1000;  // calculate time in milliseconds
      if(t1 < (half ? 33 : 16))
        vTaskDelay((half ? 33 : 16) - t1);
      else if(half)
        vTaskDelay(1);    // at least 1 ms delay to prevent watchdog timeout

      // physical refresh is 30Hz. So send vblank trigger twice a frame to the emulation. This will make the game run with 60hz speed
      emulation_notifyGive();
    }
  }
}

// render one of 36 strips (8 lines, width supplied by the active machine)
void renderRow(short row, bool isMenu) {
  if(isMenu) {
    menu.render_row(row);
  } 
  else {
    const unsigned short width = currentMachine->machineType() == MCH_SCREGG ? 240 : 224;
    memset(frame_buffer, 0, 2 * width * 8);
    currentMachine->render_row(row);
  }
}

void onVolumeUpDown(bool up, bool down) {
  audio.volumeUpDown(up, down);
}

void onDoAttractReset() {
  menu.attract_resetTimer();
}

void onDoReset() {
  if(!menu.machineIndexIsMenu()) {
    doReset = true;    
  }
}
