/*
 * Persistent high scores.
 *
 * Per-game descriptors (see hiscore_region_S in machineBase.h) come from
 * MAME's hiscore.dat. Once a game has initialized its high score table
 * (detected via the start/end pattern bytes, stable for a few frames) the
 * scores saved in NVS are injected into the game RAM. While the game runs
 * the regions are polled periodically and written back to NVS whenever a
 * new record shows up. NVS is only written on actual changes, so flash
 * wear is not a concern.
 */
#include <Arduino.h>
#include <Preferences.h>
#include "hiscore.h"
#include "../machines/machineBase.h"

// serial diagnostics for bringing up a new game: while waiting for the init
// pattern, periodically print the actual first/last byte of each region so
// wrong hiscore.dat values (different ROM set) can be spotted and fixed
// without guessing; also hex-dump the regions on load/inject/save
#define HISCORE_DEBUG 0

static Preferences prefs;

#if HISCORE_DEBUG
// hex dump of all regions, prefixed by their CPU address
static void hs_dump(machineBase *machine, const char *tag, const unsigned char *buf) {
  unsigned char count;
  const hiscore_region_S *regions = machine->hiscoreRegions(&count);
  printf("HS %s %s:", machine->hiscoreKey(), tag);
  for (unsigned char i = 0; i < count; i++) {
    printf(" [%04x]", regions[i].addr);
    for (unsigned short j = 0; j < regions[i].len; j++)
      printf(" %02x", *buf++);
  }
  printf("\n");

  // temporary (pooyan/turtles/amidar debugging): the RAM top score restores
  // fine but the game only DRAWS "TOP 10000" once at boot, before the
  // injection — dump the video RAM to locate the drawn top score tiles, so
  // they can be added to the descriptors (same approach as pacman's 0x43ED)
  unsigned short win = 0;
  if (!strcmp(machine->hiscoreKey(), "pooyan"))
    win = 0x8400;
  else if (!strcmp(machine->hiscoreKey(), "turtles") || !strcmp(machine->hiscoreKey(), "amidar"))
    win = 0x9000;
  if (win) {
    printf("HS %s videoram %04x-%04x:", machine->hiscoreKey(), win, win + 0x3ff);
    for (unsigned short a = win; a < win + 0x400; a++) {
      if (!(a & 15)) printf("\nHS  %04x:", a);
      printf(" %02x", machine->hiscoreRead(a));
    }
    printf("\n");
  }
}
#endif

void Hiscore::init(void) {
  prefs.begin("hiscore", false);
}

void Hiscore::start(machineBase *m) {
  machine = 0;

  unsigned char count = 0;
  const hiscore_region_S *regions = m->hiscoreRegions(&count);
  const char *key = m->hiscoreKey();
  if (!regions || !count || !key)
    return;   // game has no high score support

  total_len = 0;
  for (unsigned char i = 0; i < count; i++)
    total_len += regions[i].len;
  if (!total_len || total_len > HISCORE_MAX_LEN)
    return;

  machine = m;
  state = WAIT_INIT;
  frame_cnt = 0;
  stable_cnt = 0;

  have_saved = (prefs.getBytesLength(key) == total_len) &&
               (prefs.getBytes(key, saved, total_len) == total_len);

#if HISCORE_DEBUG
  if (have_saved)
    hs_dump(machine, "loaded from NVS", saved);
#endif
}

// concatenate all regions into buf (total_len bytes)
void Hiscore::readAll(unsigned char *buf) {
  unsigned char count;
  const hiscore_region_S *regions = machine->hiscoreRegions(&count);

  for (unsigned char i = 0; i < count; i++)
    for (unsigned short j = 0; j < regions[i].len; j++)
      *buf++ = machine->hiscoreRead(regions[i].addr + j);
}

bool Hiscore::initPatternMatches(void) {
  unsigned char count;
  const hiscore_region_S *regions = machine->hiscoreRegions(&count);

  for (unsigned char i = 0; i < count; i++)
    if (machine->hiscoreRead(regions[i].addr) != regions[i].start_byte ||
        machine->hiscoreRead(regions[i].addr + regions[i].len - 1) != regions[i].end_byte)
      return false;
  return true;
}

void Hiscore::frame(void) {
  if (!machine)
    return;

  if (state == WAIT_INIT) {
#if HISCORE_DEBUG
    // once every 2 seconds: dump actual vs expected pattern bytes
    if (++frame_cnt >= 120) {
      frame_cnt = 0;
      unsigned char count;
      const hiscore_region_S *regions = machine->hiscoreRegions(&count);
      printf("HS wait %s:", machine->hiscoreKey());
      for (unsigned char i = 0; i < count; i++)
        printf(" [%04x]=%02x(exp %02x) [%04x]=%02x(exp %02x)",
               regions[i].addr, machine->hiscoreRead(regions[i].addr), regions[i].start_byte,
               regions[i].addr + regions[i].len - 1,
               machine->hiscoreRead(regions[i].addr + regions[i].len - 1), regions[i].end_byte);
      printf("\n");
    }
#endif
    if (!initPatternMatches()) {
      stable_cnt = 0;
      return;
    }
    if (++stable_cnt < HISCORE_STABLE_FRAMES)
      return;

#if HISCORE_DEBUG
    printf("HS %s: init pattern stable, %s\n", machine->hiscoreKey(),
           have_saved ? "injecting saved scores" : "keeping game defaults as baseline");
#endif

    // the game has initialized its high score table
    if (have_saved) {
      // inject the saved scores
      unsigned char count;
      const hiscore_region_S *regions = machine->hiscoreRegions(&count);
      unsigned short offset = 0;
      for (unsigned char i = 0; i < count; i++)
        for (unsigned short j = 0; j < regions[i].len; j++)
          machine->hiscoreWrite(regions[i].addr + j, saved[offset++]);
      machine->hiscoreRestored();
    } else {
      // remember the game's default scores for change detection
      readAll(saved);
    }

#if HISCORE_DEBUG
    {
      unsigned char current[HISCORE_MAX_LEN];
      readAll(current);
      hs_dump(machine, "after init", current);
    }
#endif

    frame_cnt = 0;
    state = ACTIVE;
    return;
  }

  // ACTIVE: poll for changed scores
  if (++frame_cnt < HISCORE_POLL_FRAMES)
    return;
  frame_cnt = 0;

  unsigned char current[HISCORE_MAX_LEN];
  readAll(current);
  if (memcmp(current, saved, total_len)) {
    memcpy(saved, current, total_len);
    prefs.putBytes(machine->hiscoreKey(), saved, total_len);
    have_saved = true;
#if HISCORE_DEBUG
    printf("HS %s: new record saved\n", machine->hiscoreKey());
    hs_dump(machine, "saved", saved);
#endif
  }
}

void Hiscore::stop(void) {
  if (machine && state == ACTIVE) {
    // final save on game exit
    unsigned char current[HISCORE_MAX_LEN];
    readAll(current);
    if (memcmp(current, saved, total_len))
      prefs.putBytes(machine->hiscoreKey(), current, total_len);
  }
  machine = 0;
}
