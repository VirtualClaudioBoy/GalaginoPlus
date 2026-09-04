#ifndef INPUT_H
#define INPUT_H

#include <Arduino.h>
#include "../config.h"
#ifdef NUNCHUCK_INPUT
  #include "nunchuck.h"
#endif

// a total of 7 button is needed for most games
#define BUTTON_LEFT  0x01
#define BUTTON_RIGHT 0x02
#define BUTTON_UP    0x04
#define BUTTON_DOWN  0x08
#define BUTTON_FIRE  0x10
#define BUTTON_START 0x20
#define BUTTON_COIN  0x40
#define BUTTON_EXTRA 0x80


class Input {
public:
  void init(char SingleMachine);
  unsigned char buttons_get(void);
  // stato FISICO del fuoco (pre-autofire), campionato dall'ultima
  // buttons_get(): per i punti dove l'autofire e' dannoso (es. name-entry
  // hiscore, che confermerebbe una lettera per ogni impulso). Ritorna
  // BUTTON_FIRE o 0.
  unsigned char fire_raw(void) { return fire_raw_state; }
  char demoSoundsOff();
#ifdef AUTOFIRE_ENABLED
  void setAutofireDisabled(bool disabled) {
    autofire_disabled = disabled;
    autofire_timer = 0;
  }
#endif

  typedef std::function<void(bool up, bool down)> THandlerVolume;
  Input& onVolumeUpDown(THandlerVolume fn);

  typedef std::function<void(void)> THandlerDoReset;
  Input& onDoReset(THandlerDoReset fn);

  typedef std::function<void(void)> THandlerDoAttractReset;
  Input& onDoAttractReset(THandlerDoAttractReset fn);

private:
  THandlerVolume _volume_callback;
  THandlerDoReset _doReset_callback;
  THandlerDoAttractReset _doAttractReset_callback;
  unsigned char input_states_last;
  int virtual_coin_state;
  unsigned long virtual_coin_timer;
  unsigned long reset_timer;
  char singleMachine;
  char switchDemoSoundsOff;
  char firePressedAtStart;
  unsigned char fire_raw_state = 0;   // vedi fire_raw()
#ifdef AUTOFIRE_ENABLED
  unsigned long autofire_timer;   // istante di inizio della sequenza on/off corrente
  bool autofire_disabled = false;
#endif
#ifdef NUNCHUCK_INPUT
  Nunchuck nunchuck;
#endif
};

#endif
