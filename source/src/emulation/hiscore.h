#ifndef HISCORE_H
#define HISCORE_H

class machineBase;

// max total bytes of high score data per game (largest so far: bnj, 624)
#define HISCORE_MAX_LEN 640

// check for changed scores every 5 seconds (at 60Hz)
#define HISCORE_POLL_FRAMES 300

// the init pattern must match for this many consecutive frames before the
// saved scores are injected. This protects against transient matches while
// the game is still booting (e.g. RAM self test patterns).
#define HISCORE_STABLE_FRAMES 30

class Hiscore {
public:
  void init(void);                  // open the NVS namespace, call once in setup()
  void start(machineBase *machine); // call right after emulation_start()
  void frame(void);                 // call once per frame while a game is running
  void stop(void);                  // call BEFORE emulation_stop() (it clears the RAM)

private:
  void readAll(unsigned char *buf);
  bool initPatternMatches(void);

  machineBase *machine = 0;
  enum { WAIT_INIT, ACTIVE } state = WAIT_INIT;
  unsigned short total_len = 0;
  unsigned short frame_cnt = 0;
  unsigned short stable_cnt = 0;
  bool have_saved = false;

  // last known high score data (= what is stored in NVS once saved)
  unsigned char saved[HISCORE_MAX_LEN];
};

#endif
