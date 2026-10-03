// ============================================================
//  GameTypes.h  -  Axolotl Adventure by AliceFriend
//  Keep this file in the same folder as AxolotlAdventure.ino
// ============================================================
#pragma once
#include <Arduino.h>

#define MAX_PROFILES 6
#define MAX_SCORES   10
#define NAME_LEN     8

enum State {
  ST_PROFILES,   // "Who's playing?"
  ST_NAME,       // on-screen keyboard for a new player
  ST_CONFIRM,    // "Delete this player?"
  ST_TITLE,      // main menu
  ST_SKINS,      // skin picker
  ST_SCORES,     // leaderboard
  ST_READY,      // 3-2-1 countdown
  ST_PLAY,
  ST_PAUSE,      // pause menu
  ST_CLEAR,      // level complete
  ST_OVER        // out of hearts
};

enum ObType : uint8_t { OB_ROCK, OB_ROCK_TOP, OB_WEED, OB_FISH };
struct Obstacle { bool on; ObType type; float x, y; int w, h; float spd, phase; uint16_t col; bool passed; };

enum PkType : uint8_t { PK_BUBBLE, PK_HEART, PK_WORM, PK_TROPHY };
struct Pickup { bool on; PkType type; float x, y, phase; };

struct Spark     { bool on; float x, y, vx, vy, life; uint16_t c; };
struct FloatText { bool on; float x, y, life; char s[16]; uint16_t c; };
struct BgBubble  { float x, y, spd; uint8_t r; };

struct Note { uint16_t f; uint16_t ms; };   // f = 0 means a short silence

struct Skin {
  const char* name;
  uint16_t body, belly, gill, eye;
  uint8_t rainbow;      // 0 = normal, 1 = bright rainbow, 2 = dark rainbow
};

// Speed setting (the same for every level)
struct Mode { const char* name; float speed; int gapMin, gapMax; int lives; float fishSpeed; };

// Saved player (stored in flash, survives power-off)
struct Profile {
  uint8_t  used;
  char     name[NAME_LEN + 1];
  uint8_t  skin;        // chosen skin
  uint8_t  unlocked;    // how many skins are unlocked (1 = just the first)
  uint16_t level;       // the level this player plays next
  uint32_t best;        // best score in one run
  uint8_t  seen;        // skins the player has already looked at (for "NEW!" badges)
};

// Leaderboard entry
struct ScoreEntry {
  char     name[NAME_LEN + 1];
  uint16_t level;
  uint32_t score;
};
