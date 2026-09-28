// ============================================================
//  GameTypes.h  -  Axolotl Adventure by AliceFriend
//  Keep this file in the same folder as AxolotlAdventure.ino
// ============================================================
#pragma once
#include <Arduino.h>

enum State { ST_TITLE, ST_SKINS, ST_READY, ST_PLAY, ST_PAUSE, ST_OVER };

enum ObType : uint8_t { OB_ROCK, OB_ROCK_TOP, OB_WEED, OB_FISH };
struct Obstacle { bool on; ObType type; float x, y; int w, h; float spd, phase; uint16_t col; bool passed; };

enum PkType : uint8_t { PK_BUBBLE, PK_HEART };
struct Pickup { bool on; PkType type; float x, y, phase; };

struct Spark     { bool on; float x, y, vx, vy, life; uint16_t c; };
struct FloatText { bool on; float x, y, life; char s[16]; uint16_t c; };
struct BgBubble  { float x, y, spd; uint8_t r; };

struct Note  { uint16_t f; uint16_t ms; };   // f = 0 means a short silence

struct Skin {
  const char* name;
  uint16_t body, belly, gill, eye;
  int unlockScore;      // best score needed to unlock
  bool rainbow;
};

struct Level { const char* name; float v0, vMax; int gapMin, gapMax; int lives; float fishSpeed; };
