/*
 * ============================================================
 *              AXOLOTL ADVENTURE  by AliceFriend
 *                       version 1.2
 * ============================================================
 *  For the original "Cheap Yellow Display" (ESP32-2432S028R)
 *  ILI9341 320x240 screen + XPT2046 touch + speaker + RGB LED
 *
 *  Libraries (install from Arduino Library Manager):
 *  (see README.md for full setup steps)
 *    - TFT_eSPI            by Bodmer  (copy TFT_eSPI_Setup/User_Setup.h into the library)
 *    - XPT2046_Touchscreen by Paul Stoffregen
 *  Board: "ESP32 Dev Module"
 *
 *  HOW TO PLAY
 *    Touch the TOP half of the screen    -> swim up
 *    Touch the BOTTOM half of the screen -> swim down
 *    Dodge rocks, seaweed and fish. Eat worms, pop bubbles,
 *    grab golden trophies and reach each level's goal!
 * ============================================================
 */

#include <Arduino.h>
#include <SPI.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include <Preferences.h>
#include <ctype.h>

// ============================================================
//   GAME TYPES
//   (Keep these right after the #includes, above every function.
//    The Arduino IDE adds hidden function declarations before the
//    first function, and those need these types to exist already.)
// ============================================================
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
  ST_SHOP,       // spend coins
  ST_STICKERS,   // achievements
  ST_SETTINGS,   // speed, sound, music, brightness
  ST_REVIVE,     // "keep swimming for 25 coins?"
  ST_READY,      // 3-2-1 countdown
  ST_PLAY,
  ST_PAUSE,      // pause menu
  ST_CLEAR,      // level complete
  ST_OVER        // out of hearts
};

enum ObType : uint8_t { OB_ROCK, OB_ROCK_TOP, OB_WEED, OB_FISH };
struct Obstacle { bool on; ObType type; float x, y; int w, h; float spd, phase; uint16_t col; bool passed; };

enum PkType : uint8_t { PK_BUBBLE, PK_HEART, PK_WORM, PK_TROPHY, PK_COIN };
struct Pickup { bool on; PkType type; float x, y, phase; };

struct Spark     { bool on; float x, y, vx, vy, life; uint16_t c; };
struct FloatText { bool on; float x, y, life; char s[16]; uint16_t c; };
struct BgBubble  { float x, y, spd; uint8_t r; };

struct Note { uint16_t f; uint16_t ms; };   // f = 0 means a short silence

// Special colour effects for skins and hats
enum Fx : uint8_t {
  FX_NONE,      // plain colour
  FX_RAINBOW,   // bright pastel rainbow
  FX_DARKBOW,   // deep, dark rainbow
  FX_SPARKLE,   // twinkling sparkles
  FX_FLASH,     // swaps between two colours (gently, under 2 times a second)
  FX_PULSE,     // glows brighter and darker
  FX_STARS,     // little stars twinkle across it
  FX_SHIFT,     // slowly blends between two colours
  FX_DUSK       // fades blue -> pink -> purple (the "Dusky" skin)
};

struct Skin {
  const char* name;
  uint16_t body, belly, gill, eye;
  uint8_t  fx;
  uint16_t alt;         // second colour for effects
  uint16_t price;       // shop price (0 = not sold in the shop)
  uint8_t  lvl;         // unlocked after finishing this many levels (0 with a price = shop only)
};

enum HatStyle : uint8_t { HAT_BEANIE, HAT_PARTY, HAT_TOP, HAT_CAP, HAT_BOW, HAT_CROWN, HAT_SANTA, HAT_WIZARD };
struct Hat { const char* name; uint8_t style; uint16_t col, alt; uint8_t fx; uint16_t price; };

// Axolotl friends: helpers for a level, or characters you can play as
struct Friend { const char* name; const char* perk; uint16_t c1, c2, c3; uint16_t price; };

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
  // ---- added in v1.3 (kept at the end so older saves still load) ----
  uint32_t coins;
  uint32_t hatsOwned;   // one bit per hat
  uint32_t skinsOwned;  // one bit per shop skin
  uint8_t  hat;         // 0 = no hat, otherwise hat number + 1
  uint8_t  shields;     // shields in your pocket
  uint8_t  friendsOwned;// one bit per friend you can play as
  uint8_t  character;   // 0 = axolotl, otherwise friend number + 1
  uint8_t  helper;      // friend number + 1 joining you next level, 0 = none
  // ---- added in v1.2 ----
  uint32_t wormsEaten;
  uint32_t bubblesPopped;
  uint32_t coinsCollected;   // coins picked up from the water
  uint16_t trophiesFound;
  uint16_t perfectLevels;    // levels finished without a bump
  uint32_t stickers;         // one bit per sticker earned
};

// Stickers (achievements): reach the target to earn the sticker and its coins
enum StickerKind : uint8_t { SK_LEVELS, SK_WORMS, SK_BUBBLES, SK_COINS, SK_TROPHIES, SK_PERFECT, SK_HATS, SK_FRIENDS, SK_SCORE };
struct Sticker { const char* name; const char* desc; uint8_t kind; uint32_t target; uint16_t reward; uint16_t color; };

// ---------------- Full-colour drawing ----------------
// A full-screen 65,000-colour buffer doesn't fit in the ESP32's memory, so the
// game draws the top half, sends it to the screen, then draws the bottom half.
// This wrapper shifts everything up for the bottom half automatically.
class Canvas {
 public:
  TFT_eSprite spr;
  int16_t yo = 0;   // which part of the screen is being drawn
  explicit Canvas(TFT_eSPI* t) : spr(t) {}
  void fillRect(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t c) { spr.fillRect(x, y - yo, w, h, c); }
  void fillRoundRect(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint32_t c) { spr.fillRoundRect(x, y - yo, w, h, r, c); }
  void drawRoundRect(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint32_t c) { spr.drawRoundRect(x, y - yo, w, h, r, c); }
  void fillCircle(int32_t x, int32_t y, int32_t r, uint32_t c) { spr.fillCircle(x, y - yo, r, c); }
  void drawCircle(int32_t x, int32_t y, int32_t r, uint32_t c) { spr.drawCircle(x, y - yo, r, c); }
  void fillEllipse(int32_t x, int32_t y, int32_t rx, int32_t ry, uint32_t c) { spr.fillEllipse(x, y - yo, rx, ry, c); }
  void fillTriangle(int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t x2, int32_t y2, uint32_t c) {
    spr.fillTriangle(x0, y0 - yo, x1, y1 - yo, x2, y2 - yo, c);
  }
  void drawLine(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint32_t c) { spr.drawLine(x0, y0 - yo, x1, y1 - yo, c); }
  void drawFastVLine(int32_t x, int32_t y, int32_t h, uint32_t c) { spr.drawFastVLine(x, y - yo, h, c); }
  void drawFastHLine(int32_t x, int32_t y, int32_t w, uint32_t c) { spr.drawFastHLine(x, y - yo, w, c); }
  void drawPixel(int32_t x, int32_t y, uint32_t c) { spr.drawPixel(x, y - yo, c); }
  void setTextDatum(uint8_t d) { spr.setTextDatum(d); }
  void setTextColor(uint16_t c) { spr.setTextColor(c); }
  void drawString(const char* str, int32_t x, int32_t y, uint8_t f) { spr.drawString(str, x, y - yo, f); }
  int16_t textWidth(const char* str, uint8_t f) { return spr.textWidth(str, f); }
};

// Leaderboard entry
struct ScoreEntry {
  char     name[NAME_LEN + 1];
  uint16_t level;
  uint32_t score;
};

#define GAME_VERSION "1.2"   // shown next to the title (see CHANGELOG.md)

// ---------------- Hardware pins (original CYD) ----------------
#define XPT2046_IRQ   36
#define XPT2046_MOSI  32
#define XPT2046_MISO  39
#define XPT2046_CLK   25
#define XPT2046_CS    33
#define SPEAKER_PIN   26
#define LED_R_PIN     4
#define LED_G_PIN     16
#define LED_B_PIN     17
#define BACKLIGHT_PIN 21

// Screen dims after this long without a touch on a menu (saves battery)
#define DIM_AFTER_MS  60000
#define BRIGHT_DIM    20
const uint8_t BRIGHT_LEVELS[3] = {90, 170, 255};   // Low, Medium, High
const char* BRIGHT_NAMES[3] = {"Low", "Medium", "High"};

// 1 = full colour (65,000 colours, smooth pastels). 0 = 256 colours, a little faster.
#define FULL_COLOUR   1

// ---------------- Touch calibration ----------------
// If touches feel "off", set TOUCH_DEBUG to 1. A red dot shows where the
// CYD thinks you touched, plus the raw numbers. Adjust the MIN/MAX values.
#define TOUCH_X_MIN   200
#define TOUCH_X_MAX   3700
#define TOUCH_Y_MIN   240
#define TOUCH_Y_MAX   3800
#define TOUCH_FLIP_X  0     // set to 1 if left/right are reversed
#define TOUCH_FLIP_Y  0     // set to 1 if up/down are reversed
#define TOUCH_DEBUG   0

// ---------------- Game tuning ----------------
static const int   W        = 320;
static const int   H        = 240;
static const int   SAND_Y   = 214;   // where the sea floor starts
static const int   PLAYER_X = 72;    // axolotl stays at this x
static const float IMPULSE  = 110;   // instant push when you tap
static const float SWIM_ACC = 420;   // push while holding
static const float MAX_VY   = 165;   // max vertical speed

// Points
static const int PASS_POINTS   = 1;   // swimming past an obstacle
static const int BUBBLE_POINTS = 2;
static const int WORM_POINTS   = 5;
static const int TROPHY_POINTS = 20;

// Coins and the shop
static const int SHIELD_PRICE = 10;
static const int HELPER_PRICE = 15;
static const int MAX_SHIELDS  = 5;
int levelCoinBonus(int lvl) { return 10 + 5 * lvl; }   // coins for finishing a level
static const int PERFECT_BONUS = 10;   // extra coins for a level with no bumps
static const int REVIVE_PRICE  = 25;   // coins to keep swimming when out of hearts (once per level)

// Characters (0 = axolotl, then the friends)
#define CH_AXOLOTL  0
#define CH_SEAHORSE 1
#define CH_SHRIMP   2
#define CH_TURTLE   3
#define CH_PUFFER   4

#if FULL_COLOUR
static const int BANDS = 2;   // draw the screen in two halves
#else
static const int BANDS = 1;
#endif
static const int BAND_H = H / BANDS;

// A golden trophy replaces a worm once every 50-200 worms
static const int TROPHY_MIN_WORMS = 50;
static const int TROPHY_MAX_WORMS = 200;

// Points needed to finish each level: 50, 75, 100, 150, 200, 300, 400, ...
int levelGoal(int lvl) {
  if (lvl <= 1) return 50;
  if (lvl == 2) return 75;
  if (lvl == 3) return 100;
  if (lvl == 4) return 150;
  return 200 + (lvl - 5) * 100;
}

SPIClass            touchSPI(VSPI);
XPT2046_Touchscreen touch(XPT2046_CS, XPT2046_IRQ);
TFT_eSPI            tft;
Canvas              fb(&tft);        // frame buffer = no flicker
Preferences         prefs;

// ---------------- Colors ----------------
constexpr uint16_t RGB(uint8_t r, uint8_t g, uint8_t b) {
  return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}
const uint16_t C_WHITE       = RGB(255, 255, 255);
const uint16_t C_BLACK       = 0;
const uint16_t C_PEBBLE      = RGB(165, 135, 95);
const uint16_t C_ROCK        = RGB(130, 130, 145);
const uint16_t C_ROCK_LIGHT  = RGB(190, 190, 205);
const uint16_t C_ROCK_DARK   = RGB(80, 80, 100);
const uint16_t C_MOSS        = RGB(90, 185, 90);
const uint16_t C_WEED        = RGB(40, 195, 80);
const uint16_t C_WEED_DARK   = RGB(20, 140, 60);
const uint16_t C_BUBBLE      = RGB(200, 240, 255);
const uint16_t C_HEART       = RGB(255, 60, 90);
const uint16_t C_GOLD        = RGB(255, 215, 40);
const uint16_t C_WORM        = RGB(240, 130, 125);
const uint16_t C_WORM_DARK   = RGB(195, 85, 95);
const uint16_t C_TROPHY      = RGB(255, 200, 30);
const uint16_t C_TROPHY_DARK = RGB(205, 140, 0);
const uint16_t C_PANEL       = RGB(20, 40, 95);
const uint16_t C_BTN_SHADOW  = RGB(0, 35, 70);
const uint16_t C_GREEN_BTN   = RGB(40, 170, 80);
const uint16_t C_BLUE_BTN    = RGB(40, 110, 215);
const uint16_t C_PURPLE_BTN  = RGB(150, 80, 205);
const uint16_t C_ORANGE_BTN  = RGB(240, 130, 30);
const uint16_t C_TEAL_BTN    = RGB(20, 150, 160);
const uint16_t C_RED_BTN     = RGB(210, 60, 60);
const uint16_t C_GREY_BTN    = RGB(110, 110, 125);
// Each level gets a new underwater place (looks only - the speed never changes)
enum ThemeKind : uint8_t {
  TK_DEEP,      // water all the way to the top
  TK_SHALLOW,   // low water: sky above the 2nd colour line, and you can't swim up there
  TK_SUNSET,    // a sun shining down, wobbly through the water
  TK_KELP       // tall kelp swaying in the background
};
struct Theme {
  const char* name;
  uint16_t water[6];
  uint16_t hill, sand, sandDark;
  uint8_t  kind;
  uint16_t skyTop, skyBottom;
};
const Theme THEMES[] = {
  {"Sunny Lagoon", {RGB(120, 215, 240), RGB(90, 195, 230), RGB(60, 170, 220), RGB(40, 145, 205), RGB(30, 120, 185), RGB(20, 95, 165)},
   RGB(20, 90, 125), RGB(240, 210, 140), RGB(200, 165, 100), TK_SHALLOW, RGB(130, 200, 255), RGB(215, 240, 255)},
  {"Coral Reef",   {RGB(110, 225, 215), RGB(80, 205, 200), RGB(55, 180, 190), RGB(40, 155, 175), RGB(30, 130, 160), RGB(20, 105, 145)},
   RGB(230, 110, 130), RGB(250, 225, 170), RGB(215, 180, 120), TK_SHALLOW, RGB(110, 190, 255), RGB(200, 235, 255)},
  {"Sunset Cove",  {RGB(255, 190, 140), RGB(240, 160, 150), RGB(200, 130, 170), RGB(150, 105, 180), RGB(100, 85, 170), RGB(60, 60, 150)},
   RGB(70, 50, 120), RGB(235, 195, 150), RGB(195, 150, 110), TK_SUNSET, 0, 0},
  {"Kelp Forest",  {RGB(110, 200, 170), RGB(85, 180, 150), RGB(65, 160, 135), RGB(50, 140, 120), RGB(38, 118, 102), RGB(28, 95, 85)},
   RGB(30, 90, 60), RGB(200, 190, 140), RGB(160, 150, 105), TK_KELP, 0, 0},
  {"Deep Sea",     {RGB(40, 90, 160), RGB(30, 75, 140), RGB(25, 60, 120), RGB(20, 48, 100), RGB(15, 38, 85), RGB(10, 28, 70)},
   RGB(20, 40, 75), RGB(150, 140, 120), RGB(115, 105, 90), TK_DEEP, 0, 0},
};
const int N_THEMES = sizeof(THEMES) / sizeof(THEMES[0]);
const uint16_t FISH_COLORS[3] = { RGB(255, 140, 40), RGB(255, 220, 50), RGB(190, 110, 235) };

// ---------------- Skins ----------------
// Level skins: the first is free, finishing level N unlocks skin number N+1.
// Shop skins come after them and are bought with coins.
//  name        body               belly              gills              eyes               effect      2nd colour         price
const Skin SKINS[] = {
  {"Pinky",    RGB(255, 160, 190), RGB(255, 215, 230), RGB(235, 70, 130),  C_BLACK,           FX_NONE,    0, 0, 0},  // free
  {"Goldie",   RGB(255, 205, 70),  RGB(255, 240, 170), RGB(255, 120, 30),  C_BLACK,           FX_NONE,    0, 0, 1},  // level 1
  {"Wild",     RGB(105, 120, 70),  RGB(160, 175, 110), RGB(75, 60, 35),    RGB(230, 190, 40), FX_NONE,    0, 0, 2},  // level 2
  {"Sky",      RGB(90, 150, 255),  RGB(175, 205, 255), RGB(40, 70, 200),   C_BLACK,           FX_NONE,    0, 0, 3},  // level 3
  {"Minty",    RGB(110, 225, 160), RGB(200, 255, 220), RGB(30, 150, 90),   C_BLACK,           FX_NONE,    0, 0, 4},  // level 4
  {"Magenta",  RGB(235, 60, 190),  RGB(255, 160, 225), RGB(160, 20, 130),  C_BLACK,           FX_NONE,    0, 0, 5},  // level 5
  {"Cyan",     RGB(60, 220, 235),  RGB(180, 245, 250), RGB(0, 140, 170),   C_BLACK,           FX_NONE,    0, 0, 6},  // level 6
  {"Lavender", RGB(190, 160, 240), RGB(225, 210, 255), RGB(140, 90, 210),  C_BLACK,           FX_NONE,    0, 0, 7},  // level 7
  {"White",    RGB(245, 245, 250), RGB(220, 225, 240), RGB(255, 140, 170), C_BLACK,           FX_NONE,    0, 0, 8},  // level 8
  {"Midnight", RGB(90, 60, 165),   RGB(150, 120, 220), RGB(255, 120, 220), C_WHITE,           FX_NONE,    0, 0, 9},  // level 9
  {"Rainbow",  0, 0, 0,                                                     C_BLACK,           FX_RAINBOW, 0, 0, 10},  // level 10
  {"Ruby",     RGB(230, 50, 50),   RGB(255, 140, 130), RGB(150, 15, 30),   C_BLACK,           FX_NONE,    0, 0, 11},  // level 11 (red)
  {"Lemon",    RGB(255, 240, 60),  RGB(255, 250, 180), RGB(240, 175, 0),   C_BLACK,           FX_NONE,    0, 0, 12},  // level 12 (yellow)
  {"Cloud",    RGB(200, 200, 210), RGB(235, 235, 242), RGB(140, 145, 170), C_BLACK,           FX_NONE,    0, 0, 13},  // level 13 (light gray)
  {"Shadow",   RGB(25, 25, 32),    RGB(55, 55, 68),    RGB(95, 65, 120),   RGB(235, 195, 40), FX_NONE,    0, 0, 14},  // level 14 (deep black)
  {"Forest",   RGB(30, 100, 50),   RGB(70, 145, 85),   RGB(15, 60, 30),    RGB(235, 195, 40), FX_NONE,    0, 0, 15},  // level 15 (dark green)
  {"Twilight", 0, 0, 0,                                                     C_WHITE,           FX_DARKBOW, 0, 0, 16},  // level 16 (dark rainbow)
  {"Tangerine",RGB(255, 120, 0),   RGB(255, 195, 120), RGB(200, 70, 0),    C_BLACK,           FX_NONE,    0, 0, 17},  // level 17 (bright orange)
  {"Slate",    RGB(85, 88, 95),    RGB(125, 128, 135), RGB(55, 58, 65),    RGB(235, 195, 40), FX_NONE,    0, 0, 18},  // level 18 (dark gray)
  // ---- shop skins ----
  {"Glitter",  RGB(255, 170, 215), RGB(255, 225, 240), RGB(240, 90, 170),  C_BLACK, FX_SPARKLE, C_WHITE, 60, 0},
  {"Frosty",   RGB(190, 235, 255), RGB(235, 250, 255), RGB(120, 190, 240), C_BLACK, FX_SPARKLE, C_WHITE, 60, 0},
  {"Cotton Candy", RGB(255, 185, 215), RGB(255, 235, 245), RGB(170, 210, 255), C_BLACK, FX_SHIFT, RGB(175, 215, 255), 60, 0},
  {"Glow",     RGB(90, 255, 120),  RGB(200, 255, 210), RGB(20, 170, 60),   C_BLACK, FX_PULSE,   RGB(20, 140, 60), 70, 0},
  {"Disco",    RGB(255, 80, 200),  RGB(255, 200, 240), RGB(120, 60, 255),  C_BLACK, FX_FLASH,   RGB(60, 220, 255), 70, 0},
  {"Lava",     RGB(255, 90, 0),    RGB(255, 190, 90),  RGB(180, 20, 0),    C_BLACK, FX_PULSE,   RGB(220, 20, 20), 80, 0},
  {"Galaxy",   RGB(45, 25, 95),    RGB(80, 55, 140),   RGB(255, 120, 230), C_WHITE, FX_STARS,   RGB(255, 255, 200), 90, 0},
  {"Golden",   RGB(255, 200, 40),  RGB(255, 235, 140), RGB(230, 140, 0),   C_BLACK, FX_SPARKLE, RGB(255, 255, 220), 100, 0},
  // ---- pastel collection: unlock by levels OR buy in the shop (kept at the end so saves stay correct) ----
  {"Blossom",   RGB(255, 200, 215), RGB(255, 235, 240), RGB(240, 150, 180), C_BLACK, FX_NONE, 0, 75, 19},  // pastel pink
  {"Peach",     RGB(255, 205, 170), RGB(255, 235, 215), RGB(240, 150, 120), C_BLACK, FX_NONE, 0, 80, 20},  // pastel peach
  {"Butter",    RGB(255, 240, 170), RGB(255, 250, 222), RGB(235, 200, 110), C_BLACK, FX_NONE, 0, 85, 21},  // pastel yellow
  {"Pistachio", RGB(200, 235, 180), RGB(230, 250, 215), RGB(140, 195, 125), C_BLACK, FX_NONE, 0, 90, 22},  // pastel green
  {"Seafoam",   RGB(175, 235, 220), RGB(220, 250, 242), RGB(110, 195, 180), C_BLACK, FX_NONE, 0, 95, 23},  // pastel aqua
  {"Baby Blue", RGB(180, 210, 255), RGB(225, 238, 255), RGB(120, 160, 230), C_BLACK, FX_NONE, 0, 100, 24},  // pastel blue
  {"Lilac",     RGB(215, 190, 245), RGB(238, 225, 252), RGB(165, 130, 215), C_BLACK, FX_NONE, 0, 110, 25},  // pastel purple
  {"Dusky",     RGB(95, 125, 210),  RGB(220, 210, 240), RGB(130, 90, 190),  C_BLACK, FX_DUSK, 0, 150, 26},  // fades blue, pink, purple
};
const int N_SKINS = sizeof(SKINS) / sizeof(SKINS[0]);
const int FIRST_SHOP_SKIN = 19;                 // skins from here on can have a shop price
// (each one's "bought" flag is bit number (skin - FIRST_SHOP_SKIN) in skinsOwned)

bool isLevelSkin(int i) { return SKINS[i].lvl > 0 || SKINS[i].price == 0; }   // unlocks by finishing levels
bool isShopOnly(int i)  { return SKINS[i].lvl == 0 && SKINS[i].price > 0; }

// The Shop's Skins tab: shop-only skins first, then level skins you can buy early
int SHOP_SKIN_LIST[N_SKINS];
int N_SHOP_SKINS = 0;
const int MAX_SKIN_LEVEL = 26;                  // the last level that unlocks a skin
int SKIN_ORDER[N_SKINS];                        // skin screen order: by unlock level, then shop skins

void buildSkinOrder() {
  int n = 0;
  for (int l = 0; l <= MAX_SKIN_LEVEL; l++)
    for (int i = 0; i < N_SKINS; i++)
      if (isLevelSkin(i) && SKINS[i].lvl == l) SKIN_ORDER[n++] = i;
  for (int i = 0; i < N_SKINS; i++)
    if (isShopOnly(i)) SKIN_ORDER[n++] = i;
  N_SHOP_SKINS = 0;
  for (int i = 0; i < N_SKINS; i++) if (isShopOnly(i)) SHOP_SKIN_LIST[N_SHOP_SKINS++] = i;
  for (int i = 0; i < N_SKINS; i++) if (SKINS[i].price > 0 && !isShopOnly(i)) SHOP_SKIN_LIST[N_SHOP_SKINS++] = i;
}

// ---------------- Hats (bought in the shop) ----------------
//  name                 style        colour             2nd colour          effect      price
const Hat HATS[] = {
  {"Red Beanie",        HAT_BEANIE, RGB(225, 40, 50),   C_WHITE,            FX_NONE,    10},
  {"Orange Beanie",     HAT_BEANIE, RGB(255, 120, 0),   C_WHITE,            FX_NONE,    10},
  {"Yellow Beanie",     HAT_BEANIE, RGB(255, 215, 0),   C_WHITE,            FX_NONE,    10},
  {"Green Beanie",      HAT_BEANIE, RGB(40, 180, 70),   C_WHITE,            FX_NONE,    10},
  {"Blue Beanie",       HAT_BEANIE, RGB(40, 100, 230),  C_WHITE,            FX_NONE,    10},
  {"Purple Beanie",     HAT_BEANIE, RGB(140, 60, 210),  C_WHITE,            FX_NONE,    10},
  {"Pastel Beanie",     HAT_BEANIE, RGB(255, 190, 220), RGB(180, 220, 255), FX_SHIFT,   15},
  {"Pink Bow",          HAT_BOW,    RGB(255, 105, 180), RGB(255, 190, 220), FX_NONE,    12},
  {"Ball Cap",          HAT_CAP,    RGB(40, 110, 230),  C_WHITE,            FX_NONE,    15},
  {"Orange Cap",        HAT_CAP,    RGB(255, 120, 0),   C_WHITE,            FX_NONE,    15},
  {"Party Hat",         HAT_PARTY,  RGB(255, 100, 180), RGB(255, 230, 80),  FX_NONE,    15},
  {"Sparkly Party Hat", HAT_PARTY,  RGB(120, 200, 255), C_WHITE,            FX_SPARKLE, 25},
  {"Flashy Party Hat",  HAT_PARTY,  RGB(255, 60, 60),   RGB(60, 200, 255),  FX_FLASH,   25},
  {"Rainbow Party Hat", HAT_PARTY,  0,                  C_WHITE,            FX_RAINBOW, 30},
  {"Top Hat",           HAT_TOP,    RGB(30, 30, 35),    RGB(220, 40, 60),   FX_NONE,    20},
  {"Santa Hat",         HAT_SANTA,  RGB(220, 30, 40),   C_WHITE,            FX_NONE,    25},
  {"Wizard Hat",        HAT_WIZARD, RGB(110, 60, 200),  C_GOLD,             FX_SPARKLE, 35},
  {"Gold Crown",        HAT_CROWN,  RGB(255, 205, 30),  RGB(230, 40, 80),   FX_SPARKLE, 40},
};
const int N_HATS = sizeof(HATS) / sizeof(HATS[0]);

// ---------------- Axolotl friends ----------------
//  name        perk when you play as them          main colour        light colour       dark colour        price to unlock
const Friend FRIENDS[] = {
  {"Seahorse", "Pulls treats & coins to you",      RGB(255, 170, 60),  RGB(255, 225, 150), RGB(225, 105, 30), 120},
  {"Shrimp",   "Tiny! Easier to dodge",            RGB(255, 130, 110), RGB(255, 195, 175), RGB(215, 75, 65),  100},
  {"Turtle",   "Gets 1 extra heart",               RGB(60, 160, 90),   RGB(160, 215, 120), RGB(30, 105, 55),  150},
  {"Puffer",   "Shields last 8 seconds",           RGB(255, 215, 80),  RGB(255, 245, 200), RGB(210, 150, 30), 130},
};
const int N_FRIENDS = sizeof(FRIENDS) / sizeof(FRIENDS[0]);

// ---------------- Stickers ----------------
//  name              how to get it                  kind         target  coins  colour
const Sticker STICKERS[] = {
  {"First Swim",     "Finish level 1",               SK_LEVELS,   1,      5,     RGB(80, 200, 255)},
  {"Worm Muncher",   "Eat 50 worms",                 SK_WORMS,    50,     10,    RGB(240, 130, 125)},
  {"Worm Feast",     "Eat 500 worms",                SK_WORMS,    500,    30,    RGB(220, 80, 90)},
  {"Bubble Popper",  "Pop 100 bubbles",              SK_BUBBLES,  100,    10,    RGB(150, 220, 255)},
  {"Coin Collector", "Pick up 250 coins",            SK_COINS,    250,    20,    RGB(255, 215, 40)},
  {"Trophy Hunter",  "Find a golden trophy",         SK_TROPHIES, 1,      20,    RGB(255, 190, 0)},
  {"Flawless",       "Finish a level with no bumps", SK_PERFECT,  1,      15,    RGB(120, 230, 140)},
  {"Perfect Five",   "5 levels with no bumps",       SK_PERFECT,  5,      30,    RGB(40, 180, 90)},
  {"Explorer",       "Finish level 5",               SK_LEVELS,   5,      20,    RGB(60, 170, 220)},
  {"Deep Diver",     "Finish level 10",              SK_LEVELS,   10,     40,    RGB(40, 90, 200)},
  {"Fashionista",    "Own 5 hats",                   SK_HATS,     5,      20,    RGB(255, 105, 180)},
  {"Best Friends",   "Unlock a friend to play as",   SK_FRIENDS,  1,      20,    RGB(255, 170, 60)},
  {"Super Swimmer",  "Score 500 in one game",        SK_SCORE,    500,    30,    RGB(190, 110, 235)},
  {"Legend",         "Finish level 26",              SK_LEVELS,   26,     100,   RGB(255, 80, 200)},
};
const int N_STICKERS = sizeof(STICKERS) / sizeof(STICKERS[0]);

// ---------------- Speed modes (same speed on every level) ----------------
const Mode MODES[3] = {
  {"Easy",    65, 170, 240, 5, 0.5f},
  {"Normal",  90, 145, 205, 3, 1.0f},
  {"Zoom!",  118, 125, 180, 3, 1.3f},
};
const char* VOL_NAMES[3] = {"Off", "Quiet", "Loud"};

// ============================================================
//   SOUND ENGINE  (runs in its own task so timing stays exact)
// ============================================================
const Note SND_CLICK[]     = {{1800, 25}};
const Note SND_KEY[]       = {{1400, 20}};
const Note SND_SELECT[]    = {{988, 60}, {1319, 120}};
const Note SND_UP[]        = {{700, 30}, {1050, 30}};
const Note SND_DOWN[]      = {{700, 30}, {480, 30}};
const Note SND_PASS[]      = {{2400, 12}};
const Note SND_BUBBLE[]    = {{1320, 40}, {1760, 70}};
const Note SND_WORM[]      = {{330, 35}, {0, 25}, {440, 35}, {0, 25}, {330, 50}};
const Note SND_TROPHY[]    = {{784, 80}, {988, 80}, {1175, 80}, {1568, 120}, {1175, 80}, {1568, 260}};
const Note SND_HEART[]     = {{880, 60}, {1109, 60}, {1319, 60}, {1760, 140}};
const Note SND_HIT[]       = {{330, 70}, {247, 70}, {165, 140}};
const Note SND_BEEP[]      = {{880, 120}};
const Note SND_GO[]        = {{1319, 260}};
const Note SND_MILESTONE[] = {{1047, 70}, {1319, 70}, {1568, 70}, {2093, 160}};
const Note SND_CLEAR[]     = {{523, 120}, {659, 120}, {784, 120}, {1047, 160}, {0, 60}, {988, 100}, {1047, 400}};
const Note SND_OVER[]      = {{784, 160}, {659, 160}, {523, 160}, {392, 360}};
const Note SND_RECORD[]    = {{523, 100}, {659, 100}, {784, 100}, {1047, 180}, {0, 60}, {784, 100}, {1047, 360}};
const Note SND_START[]     = {{523, 90}, {659, 90}, {784, 90}, {1047, 200}};
const Note SND_LOCKED[]    = {{220, 90}, {0, 40}, {180, 140}};
const Note SND_DELETE[]    = {{400, 60}, {300, 90}};
const Note SND_COIN[]      = {{1976, 40}, {2637, 80}};
const Note SND_BUY[]       = {{1047, 60}, {1319, 60}, {1568, 60}, {2093, 140}};
const Note SND_SHIELD[]    = {{600, 60}, {900, 60}, {1200, 140}};
const Note SND_SQUEAK[]    = {{1500, 40}, {2000, 40}, {1700, 60}};
#define SND(x) x, (uint8_t)(sizeof(x) / sizeof(x[0]))

#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
  #define SPK_CH SPEAKER_PIN      // core 3.x uses the pin number
#else
  #define SPK_CH 0                // core 2.x uses a channel number
#endif

#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
  #define BL_CH BACKLIGHT_PIN
#else
  #define BL_CH 1
#endif

volatile uint8_t  volume = 2;             // 0 off, 1 quiet, 2 loud
portMUX_TYPE      sndMux = portMUX_INITIALIZER_UNLOCKED;
const Note*       sndReqSeq = nullptr;
uint8_t           sndReqLen = 0;
volatile bool     sndBusy = false;
volatile uint8_t  sndBusyPrio = 0;

void speakerInit() {
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcAttach(SPEAKER_PIN, 2000, 8);
#else
  ledcSetup(SPK_CH, 2000, 8);
  ledcAttachPin(SPEAKER_PIN, SPK_CH);
#endif
  ledcWrite(SPK_CH, 0);
}

void backlightInit() {
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcAttach(BACKLIGHT_PIN, 5000, 8);
#else
  ledcSetup(BL_CH, 5000, 8);
  ledcAttachPin(BACKLIGHT_PIN, BL_CH);
#endif
  ledcWrite(BL_CH, 255);
}

volatile bool musicWanted = false;   // set by the game: music on AND playing a level
volatile bool musicOn = true;        // the Music setting

// Background tune (an original little melody). It loops, pauses for sound
// effects, then carries on. {frequency in Hz, length in ms}, 0 = rest.
const Note MUSIC[] = {
  {523, 150}, {659, 150}, {784, 150}, {659, 150}, {698, 150}, {880, 150}, {784, 300},
  {659, 150}, {784, 150}, {1047, 150}, {784, 150}, {698, 150}, {659, 150}, {587, 300},
  {523, 150}, {587, 150}, {659, 150}, {523, 150}, {587, 150}, {659, 150}, {698, 150}, {587, 150},
  {659, 150}, {587, 150}, {523, 150}, {494, 150}, {523, 300}, {0, 300},
  {392, 150}, {523, 150}, {659, 150}, {523, 150}, {440, 150}, {587, 150}, {698, 300},
  {659, 150}, {587, 150}, {523, 150}, {587, 150}, {659, 150}, {587, 150}, {523, 300}, {0, 300},
};
const int MUSIC_LEN = sizeof(MUSIC) / sizeof(MUSIC[0]);

void speakerTone(uint16_t f, bool music = false) {
  if (f == 0 || volume == 0) { ledcWrite(SPK_CH, 0); return; }
  ledcChangeFrequency(SPK_CH, f, 8);
  uint8_t duty = volume == 1 ? 10 : 128;     // smaller duty = quieter
  if (music) duty = volume == 1 ? 4 : 30;    // music sits underneath the sound effects
  ledcWrite(SPK_CH, duty);
}

// prio: higher numbers can interrupt lower ones
void playSound(const Note* seq, uint8_t len, uint8_t prio = 1) {
  if (sndBusy && prio < sndBusyPrio) return;
  portENTER_CRITICAL(&sndMux);
  sndReqSeq = seq;
  sndReqLen = len;
  sndBusyPrio = prio;
  sndBusy = true;
  portEXIT_CRITICAL(&sndMux);
}

void soundTask(void*) {
  const Note* seq = nullptr;
  uint8_t len = 0, pos = 0;
  uint32_t noteEnd = 0;
  int mPos = 0;
  uint32_t mEnd = 0;
  bool mPlaying = false;
  for (;;) {
    const Note* req = nullptr;
    uint8_t reqLen = 0;
    portENTER_CRITICAL(&sndMux);
    if (sndReqSeq) { req = sndReqSeq; reqLen = sndReqLen; sndReqSeq = nullptr; }
    portEXIT_CRITICAL(&sndMux);

    uint32_t now = millis();
    if (req) {                                   // a new sound effect
      seq = req; len = reqLen; pos = 0;
      speakerTone(seq[0].f);
      noteEnd = now + seq[0].ms;
    } else if (seq) {                            // a sound effect is playing
      if ((int32_t)(now - noteEnd) >= 0) {
        pos++;
        if (pos >= len) {
          seq = nullptr;
          speakerTone(0);
          sndBusy = false;
          sndBusyPrio = 0;
          mEnd = now;                            // let the music carry on
        } else {
          speakerTone(seq[pos].f);
          noteEnd = now + seq[pos].ms;
        }
      }
    } else if (musicWanted && musicOn) {         // background music
      if (!mPlaying || (int32_t)(now - mEnd) >= 0) {
        if (mPlaying) mPos = (mPos + 1) % MUSIC_LEN;
        mPlaying = true;
        speakerTone(MUSIC[mPos].f, true);
        mEnd = now + MUSIC[mPos].ms;
      }
    } else if (mPlaying) {
      mPlaying = false;
      speakerTone(0);
    }
    vTaskDelay(1);
  }
}

// ============================================================
//   RGB LED  (active LOW on the CYD)
// ============================================================
uint32_t ledOffAt = 0;
void ledSet(bool r, bool g, bool b) {
  digitalWrite(LED_R_PIN, r ? LOW : HIGH);
  digitalWrite(LED_G_PIN, g ? LOW : HIGH);
  digitalWrite(LED_B_PIN, b ? LOW : HIGH);
}
void ledFlash(bool r, bool g, bool b, uint16_t ms) {
  ledSet(r, g, b);
  ledOffAt = millis() + ms;
  if (ledOffAt == 0) ledOffAt = 1;
}
void ledUpdate(uint32_t now) {
  if (ledOffAt && (int32_t)(now - ledOffAt) >= 0) { ledSet(0, 0, 0); ledOffAt = 0; }
}

// ============================================================
//   TOUCH
// ============================================================
bool tDown = false, tPressed = false, prevDown = false;
int tx = 0, ty = 0, rawX = 0, rawY = 0;
uint32_t lastTouchMs = 0, inputLockUntil = 0;

void lockInput(uint16_t ms) { inputLockUntil = millis() + ms; }

void readTouch(uint32_t now) {
  if (touch.touched()) {
    TS_Point p = touch.getPoint();
    rawX = p.x; rawY = p.y;
    int x = map(p.x, TOUCH_X_MIN, TOUCH_X_MAX, 0, W - 1);
    int y = map(p.y, TOUCH_Y_MIN, TOUCH_Y_MAX, 0, H - 1);
    if (TOUCH_FLIP_X) x = W - 1 - x;
    if (TOUCH_FLIP_Y) y = H - 1 - y;
    tx = constrain(x, 0, W - 1);
    ty = constrain(y, 0, H - 1);
    lastTouchMs = now;
  }
  // small grace period smooths out resistive-touch dropouts
  bool down = lastTouchMs != 0 && (now - lastTouchMs) < 70;
  tPressed = down && !prevDown && (int32_t)(now - inputLockUntil) >= 0;
  prevDown = down;
  tDown = down;
}

bool inRect(int px, int py, int x, int y, int w, int h) {
  return px >= x && px < x + w && py >= y && py < y + h;
}
bool rectsOverlap(int ax, int ay, int aw, int ah, int bx, int by, int bw, int bh) {
  return ax < bx + bw && ax + aw > bx && ay < by + bh && ay + ah > by;
}
float frand(float a, float b) { return a + (b - a) * (random(10001) / 10000.0f); }
bool tapped(int x, int y, int w, int h) { return tPressed && inRect(tx, ty, x, y, w, h); }

// ============================================================
//   GAME STATE
// ============================================================
State state = ST_TITLE;
State skinReturn = ST_TITLE;   // where the skin screen goes back to

const int MAX_OBS = 8, MAX_PK = 14, MAX_SPARK = 32, MAX_TXT = 6, MAX_BG = 12;
Obstacle  obs[MAX_OBS];
Pickup    pks[MAX_PK];
Spark     sparks[MAX_SPARK];
FloatText ftxt[MAX_TXT];
BgBubble  bgb[MAX_BG];

Profile    profiles[MAX_PROFILES];
ScoreEntry scores[MAX_SCORES];
int cur = -1;                  // current player

float py = 110, vy = 0, invuln = 0, gameT = 0, scrollX = 0, spawnDist = 0, speed = 0;
float readyT = 0, overT = 0, wiggleT = 0, shieldT = 0, shopMsgT = 0;
int   shopTab = 0, shopIdx[4] = {0, 0, 0, 0}, buyArmed = 0;
const char* shopMsg = nullptr;
State shopReturn = ST_TITLE;
int   levelHelper = -1, coinBonus = 0;   // friend helping this level (-1 = none)
int   levelScore = 0, runScore = 0, curLevel = 1, lives = 3;
int   mode = 0, browse = 0, readyCount = -1, wormsUntilTrophy = 100;
int   overRank = -1, confirmIdx = -1;
bool  runActive = false, newRecord = false, deleteMode = false, quitArmed = false;
const char* overMsg = "";
const char* newSkinName = nullptr;
int   newSkinIdx = -1;
char  nameBuf[NAME_LEN + 1];
int   nameLen = 0;
uint32_t lastMs = 0, lastActivityMs = 0;
int   brightness = 2;                       // 0 low, 1 medium, 2 high
bool  hitThisLevel = false, revivedThisLevel = false, perfectLevel = false, cheeredBest = false;
int   stickerQueue[4], stickerQLen = 0;     // stickers waiting to be shown
float stickerT = 0;                         // how long the current sticker banner shows
int   stickerSel = 0;                       // sticker selected on the stickers screen
State settingsReturn = ST_TITLE;
bool  dimmed = false;

// ============================================================
//   SAVED DATA  (players + leaderboard)
// ============================================================
Profile& P() { return profiles[cur]; }

void profileKey(int i, char* key) { snprintf(key, 6, "p%d", i); }

void saveProfile(int i) {
  char key[6];
  profileKey(i, key);
  prefs.putBytes(key, &profiles[i], sizeof(Profile));
}

void saveScores() { prefs.putBytes("lb", scores, sizeof(scores)); }

bool skinOwnedBy(const Profile& p, int i) {
  bool bought = SKINS[i].price > 0 && i >= FIRST_SHOP_SKIN && ((p.skinsOwned >> (i - FIRST_SHOP_SKIN)) & 1);
  bool earned = isLevelSkin(i) && p.level - 1 >= SKINS[i].lvl;   // levels finished
  return bought || earned;
}

void loadSaved() {
  for (int i = 0; i < MAX_PROFILES; i++) {
    char key[6];
    profileKey(i, key);
    Profile& p = profiles[i];
    memset(&p, 0, sizeof(Profile));
    if (prefs.isKey(key)) prefs.getBytes(key, &p, sizeof(Profile));
    p.name[NAME_LEN] = 0;
    if (p.used != 1) { memset(&p, 0, sizeof(Profile)); continue; }
    if (p.level < 1) p.level = 1;
    if (p.seen > p.level) p.seen = p.level;
    if (p.skin >= N_SKINS || !skinOwnedBy(p, p.skin)) p.skin = 0;
    if (p.hat > N_HATS || (p.hat && !((p.hatsOwned >> (p.hat - 1)) & 1))) p.hat = 0;
    if (p.character > N_FRIENDS || (p.character && !((p.friendsOwned >> (p.character - 1)) & 1))) p.character = 0;
    if (p.helper > N_FRIENDS) p.helper = 0;
    if (p.shields > MAX_SHIELDS) p.shields = MAX_SHIELDS;
  }
  memset(scores, 0, sizeof(scores));
  if (prefs.isKey("lb")) prefs.getBytes("lb", scores, sizeof(scores));
  for (int i = 0; i < MAX_SCORES; i++) scores[i].name[NAME_LEN] = 0;
}

int profileCount() {
  int n = 0;
  for (int i = 0; i < MAX_PROFILES; i++) if (profiles[i].used) n++;
  return n;
}

bool skinUnlocked(int i) { return cur >= 0 && skinOwnedBy(P(), i); }
int  profHat(const Profile& p) { return p.hat ? p.hat - 1 : -1; }
bool hatOwned(int i) { return (P().hatsOwned >> i) & 1; }
bool friendOwned(int f) { return (P().friendsOwned >> f) & 1; }
int  playerChar() { return cur >= 0 ? P().character : CH_AXOLOTL; }

// Adds a score to the leaderboard. Returns its place (0 = top) or -1.
int insertScore(const char* name, uint32_t score, uint16_t level) {
  if (score == 0) return -1;
  int pos = -1;
  for (int i = 0; i < MAX_SCORES; i++) if (score > scores[i].score) { pos = i; break; }
  if (pos < 0) return -1;
  for (int j = MAX_SCORES - 1; j > pos; j--) scores[j] = scores[j - 1];
  memset(&scores[pos], 0, sizeof(ScoreEntry));
  strncpy(scores[pos].name, name, NAME_LEN);
  scores[pos].level = level;
  scores[pos].score = score;
  saveScores();
  return pos;
}

// ============================================================
//   STICKERS
// ============================================================
int countBits(uint32_t v) { int n = 0; while (v) { n += v & 1; v >>= 1; } return n; }

uint32_t stickerProgress(int i) {
  const Profile& p = profiles[cur];
  switch (STICKERS[i].kind) {
    case SK_LEVELS:   return p.level - 1;
    case SK_WORMS:    return p.wormsEaten;
    case SK_BUBBLES:  return p.bubblesPopped;
    case SK_COINS:    return p.coinsCollected;
    case SK_TROPHIES: return p.trophiesFound;
    case SK_PERFECT:  return p.perfectLevels;
    case SK_HATS:     return countBits(p.hatsOwned);
    case SK_FRIENDS:  return countBits(p.friendsOwned);
    default: {        // SK_SCORE: best score, including the game being played now
      uint32_t live = runActive ? (uint32_t)runScore : 0;
      return p.best > live ? p.best : live;
    }
  }
}

bool stickerEarned(int i) { return (profiles[cur].stickers >> i) & 1; }

// Checks every sticker; any newly earned ones pay out coins and show a banner.
void checkStickers() {
  if (cur < 0) return;
  bool any = false;
  for (int i = 0; i < N_STICKERS; i++) {
    if (stickerEarned(i) || stickerProgress(i) < STICKERS[i].target) continue;
    P().stickers |= (1UL << i);
    P().coins += STICKERS[i].reward;
    if (stickerQLen < 4) stickerQueue[stickerQLen++] = i;
    if (stickerT <= 0) stickerT = 2.8f;
    any = true;
  }
  if (any) {
    saveProfile(cur);
    playSound(SND(SND_MILESTONE), 2);
    ledFlash(1, 0, 1, 500);
  }
}

void updateStickerBanner(float dt) {
  if (stickerQLen == 0) return;
  stickerT -= dt;
  if (stickerT > 0) return;
  for (int k = 1; k < stickerQLen; k++) stickerQueue[k - 1] = stickerQueue[k];
  stickerQLen--;
  if (stickerQLen > 0) stickerT = 2.8f;
}

// ============================================================
//   DRAWING HELPERS
// ============================================================
uint16_t pastelHue(float h) {
  float r, g, b, x = h * 6.0f;
  int i = (int)x;
  float f = x - i;
  switch (i % 6) {
    case 0: r = 1; g = f; b = 0; break;
    case 1: r = 1 - f; g = 1; b = 0; break;
    case 2: r = 0; g = 1; b = f; break;
    case 3: r = 0; g = 1 - f; b = 1; break;
    case 4: r = f; g = 0; b = 1; break;
    default: r = 1; g = 0; b = 1 - f; break;
  }
  r = 0.35f + 0.65f * r; g = 0.35f + 0.65f * g; b = 0.35f + 0.65f * b;
  return RGB((uint8_t)(r * 255), (uint8_t)(g * 255), (uint8_t)(b * 255));
}

// Deep, jewel-toned version of the rainbow (for the "Twilight" skin)
uint16_t darkHue(float h, float bright) {
  uint16_t c = pastelHue(h);
  // turn the pastel back into full colour, then darken it
  float r = (((c >> 11) & 0x1F) / 31.0f - 0.35f) / 0.65f;
  float g = (((c >> 5) & 0x3F) / 63.0f - 0.35f) / 0.65f;
  float b = ((c & 0x1F) / 31.0f - 0.35f) / 0.65f;
  r = constrain(r, 0.0f, 1.0f); g = constrain(g, 0.0f, 1.0f); b = constrain(b, 0.0f, 1.0f);
  return RGB((uint8_t)((0.08f + 0.5f * r) * 255 * bright),
             (uint8_t)((0.08f + 0.5f * g) * 255 * bright),
             (uint8_t)((0.08f + 0.5f * b) * 255 * bright));
}

void textAt(const char* s, int x, int y, int font, uint16_t col, uint8_t datum) {
  fb.setTextDatum(datum);
  fb.setTextColor(C_BLACK);
  fb.drawString(s, x + 1, y + 1, font);
  fb.setTextColor(col);
  fb.drawString(s, x, y, font);
}

void shadowText(const char* s, int x, int y, int font, uint16_t col) {
  fb.setTextDatum(MC_DATUM);
  fb.setTextColor(C_BLACK);
  fb.drawString(s, x + 2, y + 2, font);
  fb.setTextColor(col);
  fb.drawString(s, x, y, font);
}

int fitFont(const char* s, int maxW) { return fb.textWidth(s, 4) <= maxW ? 4 : 2; }

void drawButtonBase(int x, int y, int w, int h, uint16_t col) {
  fb.fillRoundRect(x, y + 3, w, h, 10, C_BTN_SHADOW);
  fb.fillRoundRect(x, y, w, h, 10, col);
  fb.drawRoundRect(x, y, w, h, 10, C_WHITE);
}

void drawButton(int x, int y, int w, int h, const char* label, uint16_t col) {
  drawButtonBase(x, y, w, h, col);
  fb.setTextDatum(MC_DATUM);
  fb.setTextColor(C_WHITE);
  fb.drawString(label, x + w / 2, y + h / 2 + 1, fitFont(label, w - 12));
}

void drawButton2(int x, int y, int w, int h, const char* cap, const char* val, uint16_t col) {
  drawButtonBase(x, y, w, h, col);
  fb.setTextDatum(MC_DATUM);
  fb.setTextColor(RGB(225, 230, 255));
  fb.drawString(cap, x + w / 2, y + 11, 2);
  fb.setTextColor(C_WHITE);
  fb.drawString(val, x + w / 2, y + 31, fitFont(val, w - 10));
}

void drawArrowButton(int x, int y, int w, int h, bool left, uint16_t col) {
  drawButtonBase(x, y, w, h, col);
  int cx = x + w / 2, cy = y + h / 2;
  if (left) fb.fillTriangle(cx + 8, cy - 13, cx + 8, cy + 13, cx - 10, cy, C_WHITE);
  else      fb.fillTriangle(cx - 8, cy - 13, cx - 8, cy + 13, cx + 10, cy, C_WHITE);
}

void drawPanel(int x, int y, int w, int h) {
  fb.fillRoundRect(x, y, w, h, 14, C_PANEL);
  fb.drawRoundRect(x, y, w, h, 14, C_WHITE);
}

void drawStickerBadge(int cx, int cy, int r, int i, bool earned);

void drawStickerBanner() {
  if (stickerQLen == 0 || stickerT <= 0) return;
  int i = stickerQueue[0];
  int y = (state == ST_PLAY) ? 196 : 2;   // bottom while playing, top on menus
  drawPanel(30, y, 260, 38);
  drawStickerBadge(52, y + 19, 14, i, true);
  char buf[40];
  snprintf(buf, sizeof(buf), "Sticker: %s!", STICKERS[i].name);
  textAt(buf, 72, y + 11, 2, C_GOLD, ML_DATUM);
  snprintf(buf, sizeof(buf), "+%d coins", STICKERS[i].reward);
  textAt(buf, 72, y + 28, 2, C_WHITE, ML_DATUM);
}

void drawNewBadge(int x, int y) {
  fb.fillRoundRect(x, y, 38, 18, 8, C_RED_BTN);
  fb.drawRoundRect(x, y, 38, 18, 8, C_WHITE);
  fb.setTextDatum(MC_DATUM);
  fb.setTextColor(C_WHITE);
  fb.drawString("NEW!", x + 19, y + 9, 2);
}

// "seen" = (levels finished + 1) when the player last looked at their skins
int  levelsDone() { return P().level - 1; }
bool skinBought(int i) { return SKINS[i].price > 0 && i >= FIRST_SHOP_SKIN && ((P().skinsOwned >> (i - FIRST_SHOP_SKIN)) & 1); }
bool isNewSkin(int i) {
  return isLevelSkin(i) && !skinBought(i) && SKINS[i].lvl >= P().seen && SKINS[i].lvl <= levelsDone();
}
bool hasNewSkins() {
  if (cur < 0) return false;
  for (int i = 0; i < N_SKINS; i++) if (isNewSkin(i)) return true;
  return false;
}

void drawStar(int cx, int cy, int r, uint16_t c);

void drawStickerBadge(int cx, int cy, int r, int i, bool earned) {
  if (earned) {
    fb.fillCircle(cx, cy, r, STICKERS[i].color);
    fb.drawCircle(cx, cy, r, C_WHITE);
    drawStar(cx, cy, r * 6 / 10, C_WHITE);
  } else {
    fb.fillCircle(cx, cy, r, RGB(70, 75, 95));
    fb.drawCircle(cx, cy, r, RGB(140, 145, 165));
    fb.setTextDatum(MC_DATUM);
    fb.setTextColor(RGB(170, 175, 195));
    fb.drawString("?", cx, cy + 1, r > 12 ? 4 : 2);
  }
}

void drawHeart(int x, int y, int s, uint16_t c) {
  fb.fillCircle(x - s, y, s, c);
  fb.fillCircle(x + s, y, s, c);
  fb.fillTriangle(x - 2 * s, y + 1, x + 2 * s, y + 1, x, y + 2 * s + 3, c);
}

void drawStar(int cx, int cy, int r, uint16_t c) {
  int ox[5], oy[5], ix[5], iy[5];
  for (int k = 0; k < 5; k++) {
    float a = -1.5708f + k * 1.2566f;
    ox[k] = cx + (int)(cosf(a) * r);
    oy[k] = cy + (int)(sinf(a) * r);
    ix[k] = cx + (int)(cosf(a + 0.6283f) * r * 0.45f);
    iy[k] = cy + (int)(sinf(a + 0.6283f) * r * 0.45f);
  }
  for (int k = 0; k < 5; k++) {
    fb.fillTriangle(cx, cy, ox[k], oy[k], ix[k], iy[k], c);
    fb.fillTriangle(cx, cy, ix[k], iy[k], ox[(k + 1) % 5], oy[(k + 1) % 5], c);
  }
}


// ---------- colour helpers for effects ----------
uint16_t lerp565(uint16_t a, uint16_t b, float t) {
  if (t < 0) t = 0;
  if (t > 1) t = 1;
  int ar = (a >> 11) & 31, ag = (a >> 5) & 63, ab = a & 31;
  int br = (b >> 11) & 31, bg = (b >> 5) & 63, bb = b & 31;
  int r = ar + (int)((br - ar) * t), g = ag + (int)((bg - ag) * t), bl = ab + (int)((bb - ab) * t);
  return (uint16_t)((r << 11) | (g << 5) | bl);
}
uint16_t darker565(uint16_t c) { return (c >> 1) & 0x7BEF; }

// The colour of something with an effect, at time t
uint16_t fxColor(uint8_t fx, uint16_t base, uint16_t alt, float t) {
  switch (fx) {
    case FX_RAINBOW: return pastelHue(fmodf(t * 0.3f, 1.0f));
    case FX_DARKBOW: return darkHue(fmodf(t * 0.2f, 1.0f), 1.0f);
    case FX_FLASH:   return fmodf(t, 0.6f) < 0.3f ? base : alt;
    case FX_PULSE:   return lerp565(base, alt, 0.5f + 0.5f * sinf(t * 4.0f));
    case FX_SHIFT:   return lerp565(base, alt, 0.5f + 0.5f * sinf(t * 1.2f));
    case FX_DUSK: {  // blue -> pink -> purple -> blue
      const uint16_t dusk[3] = {RGB(95, 125, 210), RGB(230, 140, 185), RGB(130, 90, 190)};
      float ph = fmodf(t * 0.35f, 3.0f);
      int k = (int)ph;
      return lerp565(dusk[k % 3], dusk[(k + 1) % 3], ph - k);
    }
    default:         return base;
  }
}

void fillTopHalfCircle(int x, int y, int r, uint16_t c) {
  for (int dy = 0; dy <= r; dy++) {
    int w = (int)sqrtf((float)(r * r - dy * dy));
    fb.drawFastHLine(x - w, y - dy, 2 * w + 1, c);
  }
}

void drawCoinIcon(int x, int y, int r) {
  fb.fillCircle(x, y, r, C_TROPHY_DARK);
  fb.fillCircle(x, y, r - 1, C_GOLD);
  fb.drawFastVLine(x, y - r / 2, r, C_TROPHY_DARK);
}

// Little twinkles: a '+' shape that comes and goes
const int8_t TWINKLE_PTS[8][2] = {{-8, 0}, {-3, -3}, {2, 3}, {6, -1}, {10, -6}, {14, 1}, {-13, 2}, {0, -1}};
void drawTwinkles(int cx, int cy, int s, float t, uint16_t col, int count, float speed) {
  for (int k = 0; k < count && k < 8; k++) {
    if (sinf(t * speed + k * 1.9f) < 0.55f) continue;
    int px = cx + TWINKLE_PTS[k][0] * s, py = cy + TWINKLE_PTS[k][1] * s;
    fb.drawFastHLine(px - s, py, 2 * s + 1, col);
    fb.drawFastVLine(px, py - s, 2 * s + 1, col);
  }
}

// ---------- hats ----------
// (x, y) is the top-middle of the head the hat sits on
void drawHat(int idx, int x, int y, int s, float t) {
  if (idx < 0 || idx >= N_HATS) return;
  const Hat& h = HATS[idx];
  uint16_t c = fxColor(h.fx, h.col, h.alt, t);
  uint16_t d = darker565(c);
  switch (h.style) {
    case HAT_BEANIE:
      fillTopHalfCircle(x, y, 7 * s, c);
      fb.fillRoundRect(x - 8 * s, y - 2 * s, 16 * s + 1, 3 * s, s, d);
      fb.fillCircle(x, y - 8 * s, 2 * s, h.alt);
      break;
    case HAT_PARTY: {
      uint16_t st = (h.fx == FX_FLASH) ? C_WHITE : h.alt;
      fb.fillTriangle(x - 5 * s, y + s, x + 5 * s, y + s, x, y - 13 * s, c);
      for (int k = 0; k < s; k++) {
        fb.drawFastHLine(x - 3 * s, y - 4 * s + k, 6 * s + 1, st);
        fb.drawFastHLine(x - s, y - 9 * s + k, 2 * s + 1, st);
      }
      fb.fillCircle(x, y - 13 * s, 2 * s, C_GOLD);
      break;
    }
    case HAT_TOP:
      fb.fillRect(x - 5 * s, y - 11 * s, 10 * s, 11 * s, c);
      fb.fillRect(x - 8 * s, y - s, 16 * s, 2 * s, c);
      fb.fillRect(x - 5 * s, y - 4 * s, 10 * s, 2 * s, h.alt);
      fb.drawFastVLine(x - 3 * s, y - 10 * s, 5 * s, RGB(90, 90, 100));
      break;
    case HAT_CAP:
      fillTopHalfCircle(x, y, 6 * s, c);
      fb.fillRoundRect(x + 3 * s, y - 2 * s, 9 * s, 3 * s, s, d);
      fb.fillCircle(x, y - 6 * s, s, h.alt);
      fb.fillCircle(x + s, y - 3 * s, s, h.alt);
      break;
    case HAT_BOW: {
      int bx = x - 3 * s, by = y - 2 * s;
      fb.fillTriangle(bx, by, bx - 7 * s, by - 5 * s, bx - 7 * s, by + 4 * s, c);
      fb.fillTriangle(bx, by, bx + 7 * s, by - 5 * s, bx + 7 * s, by + 4 * s, c);
      fb.fillCircle(bx, by, 2 * s, h.alt);
      break;
    }
    case HAT_CROWN:
      fb.fillRect(x - 6 * s, y - 4 * s, 12 * s + 1, 4 * s, c);
      fb.fillTriangle(x - 6 * s, y - 4 * s, x - 2 * s, y - 4 * s, x - 6 * s, y - 9 * s, c);
      fb.fillTriangle(x - 3 * s, y - 4 * s, x + 3 * s, y - 4 * s, x, y - 10 * s, c);
      fb.fillTriangle(x + 2 * s, y - 4 * s, x + 6 * s, y - 4 * s, x + 6 * s, y - 9 * s, c);
      fb.fillCircle(x, y - 2 * s, s, h.alt);
      fb.fillCircle(x - 4 * s, y - 2 * s, s, RGB(60, 200, 255));
      fb.fillCircle(x + 4 * s, y - 2 * s, s, RGB(60, 220, 120));
      break;
    case HAT_SANTA:
      fb.fillTriangle(x - 6 * s, y, x + 6 * s, y, x - 2 * s, y - 11 * s, h.col);
      fb.fillTriangle(x - 2 * s, y - 11 * s, x, y - 7 * s, x - 9 * s, y - 7 * s, h.col);   // floppy tip
      fb.fillRoundRect(x - 7 * s, y - 2 * s, 14 * s + 1, 3 * s, s, C_WHITE);           // fluffy trim
      fb.fillCircle(x - 9 * s, y - 7 * s, 2 * s, C_WHITE);                              // pom-pom
      break;
    case HAT_WIZARD:
      fb.fillEllipse(x, y, 10 * s, 2 * s, d);
      fb.fillTriangle(x - 6 * s, y, x + 6 * s, y, x + 3 * s, y - 17 * s, c);
      drawStar(x, y - 6 * s, 2 * s + 1, h.alt);
      break;
  }
  if (h.fx == FX_SPARKLE) {
    const int8_t pts[4][2] = {{-5, -3}, {3, -9}, {6, -2}, {0, -12}};
    for (int k = 0; k < 4; k++) {
      if (sinf(t * 6 + k * 2.1f) < 0.4f) continue;
      int px = x + pts[k][0] * s, py = y + pts[k][1] * s;
      fb.drawFastHLine(px - s, py, 2 * s + 1, C_WHITE);
      fb.drawFastVLine(px, py - s, 2 * s + 1, C_WHITE);
    }
  }
}

// The star of the show! Facing right. s = scale (1 in game, 2-3 in menus)
void drawAxolotl(int cx, int cy, int s, float t, const Skin& sk, bool locked = false, int hat = -1) {
  uint16_t body = sk.body, belly = sk.belly, gill = sk.gill, eye = sk.eye;
  if (sk.fx == FX_FLASH || sk.fx == FX_PULSE || sk.fx == FX_SHIFT || sk.fx == FX_DUSK) {
    body  = fxColor(sk.fx, sk.body, sk.alt, t);
    belly = lerp565(body, C_WHITE, 0.5f);
    if (sk.fx == FX_DUSK) gill = darker565(lerp565(body, C_WHITE, 0.2f)) | 0x0841;
  }
  if (sk.fx == FX_RAINBOW) {
    float h = fmodf(t * 0.3f, 1.0f);
    body  = pastelHue(h);
    belly = pastelHue(fmodf(h + 0.12f, 1.0f));
    gill  = pastelHue(fmodf(h + 0.5f, 1.0f));
  } else if (sk.fx == FX_DARKBOW) {
    float h = fmodf(t * 0.2f, 1.0f);
    body  = darkHue(h, 1.0f);
    belly = darkHue(fmodf(h + 0.12f, 1.0f), 1.4f);
    gill  = darkHue(fmodf(h + 0.5f, 1.0f), 1.5f);
  }
  if (locked) { body = RGB(60, 70, 95); belly = RGB(80, 90, 115); gill = RGB(45, 55, 80); eye = RGB(30, 35, 50); }

  float wig = sinf(t * 9.0f);
  float paddle = sinf(t * 8.0f);

  // tail
  int tipX = cx - 23 * s;
  int tipY = cy + (int)(wig * 4 * s);
  fb.fillTriangle(cx - 7 * s, cy - 4 * s, cx - 7 * s, cy + 5 * s, tipX, tipY, body);
  fb.fillTriangle(cx - 9 * s, cy - 1 * s, cx - 9 * s, cy + 2 * s, tipX + 4 * s, tipY, belly);

  // legs (paddling)
  int lp = (int)(paddle * 2 * s);
  fb.fillRoundRect(cx - 8 * s + lp, cy + 3 * s, 3 * s, 6 * s, s, body);
  fb.fillRoundRect(cx + 4 * s - lp, cy + 3 * s, 3 * s, 6 * s, s, body);

  // body + belly
  fb.fillEllipse(cx - 1 * s, cy + 1 * s, 11 * s, 6 * s, body);
  fb.fillEllipse(cx + 1 * s, cy + 4 * s, 8 * s, 2 * s, belly);

  // feathery gills
  const int8_t gb[3][2] = {{7, -6}, {6, -3}, {6, 0}};
  const int8_t gt[3][2] = {{4, -14}, {0, -11}, {-3, -6}};
  for (int k = 0; k < 3; k++) {
    int bx = cx + gb[k][0] * s, by = cy + gb[k][1] * s;
    int ex = cx + gt[k][0] * s + (int)(sinf(t * 6 + k) * s), ey = cy + gt[k][1] * s;
    for (int d = 0; d <= s; d++) fb.drawLine(bx + d, by, ex + d, ey, gill);
    fb.fillCircle(ex, ey, 2 * s, gill);
    fb.fillCircle((bx + ex) / 2, (by + ey) / 2, s, gill);
  }

  // head
  fb.fillEllipse(cx + 11 * s, cy - 1 * s, 9 * s, 7 * s, body);

  // rosy cheek
  if (!locked) fb.fillCircle(cx + 12 * s, cy + 2 * s, s + s / 2, RGB(255, 120, 160));

  // eye (blinks now and then)
  int ex = cx + 14 * s, ey = cy - 3 * s;
  bool blink = fmodf(t, 3.2f) < 0.12f;
  if (blink) {
    fb.fillRect(ex - 2 * s, ey, 4 * s, s < 2 ? 1 : s / 2 + 1, eye);
  } else {
    fb.fillCircle(ex, ey, 2 * s, eye);
    fb.fillCircle(ex + s, ey - s, s / 2, C_WHITE);
  }

  // big smile
  uint16_t mouth = locked ? RGB(40, 45, 60) : RGB(120, 40, 70);
  int th = s > 1 ? s / 2 + 1 : 1;
  for (int d = 0; d < th; d++) {
    fb.drawLine(cx + 14 * s, cy + 2 * s + d, cx + 16 * s, cy + 3 * s + d, mouth);
    fb.drawLine(cx + 16 * s, cy + 3 * s + d, cx + 19 * s, cy + 1 * s + d, mouth);
  }

  if (!locked) {
    if (sk.fx == FX_SPARKLE)    drawTwinkles(cx, cy, s, t, sk.alt, 7, 6.0f);
    else if (sk.fx == FX_STARS) drawTwinkles(cx, cy, s, t, sk.alt, 8, 3.5f);
    drawHat(hat, cx + 11 * s, cy - 7 * s, s, t);
  }
}

// ---------- the axolotl friends (facing right) ----------
// At s = 1 they're small helpers; at s = 2 they're the size of the player.
void drawFriend(int id, int cx, int cy, int s, float t) {
  const Friend& f = FRIENDS[id];
  uint16_t c1 = f.c1, c2 = f.c2, c3 = f.c3;
  switch (id) {
    case 0: {  // seahorse
      int wig = (int)(sinf(t * 14) * s);
      fb.fillCircle(cx - s, cy + 6 * s, 2 * s, c1);              // curly tail
      fb.fillCircle(cx - 2 * s, cy + 8 * s, (3 * s) / 2, c1);
      fb.fillCircle(cx - s, cy + 10 * s, s, c1);
      fb.fillCircle(cx + s, cy + 10 * s, s, c1);
      fb.fillTriangle(cx - 2 * s, cy - s, cx - 2 * s, cy + 3 * s, cx - 6 * s, cy + s + wig, c3);  // back fin
      fb.fillEllipse(cx, cy + s, 3 * s, 5 * s, c1);              // body
      fb.fillEllipse(cx + s, cy + 2 * s, s, 3 * s, c2);          // belly
      for (int k = 0; k < 3; k++) fb.drawFastHLine(cx, cy - s + k * 2 * s, 2 * s, c3);
      fb.fillCircle(cx + s, cy - 5 * s, 3 * s, c1);              // head
      fb.fillRect(cx + 3 * s, cy - 6 * s, 4 * s, 2 * s, c1);     // snout
      fb.fillCircle(cx - s, cy - 8 * s, s, c3);                  // crest
      fb.fillCircle(cx + 2 * s, cy - 6 * s, s, C_BLACK);         // eye
      if (s >= 2) fb.fillCircle(cx + 2 * s + s / 2, cy - 6 * s - s / 2, s / 2, C_WHITE);
      break;
    }
    case 1: {  // shrimp
      int wig = (int)(sinf(t * 10) * s);
      fb.fillTriangle(cx - 7 * s, cy, cx - 11 * s, cy - 4 * s + wig, cx - 11 * s, cy + 3 * s + wig, c3);  // tail fan
      for (int k = 0; k < 4; k++) {                              // little legs
        int lx = cx - 3 * s + k * 3 * s;
        fb.drawLine(lx, cy + 2 * s, lx + (int)(sinf(t * 12 + k) * s), cy + 5 * s, c3);
      }
      fb.fillCircle(cx - 6 * s, cy, 2 * s, c1);
      fb.fillCircle(cx - 3 * s, cy + s, 3 * s, c1);
      fb.fillCircle(cx + s, cy + s, 3 * s, c1);
      fb.fillCircle(cx + 5 * s, cy - s, 3 * s, c1);              // head
      fb.fillCircle(cx - 3 * s, cy + 2 * s, s, c2);
      fb.fillCircle(cx + s, cy + 2 * s, s, c2);
      fb.drawFastVLine(cx - s, cy - 2 * s, 4 * s, c3);
      fb.drawFastVLine(cx + 3 * s, cy - 3 * s, 4 * s, c3);
      fb.drawLine(cx + 7 * s, cy - 3 * s, cx + 13 * s, cy - 9 * s + wig, c3);   // feelers
      fb.drawLine(cx + 6 * s, cy - 3 * s, cx + 10 * s, cy - 10 * s - wig, c3);
      fb.fillCircle(cx + 6 * s, cy - 2 * s, s, C_BLACK);         // eye
      break;
    }
    case 2: {  // turtle
      int fl = (int)(sinf(t * 7) * s);
      fb.fillEllipse(cx - 5 * s + fl, cy + 3 * s, 3 * s, s, c2);  // flippers
      fb.fillEllipse(cx + 4 * s - fl, cy + 3 * s, 3 * s, s, c2);
      fb.fillTriangle(cx - 7 * s, cy + s, cx - 7 * s, cy + 3 * s, cx - 10 * s, cy + 2 * s, c2);  // tail
      fb.fillCircle(cx + 8 * s, cy, 3 * s, c2);                  // head
      fb.fillCircle(cx + 9 * s, cy - s, s, C_BLACK);             // eye
      fb.drawLine(cx + 9 * s, cy + s, cx + 11 * s, cy + s, c3);  // smile
      fillTopHalfCircle(cx, cy + 2 * s, 7 * s, c1);              // shell
      fb.fillRect(cx - 7 * s, cy + 2 * s, 14 * s + 1, s, c3);
      fb.fillCircle(cx, cy - 2 * s, 2 * s, c3);
      fb.fillCircle(cx - 4 * s, cy, s, c3);
      fb.fillCircle(cx + 4 * s, cy, s, c3);
      break;
    }
    default: { // pufferfish
      int wig = (int)(sinf(t * 12) * s);
      for (int k = 0; k < 12; k++) {                             // spikes
        float a = k * 0.5236f;
        fb.drawLine(cx + (int)(cosf(a) * 6 * s), cy + (int)(sinf(a) * 6 * s),
                    cx + (int)(cosf(a) * 8 * s), cy + (int)(sinf(a) * 8 * s), c3);
      }
      fb.fillTriangle(cx - 6 * s, cy, cx - 10 * s, cy - 3 * s + wig, cx - 10 * s, cy + 3 * s + wig, c3);  // tail
      fb.fillCircle(cx, cy, 6 * s, c1);                          // body
      fb.fillEllipse(cx, cy + 3 * s, 4 * s, 2 * s, c2);          // belly
      fb.fillTriangle(cx - s, cy, cx - 4 * s, cy - 2 * s + wig, cx - 4 * s, cy + 2 * s + wig, c3);  // fin
      fb.fillCircle(cx + 3 * s, cy - 2 * s, 2 * s, C_WHITE);     // big eye
      fb.fillCircle(cx + 3 * s + s / 2, cy - 2 * s, s, C_BLACK);
      fb.fillCircle(cx + 6 * s, cy + s, s, c3);                  // little "o" mouth
      break;
    }
  }
}

// Where a hat sits on each friend
void friendHatAnchor(int id, int cx, int cy, int s, int& hx, int& hy) {
  switch (id) {
    case 0:  hx = cx + s;     hy = cy - 8 * s; break;
    case 1:  hx = cx + 5 * s; hy = cy - 4 * s; break;
    case 2:  hx = cx + 8 * s; hy = cy - 3 * s; break;
    default: hx = cx + s;     hy = cy - 6 * s; break;
  }
}

// Draws whoever the player is: the axolotl (with skin) or a friend, plus hat
void drawCharacter(int ch, int skin, int hat, int cx, int cy, int s, float t) {
  if (ch == CH_AXOLOTL || ch > N_FRIENDS) {
    drawAxolotl(cx, cy, s, t, SKINS[skin], false, hat);
    return;
  }
  int fs = (s == 1) ? 2 : s + 1;   // friends are drawn a bit bigger than their scale
  drawFriend(ch - 1, cx, cy, fs, t);
  if (hat >= 0) {
    int hx, hy;
    friendHatAnchor(ch - 1, cx, cy, fs, hx, hy);
    drawHat(hat, hx, hy, s, t);
  }
}

bool inGameState() {
  return state == ST_READY || state == ST_PLAY || state == ST_PAUSE || state == ST_CLEAR || state == ST_OVER ||
         state == ST_REVIVE;
}

int currentTheme() { return inGameState() ? (curLevel - 1) % N_THEMES : 0; }

// Top of the water. 0 normally; on shallow levels it's the 2nd colour line,
// and everything above it is sky.
int surfaceY() {
  if (!inGameState() || THEMES[currentTheme()].kind != TK_SHALLOW) return 0;
  return 2 * SAND_Y / 6;
}

// A soft, wobbly disc - like the sun seen through moving water
void drawWobblyDisc(int cx, int cy, int r, uint16_t c, float wob, float t) {
  for (int dy = -r; dy <= r; dy++) {
    int w = (int)(sqrtf((float)(r * r - dy * dy)) * 1.15f);
    int off = (int)(sinf(dy * 0.35f + t * 2.2f) * wob);
    fb.drawFastHLine(cx - w + off, cy + dy, 2 * w + 1, c);
  }
}

void drawBackground() {
  const Theme& T = THEMES[currentTheme()];
  int surf = surfaceY();
  for (int i = 0; i < 6; i++) {
    int y0 = i * SAND_Y / 6, y1 = (i + 1) * SAND_Y / 6;
    if (y1 <= surf) continue;          // that part is sky
    fb.fillRect(0, y0, W, y1 - y0, T.water[i]);
  }

  if (surf > 0) {
    // sky
    for (int y = 0; y < surf; y += 4) fb.fillRect(0, y, W, 4, lerp565(T.skyTop, T.skyBottom, (float)y / surf));
    fb.fillCircle(290, 50, 13, RGB(255, 240, 150));          // sun
    fb.fillCircle(290, 50, 10, RGB(255, 225, 90));
    for (int k = 0; k < 3; k++) {                            // drifting clouds
      float m = fmodf(k * 130.0f - scrollX * 0.15f, 420.0f);
      if (m < 0) m += 420.0f;
      int cx = (int)m - 50, cy = 18 + k * 13;
      fb.fillEllipse(cx, cy, 18, 6, C_WHITE);
      fb.fillEllipse(cx - 10, cy + 2, 10, 5, C_WHITE);
      fb.fillEllipse(cx + 9, cy - 3, 9, 5, C_WHITE);
    }
    // gentle waves along the surface
    for (int x = -4; x < W + 8; x += 8) {
      int wy = surf + (int)(sinf((x + scrollX) * 0.06f + gameT * 2.0f) * 1.5f);
      fb.fillCircle(x, wy + 1, 4, T.water[2]);
      fb.drawFastHLine(x - 2, wy - 2, 5, lerp565(T.water[2], C_WHITE, 0.6f));
    }
    // sparkles where the sun hits the water
    for (int k = 0; k < 5; k++)
      if (sinf(gameT * 3 + k * 1.7f) > 0.3f) fb.drawFastHLine(262 + k * 10 - (k % 2) * 6, surf + 4 + (k % 3) * 3, 6, C_WHITE);
  }

  if (T.kind == TK_SUNSET) {
    // the sun shining down through the water: blurry glow, wobbling with the waves
    drawWobblyDisc(230, 50, 34, lerp565(T.water[1], RGB(255, 200, 120), 0.35f), 3.0f, gameT);
    drawWobblyDisc(230, 50, 26, lerp565(T.water[1], RGB(255, 210, 120), 0.65f), 2.5f, gameT + 0.4f);
    drawWobblyDisc(230, 50, 19, RGB(255, 228, 150), 2.0f, gameT + 0.8f);
    for (int k = 0; k < 6; k++) {                            // shimmering light below it
      int y = 90 + k * 10, w = 28 - k * 4;
      int off = (int)(sinf(gameT * 2.5f + k) * 6);
      fb.drawFastHLine(230 - w + off, y, 2 * w, lerp565(T.water[2 + k / 2], RGB(255, 220, 160), 0.45f - k * 0.06f));
    }
  }

  if (T.kind == TK_KELP) {
    // tall kelp swaying far away
    for (int k = 0; k < 6; k++) {
      float m = fmodf(k * 64.0f - scrollX * 0.4f, 384.0f);
      if (m < 0) m += 384.0f;
      int kx = (int)m - 32, top = 30 + (k * 37) % 50;
      for (int y = SAND_Y; y > top; y -= 7) {
        int sway = (int)(sinf(gameT * 1.5f + k + y * 0.03f) * (SAND_Y - y) * 0.05f);
        fb.fillCircle(kx + sway, y, 4, RGB(45, 120, 90));
      }
    }
  }
  // far-away hills (slow parallax)
  float s = scrollX * 0.25f;
  for (int x = 0; x < W; x += 4) {
    int h = 16 + (int)(9 * sinf((x + s) * 0.025f) + 5 * sinf((x + s) * 0.071f));
    fb.fillRect(x, SAND_Y - h, 4, h, T.hill);
  }
  // rising background bubbles
  for (int i = 0; i < MAX_BG; i++)
    if (bgb[i].y > surf + 3) fb.drawCircle((int)bgb[i].x, (int)bgb[i].y, bgb[i].r, C_BUBBLE);
  // sandy floor
  fb.fillRect(0, SAND_Y, W, H - SAND_Y, T.sand);
  fb.fillRect(0, SAND_Y, W, 3, T.sandDark);
  for (int i = 0; i < 12; i++) {
    float m = fmodf(i * 47.0f - scrollX, 360.0f);
    if (m < 0) m += 360.0f;
    fb.fillCircle((int)m - 20, SAND_Y + 7 + (i * 7) % 16, 2 + (i % 3), C_PEBBLE);
  }
}

void drawRock(const Obstacle& o) {
  int x = (int)o.x, w = o.w, h = o.h, r = w / 4;
  if (o.type == OB_ROCK) {
    int top = SAND_Y - h;
    fb.fillRoundRect(x, top + r, w, h - r + 4, 6, C_ROCK);
    fb.fillCircle(x + w / 3, top + r, r, C_ROCK);
    fb.fillCircle(x + 2 * w / 3, top + r + 3, r, C_ROCK);
    fb.fillCircle(x + w / 3 - r / 3, top + r - r / 3, r / 3 + 1, C_ROCK_LIGHT);
    fb.fillCircle(x + 2 * w / 3, top + h / 2 + 4, 3, C_ROCK_DARK);
    fb.fillCircle(x + w / 4, top + h * 3 / 4, 2, C_ROCK_DARK);
    fb.fillCircle(x + w / 3 + 2, top + 2, 3, C_MOSS);
    fb.fillCircle(x + w / 3 + 7, top + 3, 2, C_MOSS);
  } else if (o.y > 0) {  // shallow water: a floating log with mossy roots hanging down
    int top = (int)o.y;
    for (int k = 0; k < 4; k++) {
      int rx = x + 6 + k * (w - 12) / 3;
      int len = h - (k % 2) * 8;
      for (int yy = 0; yy < len; yy += 5) {
        int sway = (int)(sinf(gameT * 2.0f + o.phase + k + yy * 0.08f) * yy * 0.06f);
        fb.fillCircle(rx + sway, top + yy, 3 - yy * 2 / (len + 1), (yy / 5) % 2 ? C_WEED : C_WEED_DARK);
      }
    }
    fb.fillRoundRect(x - 4, top - 6, w + 8, 12, 6, RGB(140, 95, 55));      // the log
    fb.drawFastHLine(x + 2, top - 2, w - 6, RGB(105, 70, 40));
    fb.fillEllipse(x + w + 2, top, 3, 5, RGB(190, 145, 95));               // cut end
  } else {  // hanging rock from the top
    int bottom = h;
    fb.fillRoundRect(x, -6, w, bottom - r + 6, 6, C_ROCK);
    fb.fillCircle(x + w / 3, bottom - r, r, C_ROCK);
    fb.fillCircle(x + 2 * w / 3, bottom - r - 3, r, C_ROCK);
    fb.fillCircle(x + w / 4, bottom / 3, r / 3 + 1, C_ROCK_LIGHT);
    fb.fillCircle(x + 2 * w / 3, bottom / 2, 3, C_ROCK_DARK);
  }
}

void drawWeed(const Obstacle& o) {
  int base = SAND_Y + 2;
  for (int strand = 0; strand < 2; strand++) {
    int sx = (int)o.x + strand * 8;
    int sh = strand == 0 ? o.h : o.h * 6 / 10;
    for (int yy = 0; yy < sh; yy += 5) {
      float sway = sinf(gameT * 2.5f + o.phase + strand + yy * 0.06f) * (yy * 0.07f);
      int r = 5 - (yy * 3) / sh;
      uint16_t c = ((yy / 5) % 2) ? C_WEED : C_WEED_DARK;
      fb.fillCircle(sx + (int)sway, base - yy, r, c);
      if (yy % 20 == 10) fb.fillEllipse(sx + (int)sway + (strand ? 5 : -5), base - yy, 4, 2, C_WEED);
    }
  }
}

void drawFish(const Obstacle& o) {
  int x = (int)o.x, y = (int)o.y;
  int w = (int)(sinf(gameT * 12 + o.phase) * 3);
  fb.fillTriangle(x + 9, y, x + 19, y - 7 + w, x + 19, y + 7 + w, o.col);
  fb.fillEllipse(x, y, 12, 7, o.col);
  fb.fillTriangle(x - 2, y - 6, x + 7, y - 6, x + 3, y - 11, o.col);
  fb.drawFastVLine(x + 3, y - 5, 11, C_WHITE);
  fb.drawFastVLine(x + 4, y - 5, 11, C_WHITE);
  fb.fillCircle(x - 6, y - 2, 3, C_WHITE);
  fb.fillCircle(x - 7, y - 2, 1, C_BLACK);
  fb.drawLine(x - 11, y + 2, x - 8, y + 3, C_BLACK);   // little smile
}

void drawWorm(int x, int y, float phase) {
  for (int i = 5; i >= 0; i--) {
    int wx = x - 8 + i * 3;
    int wy = y + (int)(sinf(gameT * 8 + phase + i * 0.9f) * 2.5f);
    fb.fillCircle(wx, wy, i == 0 ? 3 : 2, (i % 2) ? C_WORM_DARK : C_WORM);
    if (i == 0) fb.drawPixel(wx - 1, wy - 1, C_BLACK);   // tiny eye
  }
}

void drawTrophy(int x, int y) {
  fb.drawCircle(x, y - 2, 14, RGB(255, 245, 170));        // glow
  fb.drawCircle(x - 8, y - 5, 3, C_TROPHY);               // handles
  fb.drawCircle(x + 8, y - 5, 3, C_TROPHY);
  fb.fillRoundRect(x - 7, y - 10, 15, 11, 4, C_TROPHY);   // cup
  fb.fillRect(x - 1, y + 1, 3, 4, C_TROPHY_DARK);         // stem
  fb.fillRoundRect(x - 6, y + 5, 13, 4, 1, C_TROPHY_DARK); // base
  fb.fillRect(x - 4, y - 8, 2, 5, C_WHITE);               // shine
  if (((int)(gameT * 4)) % 2) {                           // twinkle
    fb.drawFastHLine(x + 9, y - 12, 5, C_WHITE);
    fb.drawFastVLine(x + 11, y - 14, 5, C_WHITE);
  }
}

void drawPickups() {
  for (int i = 0; i < MAX_PK; i++) {
    Pickup& p = pks[i];
    if (!p.on) continue;
    int x = (int)p.x, y = (int)(p.y + sinf(gameT * 3 + p.phase) * 3);
    switch (p.type) {
      case PK_BUBBLE:
        fb.fillCircle(x, y, 8, C_BUBBLE);
        fb.fillCircle(x, y, 6, RGB(100, 195, 240));
        fb.fillCircle(x - 3, y - 3, 2, C_WHITE);
        break;
      case PK_HEART:
        fb.fillCircle(x, y, 10, C_WHITE);
        drawHeart(x, y - 2, 4, C_HEART);
        break;
      case PK_WORM:
        drawWorm(x, y, p.phase);
        break;
      case PK_TROPHY:
        drawTrophy(x, y);
        break;
      case PK_COIN: {           // spinning coin
        int w = (int)(fabsf(cosf(gameT * 4 + p.phase)) * 6) + 1;
        fb.fillEllipse(x, y, w, 6, C_TROPHY_DARK);
        if (w > 2) fb.fillEllipse(x, y, w - 1, 5, C_GOLD);
        if (w > 3) fb.drawFastVLine(x - w / 3, y - 3, 6, C_WHITE);
        break;
      }
    }
  }
}

void drawObstacles() {
  for (int i = 0; i < MAX_OBS; i++) {
    Obstacle& o = obs[i];
    if (!o.on) continue;
    if (o.type == OB_ROCK || o.type == OB_ROCK_TOP) drawRock(o);
    else if (o.type == OB_WEED) drawWeed(o);
    else drawFish(o);
  }
}

int currentSkin() { return cur >= 0 ? P().skin : 0; }

// The helper friend swims in a circle around you
void helperPos(float& x, float& y) {
  float a = gameT * 2.5f;
  x = PLAYER_X + cosf(a) * 30;
  y = py + sinf(a) * 22;
}

void drawHelper(bool front) {
  if (levelHelper < 0) return;
  bool isFront = sinf(gameT * 2.5f) >= 0;   // in front when it's below you
  if (isFront != front) return;
  float hx, hy;
  helperPos(hx, hy);
  drawFriend(levelHelper, (int)hx, (int)hy, 1, gameT * 1.3f);
}

void drawPlayer() {
  drawHelper(false);
  bool blinkOff = invuln > 0 && ((int)(invuln * 12)) % 2 == 0;   // blink after a bump
  int bob = (state == ST_READY) ? (int)(sinf(gameT * 3) * 3) : 0;
  if (!blinkOff) drawCharacter(playerChar(), currentSkin(), profHat(P()), PLAYER_X, (int)py + bob, 1, gameT);
  if (shieldT > 0 && (shieldT > 1.5f || ((int)(shieldT * 8)) % 2)) {   // shield bubble (blinks near the end)
    fb.drawCircle(PLAYER_X, (int)py, 23, RGB(170, 230, 255));
    fb.drawCircle(PLAYER_X, (int)py, 22, C_WHITE);
    float a = gameT * 3;
    fb.fillCircle(PLAYER_X + (int)(cosf(a) * 17), (int)py + (int)(sinf(a) * 17), 2, C_WHITE);
  }
  drawHelper(true);
}

bool showShieldButton() { return cur >= 0 && (P().shields > 0 || shieldT > 0); }

void drawSparksAndText() {
  for (int i = 0; i < MAX_SPARK; i++)
    if (sparks[i].on) fb.fillCircle((int)sparks[i].x, (int)sparks[i].y, sparks[i].life > 0.3f ? 2 : 1, sparks[i].c);
  for (int i = 0; i < MAX_TXT; i++)
    if (ftxt[i].on) shadowText(ftxt[i].s, (int)ftxt[i].x, (int)ftxt[i].y, 4, ftxt[i].c);
}

void drawHUD(bool showPause) {
  for (int i = 0; i < lives; i++) drawHeart(13 + i * 16, 12, 4, C_HEART);

  // coins in your wallet
  char cbuf[12];
  snprintf(cbuf, sizeof(cbuf), "%lu", (unsigned long)P().coins);
  drawCoinIcon(13, 31, 5);
  textAt(cbuf, 22, 31, 2, C_GOLD, ML_DATUM);

  // level progress bar
  int goal = levelGoal(curLevel);
  int bx = 110, bw = 100, by = 5;
  int shown = levelScore < goal ? levelScore : goal;
  int fill = (int)((long)bw * shown / goal);
  fb.fillRoundRect(bx, by, bw, 11, 4, RGB(0, 50, 90));
  if (fill > 3) fb.fillRoundRect(bx, by, fill, 11, 4, C_GOLD);
  fb.drawRoundRect(bx, by, bw, 11, 4, C_WHITE);
  char buf[24];
  snprintf(buf, sizeof(buf), "Level %d:  %d / %d", curLevel, levelScore, goal);
  textAt(buf, W / 2, 27, 2, C_WHITE, MC_DATUM);

  // total score for this run
  snprintf(buf, sizeof(buf), "%d", runScore);
  textAt(buf, 246, 12, 2, C_GOLD, MR_DATUM);

  if (showPause && showShieldButton()) {   // tap to use a shield
    int bx2 = W - 70;
    fb.fillRoundRect(bx2, 4, 30, 26, 6, shieldT > 0 ? RGB(40, 120, 200) : RGB(0, 60, 115));
    fb.drawCircle(bx2 + 12, 17, 8, C_BUBBLE);
    fb.drawCircle(bx2 + 12, 17, 7, C_WHITE);
    if (shieldT > 0) snprintf(buf, sizeof(buf), "%d", (int)shieldT + 1);
    else             snprintf(buf, sizeof(buf), "%d", P().shields);
    textAt(buf, bx2 + 25, 22, 2, shieldT > 0 ? C_GOLD : C_WHITE, MC_DATUM);
  }

  if (showPause) {
    fb.fillRoundRect(W - 34, 4, 28, 26, 6, RGB(0, 60, 115));
    fb.fillRect(W - 26, 10, 5, 14, C_WHITE);
    fb.fillRect(W - 17, 10, 5, 14, C_WHITE);
  }
}

void drawGame() {
  drawBackground();
  drawPickups();
  drawObstacles();
  drawPlayer();
}

// ============================================================
//   EFFECTS
// ============================================================
void burst(float x, float y, uint16_t c, int n) {
  for (int i = 0; i < MAX_SPARK && n > 0; i++) {
    if (sparks[i].on) continue;
    sparks[i] = {true, x, y, frand(-70, 70), frand(-90, 30), frand(0.4f, 0.9f), c};
    n--;
  }
}

void addText(const char* s, float x, float y, uint16_t c) {
  for (int i = 0; i < MAX_TXT; i++) {
    if (ftxt[i].on) continue;
    ftxt[i].on = true;
    strncpy(ftxt[i].s, s, sizeof(ftxt[i].s) - 1);
    ftxt[i].s[sizeof(ftxt[i].s) - 1] = 0;
    ftxt[i].x = x; ftxt[i].y = y; ftxt[i].life = 1.2f; ftxt[i].c = c;
    return;
  }
}

void initBg() {
  for (int i = 0; i < MAX_BG; i++)
    bgb[i] = {frand(0, W), frand(0, SAND_Y), frand(15, 35), (uint8_t)random(1, 4)};
}

void updateCommon(float dt, float worldSpd) {
  for (int i = 0; i < MAX_BG; i++) {
    BgBubble& b = bgb[i];
    b.y -= b.spd * dt;
    b.x -= worldSpd * 0.3f * dt;
    if (b.y < surfaceY() - 5 || b.x < -5) { b.y = SAND_Y + frand(0, 20); b.x = frand(0, W + 40); }
  }
  for (int i = 0; i < MAX_SPARK; i++) {
    Spark& s = sparks[i];
    if (!s.on) continue;
    s.x += (s.vx - worldSpd * 0.5f) * dt;
    s.y += s.vy * dt;
    s.vy += 60 * dt;
    s.life -= dt;
    if (s.life <= 0) s.on = false;
  }
  for (int i = 0; i < MAX_TXT; i++) {
    if (!ftxt[i].on) continue;
    ftxt[i].y -= 22 * dt;
    ftxt[i].life -= dt;
    if (ftxt[i].life <= 0) ftxt[i].on = false;
  }
}

// ============================================================
//   GAME FLOW
// ============================================================
void goReady() { state = ST_READY; readyT = 0; readyCount = -1; }

void clearWorld() {
  for (int i = 0; i < MAX_OBS; i++) obs[i].on = false;
  for (int i = 0; i < MAX_PK; i++) pks[i].on = false;
  for (int i = 0; i < MAX_SPARK; i++) sparks[i].on = false;
  for (int i = 0; i < MAX_TXT; i++) ftxt[i].on = false;
}

int maxLives() { return MODES[mode].lives + (playerChar() == CH_TURTLE ? 1 : 0); }
float shieldTime() { return playerChar() == CH_PUFFER ? 8.0f : 5.0f; }

void startLevel() {
  clearWorld();
  vy = 0; invuln = 0;
  levelScore = 0;
  lives = maxLives();              // hearts refill every level
  speed = MODES[mode].speed;
  spawnDist = 120;
  shieldT = 0;
  hitThisLevel = false;
  revivedThisLevel = false;
  levelHelper = P().helper ? P().helper - 1 : -1;   // stays until the level is finished
  goReady();
  py = (surfaceY() + SAND_Y) / 2;   // start in the middle of the water
}

void startRun() {
  runScore = 0;
  cheeredBest = false;
  runActive = true;
  curLevel = P().level;
  startLevel();
}

// Ends the current run: saves best score and adds it to the leaderboard.
void endRun() {
  if (!runActive) return;
  runActive = false;
  if ((uint32_t)runScore > P().best) P().best = runScore;
  saveProfile(cur);                  // also saves coins and shields
  overRank = insertScore(P().name, runScore, curLevel);
  checkStickers();
}

void levelComplete() {
  state = ST_CLEAR;
  overT = 0;
  newSkinName = nullptr;
  newSkinIdx = -1;
  Profile& p = P();
  if (curLevel >= p.level) {            // first time finishing this level
    for (int i = 0; i < N_SKINS; i++)
      if (isLevelSkin(i) && SKINS[i].lvl == curLevel && !skinBought(i)) { newSkinIdx = i; newSkinName = SKINS[i].name; }
  }
  coinBonus = levelCoinBonus(curLevel);
  perfectLevel = !hitThisLevel;
  if (perfectLevel) { coinBonus += PERFECT_BONUS; p.perfectLevels++; }
  p.coins += coinBonus;
  if (levelHelper >= 0) p.helper = 0;   // the helper's job is done
  if (curLevel >= p.level) p.level = curLevel + 1;
  if ((uint32_t)runScore > p.best) p.best = runScore;
  saveProfile(cur);
  checkStickers();
  playSound(SND(SND_CLEAR), 4);
  ledFlash(0, 1, 0, 800);
  lockInput(900);
}

void finishGameOver();

// Out of hearts: offer a revive if there are enough coins (once per level)
void gameOver() {
  if (!revivedThisLevel && P().coins >= (uint32_t)REVIVE_PRICE) {
    state = ST_REVIVE;
    playSound(SND(SND_HIT), 4);
    lockInput(900);
    return;
  }
  finishGameOver();
}

void finishGameOver() {
  state = ST_OVER;
  overT = 0;
  invuln = 0;
  newRecord = runScore > 0 && (uint32_t)runScore > P().best;
  endRun();
  if (newRecord) { playSound(SND(SND_RECORD), 4); ledFlash(1, 1, 0, 800); }
  else           { playSound(SND(SND_OVER), 4); }
  static const char* msgs[] = {"Great swim!", "Nice try!", "So close!", "Well done!", "Good job!"};
  overMsg = newRecord ? "New Record!" : msgs[random(5)];
  lockInput(900);   // stops accidental taps on the buttons
}

void addScore(int n) {
  int before = levelScore;
  levelScore += n;
  runScore += n;
  if (!cheeredBest && P().best > 0 && (uint32_t)runScore > P().best) {   // beat your best mid-game
    cheeredBest = true;
    addText("New best!", W / 2, 96, C_GOLD);
  }
  if (levelScore >= levelGoal(curLevel)) { levelComplete(); return; }
  if (levelScore / 25 > before / 25) {
    static const char* praise[] = {"Awesome!", "Super!", "Wow!", "Amazing!", "Splashy!", "Keep going!"};
    addText(praise[random(6)], W / 2, 70, C_GOLD);
    playSound(SND(SND_MILESTONE), 2);
    ledFlash(0, 0, 1, 400);
  }
}

void hurt() {
  lives--;
  hitThisLevel = true;
  invuln = 1.8f;
  vy = 0;
  burst(PLAYER_X, py, C_HEART, 10);
  ledFlash(1, 0, 0, 300);
  if (lives <= 0) {
    gameOver();
  } else {
    playSound(SND(SND_HIT), 3);
    addText("Ouch!", PLAYER_X + 35, py - 20, C_WHITE);
  }
}

bool spawnPickup(PkType type, float x, float y) {
  for (int i = 0; i < MAX_PK; i++) {
    if (pks[i].on) continue;
    pks[i].on = true;
    pks[i].type = type;
    pks[i].x = x;
    pks[i].y = y;
    pks[i].phase = frand(0, 6.28f);
    return true;
  }
  return false;
}

void spawnObstacle(int gap) {
  int slot = -1;
  for (int i = 0; i < MAX_OBS; i++) if (!obs[i].on) { slot = i; break; }
  if (slot < 0) return;
  Obstacle& o = obs[slot];
  o.on = true; o.passed = false; o.phase = frand(0, 6.28f); o.spd = 0; o.y = 0; o.col = 0;

  // in shallow water everything is scaled down so there's still room to swim
  int surf = surfaceY();
  float depth = (float)(SAND_Y - surf) / SAND_Y;
  int r = random(100);
  if (r < 30) {
    o.type = OB_ROCK; o.w = random(34, 53); o.h = (int)(random(28, 71) * depth); o.x = W + 10;
    if (o.h < 22) o.h = 22;
  } else if (r < 55) {
    o.type = OB_WEED; o.w = 16; o.h = (int)(random(60, 116) * depth); o.x = W + 20;
  } else if (r < 82 || levelScore < 4) {
    o.type = OB_FISH; o.w = 24; o.h = 14; o.x = W + 24;
    float fishTop = surf > 0 ? surf + 14.0f : 28.0f;
    o.y = constrain(py + frand(-35, 35), fishTop, (float)(SAND_Y - 24));  // fish aim near you
    o.spd = frand(12, 40) * MODES[mode].fishSpeed;
    o.col = FISH_COLORS[random(3)];
  } else {
    o.type = OB_ROCK_TOP; o.w = random(34, 51); o.x = W + 10;
    o.h = surf > 0 ? random(28, 50) : random(30, 66);   // floating log in shallow water, rock otherwise
    o.y = surf;
  }

  // in the open water after the obstacle: a treat, or a little stack of coins
  int what = random(100);
  if (what >= 55 && what < 85) {
    float cy0 = frand(surf > 0 ? surf + 18 : 50, SAND_Y - 74);
    for (int k = 0; k < 3; k++) spawnPickup(PK_COIN, o.x + gap / 2.0f, cy0 + k * 18);
  } else if (what < 55) {
    for (int i = 0; i < MAX_PK; i++) {
      if (pks[i].on) continue;
      Pickup& p = pks[i];
      p.on = true;
      p.x = o.x + gap / 2.0f;
      p.y = frand(surf > 0 ? surf + 16 : 35, SAND_Y - 30);
      p.phase = frand(0, 6.28f);
      if (lives < maxLives() && random(100) < 12) {
        p.type = PK_HEART;
      } else if (random(100) < 50) {
        p.type = PK_WORM;
        if (--wormsUntilTrophy <= 0) {          // a rare golden trophy!
          p.type = PK_TROPHY;
          wormsUntilTrophy = random(TROPHY_MIN_WORMS, TROPHY_MAX_WORMS + 1);
        }
      } else {
        p.type = PK_BUBBLE;
      }
      break;
    }
  }
}

void obHitbox(const Obstacle& o, int& x, int& y, int& w, int& h) {
  switch (o.type) {
    case OB_ROCK:     x = (int)o.x + 3; y = SAND_Y - o.h + 3; w = o.w - 6; h = o.h; break;
    case OB_ROCK_TOP: x = (int)o.x + 3; y = (int)o.y; w = o.w - 6; h = o.h - 3; break;
    case OB_WEED:     x = (int)o.x - 6; y = SAND_Y - o.h + 4; w = 14; h = o.h; break;
    default:          x = (int)o.x - 10; y = (int)o.y - 5; w = 22; h = 10; break;
  }
}

void collectOne(Pickup& p) {
  switch (p.type) {
    case PK_BUBBLE:
      P().bubblesPopped++;
      burst(p.x, p.y, C_BUBBLE, 8);
      playSound(SND(SND_BUBBLE), 1);
      ledFlash(0, 1, 1, 120);
      addText("+2", p.x, p.y - 15, C_BUBBLE);
      addScore(BUBBLE_POINTS);
      break;
    case PK_WORM:
      P().wormsEaten++;
      burst(p.x, p.y, C_WORM, 8);
      playSound(SND(SND_WORM), 1);
      ledFlash(1, 1, 0, 150);
      addText("+5", p.x, p.y - 15, C_GOLD);
      addScore(WORM_POINTS);
      break;
    case PK_TROPHY:
      P().trophiesFound++;
      burst(p.x, p.y, C_TROPHY, 24);
      playSound(SND(SND_TROPHY), 3);
      ledFlash(1, 1, 0, 700);
      addText("+20!", p.x, p.y - 18, C_TROPHY);
      addScore(TROPHY_POINTS);
      break;
    case PK_COIN:
      if (P().coins < 999999) P().coins++;
      P().coinsCollected++;
      burst(p.x, p.y, C_GOLD, 5);
      playSound(SND(SND_COIN), 1);
      ledFlash(1, 1, 0, 80);
      break;
    case PK_HEART:
      if (lives < maxLives()) lives++;
      burst(p.x, p.y, C_HEART, 12);
      addText("+1 Heart!", p.x, p.y - 15, C_HEART);
      playSound(SND(SND_HEART), 2);
      ledFlash(1, 0, 1, 400);
      break;
  }
}

void collect(Pickup& p) {
  p.on = false;
  collectOne(p);
  if (state == ST_PLAY) checkStickers();
}


void openSkins(State returnTo) {
  skinReturn = returnTo;
  browse = 0;                                     // start on the skin you're wearing
  for (int k = 0; k < N_SKINS; k++) if (SKIN_ORDER[k] == P().skin) browse = k;
  state = ST_SKINS;
  lockInput(250);
}

// ============================================================
//   SCREEN: "Who's playing?"
// ============================================================
void profileSlotRect(int k, int& x, int& y) { x = 10 + (k % 2) * 155; y = 44 + (k / 2) * 48; }

void updateProfiles(float dt) {
  scrollX += 35 * dt;
  if (!tPressed) return;
  int k = 0;
  for (int i = 0; i < MAX_PROFILES; i++) {
    if (!profiles[i].used) continue;
    int x, y;
    profileSlotRect(k++, x, y);
    if (inRect(tx, ty, x, y, 145, 42)) {
      if (deleteMode) {
        confirmIdx = i;
        state = ST_CONFIRM;
        playSound(SND(SND_CLICK));
      } else {
        cur = i;
        prefs.putUChar("last", i);
        state = ST_TITLE;
        playSound(SND(SND_SELECT));
      }
      lockInput(250);
      return;
    }
  }
  if (inRect(tx, ty, 10, 194, 145, 40)) {
    if (profileCount() < MAX_PROFILES) {
      nameLen = 0; nameBuf[0] = 0;
      deleteMode = false;
      state = ST_NAME;
      playSound(SND(SND_CLICK));
      lockInput(250);
    } else {
      playSound(SND(SND_LOCKED));
    }
  } else if (inRect(tx, ty, 165, 194, 145, 40)) {
    deleteMode = !deleteMode;
    playSound(SND(SND_CLICK));
  }
}

void drawProfiles() {
  drawBackground();
  shadowText(deleteMode ? "Tap a player to delete" : "Who's playing?", W / 2, 22, 4, C_GOLD);
  int k = 0;
  char buf[16];
  for (int i = 0; i < MAX_PROFILES; i++) {
    Profile& p = profiles[i];
    if (!p.used) continue;
    int x, y;
    profileSlotRect(k++, x, y);
    drawButtonBase(x, y, 145, 42, deleteMode ? C_RED_BTN : C_BLUE_BTN);
    drawCharacter(p.character, p.skin, profHat(p), x + 27, y + 26, 1, gameT + i);
    textAt(p.name, x + 52, y + 14, fitFont(p.name, 88), C_WHITE, ML_DATUM);
    snprintf(buf, sizeof(buf), "Level %d", p.level);
    textAt(buf, x + 52, y + 33, 2, C_GOLD, ML_DATUM);
  }
  drawButton(10, 194, 145, 40, "+ New", profileCount() < MAX_PROFILES ? C_GREEN_BTN : C_GREY_BTN);
  drawButton(165, 194, 145, 40, deleteMode ? "Done" : "Delete", deleteMode ? C_GREY_BTN : C_RED_BTN);
}

// ============================================================
//   SCREEN: type a name
// ============================================================
const char* KB_ROWS[3] = {"ABCDEFGHI", "JKLMNOPQR", "STUVWXYZ"};

void keyRect(int r, int c, int& x, int& y) {
  int n = strlen(KB_ROWS[r]);
  x = 8 + (9 - n) * 17 + c * 34;
  y = 54 + r * 38;
}

void updateName(float dt) {
  scrollX += 20 * dt;
  if (!tPressed) return;
  for (int r = 0; r < 3; r++) {
    int n = strlen(KB_ROWS[r]);
    for (int c = 0; c < n; c++) {
      int x, y;
      keyRect(r, c, x, y);
      if (inRect(tx, ty, x, y, 32, 34)) {
        if (nameLen < NAME_LEN) {
          char ch = KB_ROWS[r][c];
          if (nameLen > 0) ch = tolower(ch);    // "ALICE" -> "Alice"
          nameBuf[nameLen++] = ch;
          nameBuf[nameLen] = 0;
          playSound(SND(SND_KEY));
        } else {
          playSound(SND(SND_LOCKED));
        }
        return;
      }
    }
  }
  bool canBack = profileCount() > 0;
  if (inRect(tx, ty, 5, 176, 100, 48)) {
    if (canBack) { state = ST_PROFILES; playSound(SND(SND_CLICK)); lockInput(250); }
  } else if (inRect(tx, ty, 110, 176, 100, 48)) {
    if (nameLen > 0) { nameBuf[--nameLen] = 0; playSound(SND(SND_DELETE)); }
  } else if (inRect(tx, ty, 215, 176, 100, 48)) {
    if (nameLen == 0) { playSound(SND(SND_LOCKED)); return; }
    for (int i = 0; i < MAX_PROFILES; i++) {
      if (profiles[i].used) continue;
      Profile& p = profiles[i];
      memset(&p, 0, sizeof(Profile));
      p.used = 1;
      strncpy(p.name, nameBuf, NAME_LEN);
      p.skin = 0;
      p.unlocked = 1;   // (no longer used, kept for old saves)
      p.seen = 1;
      p.level = 1;
      p.best = 0;
      saveProfile(i);
      cur = i;
      prefs.putUChar("last", i);
      state = ST_TITLE;
      playSound(SND(SND_START), 2);
      lockInput(250);
      return;
    }
  }
}

void drawName() {
  drawBackground();
  drawPanel(10, 6, 300, 40);
  if (nameLen == 0) {
    textAt("Type your name", W / 2, 26, 4, RGB(150, 160, 200), MC_DATUM);
  } else {
    char buf[NAME_LEN + 2];
    snprintf(buf, sizeof(buf), "%s%s", nameBuf, ((int)(gameT * 2)) % 2 ? "_" : " ");
    textAt(buf, W / 2, 26, 4, C_WHITE, MC_DATUM);
  }
  for (int r = 0; r < 3; r++) {
    int n = strlen(KB_ROWS[r]);
    for (int c = 0; c < n; c++) {
      int x, y;
      keyRect(r, c, x, y);
      fb.fillRoundRect(x, y + 2, 32, 34, 6, C_BTN_SHADOW);
      fb.fillRoundRect(x, y, 32, 34, 6, C_BLUE_BTN);
      fb.drawRoundRect(x, y, 32, 34, 6, C_WHITE);
      char k[2] = {KB_ROWS[r][c], 0};
      fb.setTextDatum(MC_DATUM);
      fb.setTextColor(C_WHITE);
      fb.drawString(k, x + 16, y + 18, 4);
    }
  }
  drawButton(5, 176, 100, 48, "Back", profileCount() > 0 ? C_GREY_BTN : RGB(60, 60, 75));
  drawButton(110, 176, 100, 48, "Erase", C_ORANGE_BTN);
  drawButton(215, 176, 100, 48, "Done", nameLen > 0 ? C_GREEN_BTN : C_GREY_BTN);
}

// ============================================================
//   SCREEN: confirm delete
// ============================================================
void updateConfirm() {
  if (tapped(55, 150, 100, 42)) {
    profiles[confirmIdx].used = 0;
    saveProfile(confirmIdx);
    if (cur == confirmIdx) cur = -1;
    playSound(SND(SND_DELETE));
    if (profileCount() == 0) { deleteMode = false; nameLen = 0; nameBuf[0] = 0; state = ST_NAME; }
    else state = ST_PROFILES;
    lockInput(250);
  } else if (tapped(165, 150, 100, 42)) {
    playSound(SND(SND_CLICK));
    state = ST_PROFILES;
    lockInput(250);
  }
}

void drawConfirm() {
  drawBackground();
  drawPanel(40, 50, 240, 150);
  char buf[32];
  snprintf(buf, sizeof(buf), "Delete %s?", profiles[confirmIdx].name);
  shadowText(buf, W / 2, 80, 4, C_WHITE);
  shadowText("Their progress will be gone.", W / 2, 116, 2, C_GOLD);
  drawButton(55, 150, 100, 42, "Yes", C_RED_BTN);
  drawButton(165, 150, 100, 42, "No", C_GREEN_BTN);
}

// ============================================================
//   SCREEN: title / main menu
// ============================================================
void openShop(State returnTo) {
  shopReturn = returnTo;
  buyArmed = 0;
  shopMsgT = 0;
  state = ST_SHOP;
  lockInput(250);
}

void updateTitle(float dt) {
  scrollX += 35 * dt;
  if (!tPressed) return;
  if (inRect(tx, ty, 95, 122, 130, 44)) {
    playSound(SND(SND_CLICK));
    startRun();
  } else if (inRect(tx, ty, 5, 122, 85, 44)) {
    playSound(SND(SND_CLICK));
    openShop(ST_TITLE);
  } else if (inRect(tx, ty, 230, 122, 85, 44)) {
    playSound(SND(SND_CLICK));
    state = ST_SCORES;
    lockInput(250);
  } else if (inRect(tx, ty, 4, 178, 75, 48)) {
    playSound(SND(SND_CLICK));
    openSkins(ST_TITLE);
  } else if (inRect(tx, ty, 83, 178, 75, 48)) {
    playSound(SND(SND_CLICK));
    state = ST_STICKERS;
    lockInput(250);
  } else if (inRect(tx, ty, 162, 178, 75, 48)) {
    playSound(SND(SND_CLICK));
    state = ST_SETTINGS;
    lockInput(250);
  } else if (inRect(tx, ty, 241, 178, 75, 48)) {
    playSound(SND(SND_CLICK));
    deleteMode = false;
    state = ST_PROFILES;
    lockInput(250);
  } else if (inRect(tx, ty, 60, 56, 100, 60)) {
    wiggleT = 0.6f;
    playSound(SND(SND_SQUEAK));
  }
}

void drawTitle() {
  drawBackground();
  // title with the version number in small print next to it
  const char* title = "Axolotl Adventure";
  const char* ver = "v" GAME_VERSION;
  int tw1 = fb.textWidth(title, 4), tw2 = fb.textWidth(ver, 2);
  int tx0 = W / 2 - (tw1 + 5 + tw2) / 2;
  fb.setTextDatum(ML_DATUM);
  fb.setTextColor(C_BLACK);
  fb.drawString(title, tx0 + 2, 22, 4);
  fb.setTextColor(C_GOLD);
  fb.drawString(title, tx0, 20, 4);
  textAt(ver, tx0 + tw1 + 5, 24, 2, C_WHITE, ML_DATUM);
  shadowText("by AliceFriend", W / 2, 42, 2, C_WHITE);
  int wig = wiggleT > 0 ? (int)(sinf(gameT * 30) * 4) : 0;
  drawCharacter(P().character, P().skin, profHat(P()), 110, 92 + (int)(sinf(gameT * 2) * 3) + wig, 2, gameT);

  char buf[24];
  textAt(P().name, 244, 64, fitFont(P().name, 140), C_WHITE, MC_DATUM);
  snprintf(buf, sizeof(buf), "Level %d   Best %lu", P().level, (unsigned long)P().best);
  textAt(buf, 244, 86, 2, C_GOLD, MC_DATUM);
  snprintf(buf, sizeof(buf), "%lu coins", (unsigned long)P().coins);
  int tw = fb.textWidth(buf, 2);
  drawCoinIcon(244 - tw / 2 - 10, 104, 6);
  textAt(buf, 244 + 4, 104, 2, C_GOLD, MC_DATUM);

  drawButton(5, 122, 85, 44, "Shop", C_ORANGE_BTN);
  drawButton(95, 122, 130, 44, "PLAY", C_GREEN_BTN);
  drawButton(230, 122, 85, 44, "Scores", C_TEAL_BTN);
  drawButton2(4, 178, 75, 48, "SKIN", SKINS[P().skin].name, C_PURPLE_BTN);
  if (hasNewSkins()) drawNewBadge(44, 170);
  char sb[12];
  snprintf(sb, sizeof(sb), "%d/%d", countBits(P().stickers), N_STICKERS);
  drawButton2(83, 178, 75, 48, "STICKERS", sb, RGB(200, 100, 30));
  drawButton2(162, 178, 75, 48, "SETTINGS", MODES[mode].name, C_BLUE_BTN);
  drawButton2(241, 178, 75, 48, "PLAYER", P().name, C_TEAL_BTN);
}

// ============================================================
//   SCREEN: skins
// ============================================================
void updateSkins(float dt) {
  if (skinReturn == ST_TITLE) scrollX += 20 * dt;
  if (!tPressed) return;
  int si = SKIN_ORDER[browse];
  bool un = skinUnlocked(si);
  if (inRect(tx, ty, 8, 72, 50, 56)) {
    browse = (browse + N_SKINS - 1) % N_SKINS;
    playSound(SND(SND_CLICK));
  } else if (inRect(tx, ty, 262, 72, 50, 56)) {
    browse = (browse + 1) % N_SKINS;
    playSound(SND(SND_CLICK));
  } else if (inRect(tx, ty, 90, 55, 140, 80)) {
    if (un) { wiggleT = 0.6f; playSound(SND(SND_SQUEAK)); }
    else playSound(SND(SND_LOCKED));
  } else if (inRect(tx, ty, 110, 190, 100, 40)) {
    if (un) {
      P().skin = si;
      P().character = CH_AXOLOTL;    // skins are for the axolotl
      saveProfile(cur);
      playSound(SND(SND_SELECT));
    } else {
      playSound(SND(SND_CLICK));
    }
    if (P().seen < P().level) { P().seen = P().level; saveProfile(cur); }   // "NEW!" badges cleared
    state = skinReturn;
    lockInput(250);
  }
}

void drawSkins() {
  drawBackground();
  shadowText("Pick Your Axolotl", W / 2, 22, 4, C_GOLD);
  int si = SKIN_ORDER[browse];
  const Skin& sk = SKINS[si];
  bool un = skinUnlocked(si);
  int wig = wiggleT > 0 ? (int)(sinf(gameT * 30) * 5) : 0;
  drawAxolotl(160, 100 + wig, 3, gameT, sk, !un);

  if (!un) {  // padlock
    for (int r = 6; r <= 9; r++) fb.drawCircle(160, 94, r, C_GOLD);
    fb.fillRoundRect(147, 96, 26, 20, 4, C_GOLD);
    fb.fillCircle(160, 104, 3, C_BLACK);
    fb.fillRect(159, 104, 3, 7, C_BLACK);
  }

  drawArrowButton(8, 72, 50, 56, true, C_BLUE_BTN);
  drawArrowButton(262, 72, 50, 56, false, C_BLUE_BTN);
  shadowText(sk.name, W / 2, 150, 4, C_WHITE);
  if (un && isNewSkin(si)) drawNewBadge(212, 52);

  char buf[32];
  if (!un) {
    if (isShopOnly(si))     snprintf(buf, sizeof(buf), "Buy it in the Shop!");
    else if (sk.price > 0)  snprintf(buf, sizeof(buf), "Finish level %d, or buy in the Shop!", sk.lvl);
    else                    snprintf(buf, sizeof(buf), "Finish level %d to unlock!", sk.lvl);
    shadowText(buf, W / 2, 174, 2, C_GOLD);
  } else if (si == P().skin && P().character == CH_AXOLOTL) {
    shadowText("This is you!", W / 2, 174, 2, C_GOLD);
  } else {
    shadowText("Tap OK to choose", W / 2, 174, 2, C_WHITE);
  }
  snprintf(buf, sizeof(buf), "%d/%d", browse + 1, N_SKINS);
  textAt(buf, W - 8, 225, 2, C_WHITE, MR_DATUM);
  drawButton(110, 190, 100, 40, un ? "OK" : "Back", un ? C_GREEN_BTN : C_GREY_BTN);
}

// ============================================================
//   SCREEN: leaderboard
// ============================================================
void updateScores(float dt) {
  scrollX += 20 * dt;
  if (tapped(110, 194, 100, 40)) {
    playSound(SND(SND_CLICK));
    state = ST_TITLE;
    lockInput(250);
  }
}

void drawScores() {
  drawBackground();
  shadowText("Top Swimmers", W / 2, 18, 4, C_GOLD);
  drawPanel(8, 34, 304, 154);
  if (scores[0].score == 0) {
    shadowText("No scores yet.", W / 2, 96, 4, C_WHITE);
    shadowText("Go for a swim!", W / 2, 126, 2, C_GOLD);
  }
  char buf[24];
  for (int i = 0; i < MAX_SCORES; i++) {
    ScoreEntry& e = scores[i];
    if (e.score == 0) break;
    int y = 46 + i * 14;
    bool me = cur >= 0 && strcmp(e.name, P().name) == 0;
    uint16_t c = me ? C_GOLD : C_WHITE;
    if (i == 0) drawStar(26, y, 6, C_GOLD);
    else { snprintf(buf, sizeof(buf), "%d", i + 1); textAt(buf, 26, y, 2, c, MC_DATUM); }
    textAt(e.name, 44, y, 2, c, ML_DATUM);
    snprintf(buf, sizeof(buf), "Lv %d", e.level);
    textAt(buf, 200, y, 2, c, MC_DATUM);
    snprintf(buf, sizeof(buf), "%lu", (unsigned long)e.score);
    textAt(buf, 296, y, 2, c, MR_DATUM);
  }
  drawButton(110, 194, 100, 40, "Back", C_BLUE_BTN);
}

// ============================================================
//   SCREEN: countdown
// ============================================================
void updateReady(float dt) {
  readyT += dt;
  int c = (int)(readyT / 0.55f);
  if (c != readyCount) {
    readyCount = c;
    if (c < 3) playSound(SND(SND_BEEP), 2);
    else if (c == 3) playSound(SND(SND_GO), 2);
  }
  if (readyT > 0.55f * 4) state = ST_PLAY;
}

void drawReady() {
  drawGame();
  drawHUD(false);
  for (int x = 0; x < W; x += 16) fb.drawFastHLine(x, H / 2, 8, C_WHITE);
  fb.fillTriangle(285, 40, 272, 58, 298, 58, C_WHITE);
  shadowText("Swim up", 285, 72, 2, C_WHITE);
  shadowText("Swim down", 280, 168, 2, C_WHITE);
  fb.fillTriangle(285, 200, 272, 182, 298, 182, C_WHITE);

  char buf[24];
  snprintf(buf, sizeof(buf), "Level %d", curLevel);
  shadowText(buf, 160, 50, 4, C_WHITE);
  shadowText(THEMES[currentTheme()].name, 160, 72, 2, C_WHITE);
  snprintf(buf, sizeof(buf), "Get %d points!", levelGoal(curLevel));
  shadowText(buf, 160, 150, 2, C_GOLD);
  if (levelHelper >= 0) {
    snprintf(buf, sizeof(buf), "Helper: %s", FRIENDS[levelHelper].name);
    shadowText(buf, 160, 168, 2, C_WHITE);
  }

  if (readyCount < 3) {
    snprintf(buf, sizeof(buf), "%d", 3 - readyCount);
    fb.setTextDatum(MC_DATUM);
    fb.setTextColor(C_BLACK);
    fb.drawString(buf, 162, 108, 7);
    fb.setTextColor(C_GOLD);
    fb.drawString(buf, 160, 106, 7);
  } else {
    shadowText("Swim!", 160, 106, 4, C_GOLD);
  }
}

// ============================================================
//   SCREEN: playing
// ============================================================
void updatePlay(float dt) {
  const Mode& M = MODES[mode];
  bool onPause = inRect(tx, ty, W - 38, 0, 38, 38);
  bool onShield = showShieldButton() && inRect(tx, ty, W - 74, 0, 36, 38);
  if (tPressed && onPause) {
    state = ST_PAUSE;
    quitArmed = false;
    playSound(SND(SND_CLICK));
    lockInput(250);
    return;
  }
  if (tPressed && onShield && shieldT <= 0 && P().shields > 0) {   // use a shield
    P().shields--;
    shieldT = shieldTime();
    playSound(SND(SND_SHIELD), 3);
    ledFlash(0, 0, 1, 300);
    addText("Shield!", PLAYER_X + 30, py - 24, C_BUBBLE);
  }

  // --- controls: top half = up, bottom half = down ---
  if (tDown && !onPause && !onShield) {
    bool up = ty < H / 2;
    if (tPressed) {
      vy = up ? -IMPULSE : IMPULSE;
      if (up) playSound(SND(SND_UP), 0);
      else    playSound(SND(SND_DOWN), 0);
    }
    vy += (up ? -SWIM_ACC : SWIM_ACC) * dt;
    if (random(100) < 20) {        // little bubble trail from the tail
      for (int i = 0; i < MAX_SPARK; i++) {
        if (sparks[i].on) continue;
        sparks[i] = {true, (float)PLAYER_X - 22, py + frand(-3, 3), frand(-30, -10), frand(-70, -40), 0.6f, C_BUBBLE};
        break;
      }
    }
  } else {
    vy -= vy * fminf(1.0f, 3.0f * dt);   // gently glide to a stop
  }
  vy = constrain(vy, -MAX_VY, MAX_VY);
  py += vy * dt;
  int topLimit = surfaceY() > 0 ? surfaceY() + 10 : 18;   // can't swim up into the sky
  if (py < topLimit)    { py = topLimit;    if (vy < 0) vy = 0; }
  if (py > SAND_Y - 12) { py = SAND_Y - 12; if (vy > 0) vy = 0; }

  // --- world scroll & spawning (speed stays the same on every level) ---
  speed = M.speed;
  float dx = speed * dt;
  scrollX += dx;
  spawnDist -= dx;
  if (spawnDist <= 0) {
    int gap = random(M.gapMin, M.gapMax + 1);
    spawnObstacle(gap);
    spawnDist = gap;
  }

  // forgiving hitbox (smaller than the drawing). The shrimp is extra tiny.
  int hx = PLAYER_X - 10, hy = (int)py - 6, hw = 26, hh = 13;
  if (playerChar() == CH_SHRIMP) { hx = PLAYER_X - 7; hy = (int)py - 4; hw = 18; hh = 9; }
  float helpX = 0, helpY = 0;
  if (levelHelper >= 0) helperPos(helpX, helpY);

  for (int i = 0; i < MAX_OBS; i++) {
    Obstacle& o = obs[i];
    if (!o.on) continue;
    o.x -= dx;
    if (o.type == OB_FISH) {
      o.x -= o.spd * dt;
      o.y += cosf(gameT * 2.5f + o.phase) * 14 * dt;
    }
    int bx, by, bw, bh;
    obHitbox(o, bx, by, bw, bh);
    if (!o.passed && bx + bw < hx) {
      o.passed = true;
      playSound(SND(SND_PASS), 0);
      addScore(PASS_POINTS);
      if (state != ST_PLAY) return;
    }
    if (bx + bw < -30) { o.on = false; continue; }
    if (invuln <= 0 && shieldT <= 0 && rectsOverlap(hx, hy, hw, hh, bx, by, bw, bh)) {
      hurt();
      if (state != ST_PLAY) return;
    }
  }

  for (int i = 0; i < MAX_PK; i++) {
    Pickup& p = pks[i];
    if (!p.on) continue;
    p.x -= dx;
    if (p.x < -20) { p.on = false; continue; }
    float ddx = p.x - (PLAYER_X + 4), ddy = p.y - py;
    float d2 = ddx * ddx + ddy * ddy;
    if (playerChar() == CH_SEAHORSE && d2 < 70 * 70) {   // seahorse magnet
      p.x -= ddx * 3.0f * dt;
      p.y -= ddy * 3.0f * dt;
    }
    bool helperGot = false;
    if (levelHelper >= 0) {
      float hdx = p.x - helpX, hdy = p.y - helpY;
      helperGot = hdx * hdx + hdy * hdy < 14 * 14;
    }
    if (d2 < 18 * 18 || helperGot) {
      collect(p);
      if (state != ST_PLAY) return;
    }
  }

  if (invuln > 0) invuln -= dt;
  if (shieldT > 0) shieldT -= dt;
}

// ============================================================
//   SCREEN: pause menu
// ============================================================
void updatePause() {
  if (!tPressed) return;
  if (inRect(tx, ty, 70, 52, 180, 42)) {
    playSound(SND(SND_CLICK));
    goReady();
  } else if (inRect(tx, ty, 50, 104, 70, 48)) {
    playSound(SND(SND_CLICK));
    openSkins(ST_PAUSE);
  } else if (inRect(tx, ty, 125, 104, 70, 48)) {
    volume = (volume + 1) % 3;
    prefs.putUChar("vol", volume);
    playSound(SND(SND_SELECT));
  } else if (inRect(tx, ty, 200, 104, 70, 48)) {
    musicOn = !musicOn;
    prefs.putUChar("mus", musicOn);
    playSound(SND(SND_SELECT));
  } else if (inRect(tx, ty, 70, 166, 180, 42)) {
    if (!quitArmed) {              // first tap: ask to make sure
      quitArmed = true;
      playSound(SND(SND_LOCKED));
      return;
    }
    playSound(SND(SND_CLICK));
    endRun();
    state = ST_TITLE;
    lockInput(250);
  } else {
    quitArmed = false;
  }
}

void drawPause() {
  drawGame();
  drawHUD(false);
  drawPanel(40, 14, 240, 212);
  shadowText("Paused", W / 2, 33, 4, C_GOLD);
  drawButton(70, 52, 180, 42, "Keep Swimming", C_GREEN_BTN);
  drawButton2(50, 104, 70, 48, "SKIN", SKINS[P().skin].name, C_PURPLE_BTN);
  if (hasNewSkins()) drawNewBadge(88, 96);
  drawButton2(125, 104, 70, 48, "SOUND", VOL_NAMES[volume], C_BLUE_BTN);
  drawButton2(200, 104, 70, 48, "MUSIC", musicOn ? "On" : "Off", C_TEAL_BTN);
  drawButton(70, 166, 180, 42, quitArmed ? "Tap again to quit" : "Quit to Menu", quitArmed ? C_RED_BTN : C_ORANGE_BTN);
}

// ============================================================
//   SCREEN: level complete
// ============================================================
void updateClear(float dt) {
  overT += dt;
  if (overT < 3.0f && random(100) < 30)
    burst(frand(50, 270), frand(40, 110), pastelHue(frand(0, 1)), 5);   // confetti!
  if (!tPressed) return;
  if (inRect(tx, ty, 49, 168, 70, 40)) {
    playSound(SND(SND_CLICK));
    curLevel++;
    startLevel();          // run score carries on
  } else if (inRect(tx, ty, 127, 168, 70, 40)) {
    playSound(SND(SND_CLICK));
    openShop(ST_CLEAR);    // spend coins between levels
  } else if (inRect(tx, ty, 205, 168, 70, 40)) {
    playSound(SND(SND_CLICK));
    endRun();
    state = ST_TITLE;
    lockInput(250);
  }
}

void drawClear() {
  drawGame();
  drawPanel(40, 30, 240, 185);
  char buf[32];
  snprintf(buf, sizeof(buf), "Level %d", curLevel);
  shadowText(buf, W / 2, 48, 4, C_GOLD);
  shadowText("Complete!", W / 2, 72, 4, C_WHITE);
  int yInfo = 128;
  if (newSkinName) {
    drawStar(88, 112, 10, C_GOLD);
    drawStar(232, 112, 10, C_GOLD);
    drawAxolotl(160, 116, 2, overT * 2, SKINS[newSkinIdx]);   // show off the new skin
    yInfo = 140;
    snprintf(buf, sizeof(buf), "New skin: %s!", newSkinName);
    shadowText(buf, W / 2, yInfo, 2, C_GOLD);
  } else {
    for (int k = 0; k < 3; k++) drawStar(120 + k * 40, 102, k == 1 ? 13 : 10, C_GOLD);
    snprintf(buf, sizeof(buf), "Score: %d", runScore);
    shadowText(buf, W / 2, yInfo, 2, C_WHITE);
  }
  if (perfectLevel) snprintf(buf, sizeof(buf), "Perfect! +%d coins", coinBonus);
  else              snprintf(buf, sizeof(buf), "+%d coins!", coinBonus);
  int tw = fb.textWidth(buf, 2);
  drawCoinIcon(W / 2 - tw / 2 - 9, yInfo + 15, 5);
  shadowText(buf, W / 2 + 3, yInfo + 15, 2, C_GOLD);
  drawButton(49, 168, 70, 40, "Next", C_GREEN_BTN);
  drawButton(127, 168, 70, 40, "Shop", C_ORANGE_BTN);
  drawButton(205, 168, 70, 40, "Menu", C_BLUE_BTN);
}

// ============================================================
//   SCREEN: revive ("keep swimming for 25 coins?")
// ============================================================
void updateRevive() {
  if (tapped(55, 150, 100, 44)) {
    P().coins -= REVIVE_PRICE;
    revivedThisLevel = true;
    lives = 2;
    invuln = 2.5f;              // a moment of safety to get going again
    burst(PLAYER_X, py, C_HEART, 14);
    playSound(SND(SND_HEART), 4);
    ledFlash(1, 0, 1, 400);
    goReady();
  } else if (tapped(165, 150, 100, 44)) {
    playSound(SND(SND_CLICK));
    finishGameOver();
  }
}

void drawPriceLine(int price, int y);

void drawRevive() {
  drawGame();
  drawPanel(40, 46, 240, 156);
  shadowText("Out of hearts!", W / 2, 70, 4, C_WHITE);
  shadowText("Keep swimming for", W / 2, 98, 2, C_WHITE);
  drawPriceLine(REVIVE_PRICE, 116);
  drawButton(55, 150, 100, 44, "Yes!", C_GREEN_BTN);
  drawButton(165, 150, 100, 44, "No thanks", C_GREY_BTN);
}

// ============================================================
//   SCREEN: settings
// ============================================================
void updateSettings(float dt) {
  scrollX += 20 * dt;
  if (!tPressed) return;
  if (inRect(tx, ty, 20, 50, 135, 56)) {
    mode = (mode + 1) % 3;
    prefs.putUChar("mode", mode);
    playSound(SND(SND_CLICK));
  } else if (inRect(tx, ty, 165, 50, 135, 56)) {
    volume = (volume + 1) % 3;
    prefs.putUChar("vol", volume);
    playSound(SND(SND_SELECT));
  } else if (inRect(tx, ty, 20, 116, 135, 56)) {
    musicOn = !musicOn;
    prefs.putUChar("mus", musicOn);
    playSound(SND(SND_SELECT));
  } else if (inRect(tx, ty, 165, 116, 135, 56)) {
    brightness = (brightness + 1) % 3;
    prefs.putUChar("bri", brightness);
    ledcWrite(BL_CH, BRIGHT_LEVELS[brightness]);
    playSound(SND(SND_CLICK));
  } else if (inRect(tx, ty, 110, 190, 100, 40)) {
    playSound(SND(SND_CLICK));
    state = ST_TITLE;
    lockInput(250);
  }
}

void drawSettings() {
  drawBackground();
  shadowText("Settings", W / 2, 22, 4, C_GOLD);
  drawButton2(20, 50, 135, 56, "SPEED", MODES[mode].name, RGB(200, 100, 30));
  drawButton2(165, 50, 135, 56, "SOUND", VOL_NAMES[volume], C_BLUE_BTN);
  drawButton2(20, 116, 135, 56, "MUSIC", musicOn ? "On" : "Off", C_TEAL_BTN);
  drawButton2(165, 116, 135, 56, "BRIGHTNESS", BRIGHT_NAMES[brightness], C_PURPLE_BTN);
  drawButton(110, 190, 100, 40, "Back", C_GREEN_BTN);
  textAt("v" GAME_VERSION, W - 8, 226, 2, C_WHITE, MR_DATUM);
}

// ============================================================
//   SCREEN: stickers
// ============================================================
void stickerCell(int i, int& cx, int& cy) { cx = 28 + (i % 7) * 44; cy = 62 + (i / 7) * 50; }

void updateStickers(float dt) {
  scrollX += 20 * dt;
  if (!tPressed) return;
  for (int i = 0; i < N_STICKERS; i++) {
    int cx, cy;
    stickerCell(i, cx, cy);
    if (inRect(tx, ty, cx - 22, cy - 24, 44, 48)) { stickerSel = i; playSound(SND(SND_CLICK)); return; }
  }
  if (inRect(tx, ty, 110, 196, 100, 38)) {
    playSound(SND(SND_CLICK));
    state = ST_TITLE;
    lockInput(250);
  }
}

void drawStickers() {
  drawBackground();
  shadowText("Stickers", 70, 18, 4, C_GOLD);
  char buf[48];
  snprintf(buf, sizeof(buf), "%d of %d", countBits(P().stickers), N_STICKERS);
  textAt(buf, W - 10, 18, 2, C_WHITE, MR_DATUM);
  for (int i = 0; i < N_STICKERS; i++) {
    int cx, cy;
    stickerCell(i, cx, cy);
    if (i == stickerSel) { fb.drawCircle(cx, cy, 21, C_GOLD); fb.drawCircle(cx, cy, 22, C_GOLD); }
    drawStickerBadge(cx, cy, 18, i, stickerEarned(i));
  }
  const Sticker& st = STICKERS[stickerSel];
  shadowText(st.name, W / 2, 146, 4, C_WHITE);
  shadowText(st.desc, W / 2, 168, 2, C_WHITE);
  if (stickerEarned(stickerSel)) {
    snprintf(buf, sizeof(buf), "Earned! (+%d coins)", st.reward);
  } else {
    uint32_t pr = stickerProgress(stickerSel);
    if (pr > st.target) pr = st.target;
    snprintf(buf, sizeof(buf), "%lu / %lu   Reward: %d coins", (unsigned long)pr, (unsigned long)st.target, st.reward);
  }
  shadowText(buf, W / 2, 184, 2, C_GOLD);
  drawButton(110, 196, 100, 38, "Back", C_BLUE_BTN);
}

// ============================================================
//   SCREEN: shop (main menu and between levels - not while paused)
// ============================================================
const char* TAB_NAMES[4] = {"Hats", "Skins", "Power", "Friends"};

int shopCount(int tab) {
  switch (tab) {
    case 0:  return N_HATS;
    case 1:  return N_SHOP_SKINS;
    case 2:  return 1;
    default: return N_FRIENDS + 1;   // the axolotl, then the friends
  }
}

void shopSay(const char* m) { shopMsg = m; shopMsgT = 1.8f; }

// Two taps to buy, so coins aren't spent by accident.
bool tryBuy(int btn, uint16_t price) {
  if (P().coins < price) {
    shopSay("Need more coins!");
    playSound(SND(SND_LOCKED));
    buyArmed = 0;
    return false;
  }
  if (buyArmed != btn) {
    buyArmed = btn;
    playSound(SND(SND_CLICK));
    return false;
  }
  buyArmed = 0;
  P().coins -= price;
  playSound(SND(SND_BUY), 3);
  ledFlash(1, 1, 0, 300);
  burst(160, 108, C_GOLD, 16);
  return true;
}

void shopAction(int btn) {
  Profile& p = P();
  int idx = shopIdx[shopTab];
  switch (shopTab) {
    case 0: {  // hats
      if (!hatOwned(idx)) {
        if (tryBuy(1, HATS[idx].price)) { p.hatsOwned |= (1UL << idx); p.hat = idx + 1; saveProfile(cur); shopSay("Looking good!"); }
      } else if (p.hat == idx + 1) {
        p.hat = 0; saveProfile(cur); playSound(SND(SND_CLICK));
      } else {
        p.hat = idx + 1; saveProfile(cur); playSound(SND(SND_SELECT));
      }
      break;
    }
    case 1: {  // shop skins
      int si = SHOP_SKIN_LIST[idx];
      if (!skinUnlocked(si)) {
        if (tryBuy(1, SKINS[si].price)) { p.skinsOwned |= (1UL << (si - FIRST_SHOP_SKIN)); p.skin = si; p.character = CH_AXOLOTL; saveProfile(cur); shopSay("So shiny!"); }
      } else {
        p.skin = si; p.character = CH_AXOLOTL; saveProfile(cur); playSound(SND(SND_SELECT));
      }
      break;
    }
    case 2:    // shields
      if (p.shields >= MAX_SHIELDS) { shopSay("Your pockets are full!"); playSound(SND(SND_LOCKED)); buyArmed = 0; }
      else if (tryBuy(1, SHIELD_PRICE)) { p.shields++; saveProfile(cur); shopSay("Shield ready!"); }
      break;
    default: { // friends
      if (idx == 0) { p.character = CH_AXOLOTL; saveProfile(cur); playSound(SND(SND_SELECT)); break; }
      int f = idx - 1;
      if (btn == 1) {
        if (p.helper) { shopSay("A helper is already coming!"); playSound(SND(SND_LOCKED)); buyArmed = 0; }
        else if (tryBuy(1, HELPER_PRICE)) { p.helper = f + 1; saveProfile(cur); shopSay("Helper joins next level!"); }
      } else if (!friendOwned(f)) {
        if (tryBuy(2, FRIENDS[f].price)) { p.friendsOwned |= (1 << f); p.character = f + 1; saveProfile(cur); shopSay("New friend!"); }
      } else {
        p.character = f + 1; saveProfile(cur); playSound(SND(SND_SELECT));
      }
      break;
    }
  }
}

void updateShop(float dt) {
  scrollX += 20 * dt;
  if (shopMsgT > 0) shopMsgT -= dt;
  if (!tPressed) return;
  for (int i = 0; i < 4; i++) {
    if (inRect(tx, ty, 4 + i * 79, 36, 75, 26)) {
      shopTab = i; buyArmed = 0; shopMsgT = 0;
      playSound(SND(SND_CLICK));
      return;
    }
  }
  int n = shopCount(shopTab);
  int& idx = shopIdx[shopTab];
  if (inRect(tx, ty, 6, 80, 44, 56))   { idx = (idx + n - 1) % n; buyArmed = 0; shopMsgT = 0; playSound(SND(SND_CLICK)); return; }
  if (inRect(tx, ty, 270, 80, 44, 56)) { idx = (idx + 1) % n;     buyArmed = 0; shopMsgT = 0; playSound(SND(SND_CLICK)); return; }
  if (inRect(tx, ty, 6, 190, 80, 42)) {
    playSound(SND(SND_CLICK));
    buyArmed = 0;
    state = shopReturn;
    lockInput(250);
    return;
  }
  int btn = 0;
  bool twoButtons = (shopTab == 3 && idx > 0);
  if (twoButtons) {
    if (inRect(tx, ty, 96, 190, 106, 42))       btn = 1;
    else if (inRect(tx, ty, 208, 190, 106, 42)) btn = 2;
  } else if (inRect(tx, ty, 96, 190, 218, 42)) {
    btn = 1;
  }
  if (btn) { shopAction(btn); checkStickers(); }   // e.g. "Own 5 hats"
  else buyArmed = 0;
}

int priceHintLevel = 0;   // when set, the price line adds "or finish level N"

void drawPriceLine(int price, int y) {
  char buf[40];
  if (priceHintLevel > 0) snprintf(buf, sizeof(buf), "%d coins  (or finish level %d)", price, priceHintLevel);
  else                    snprintf(buf, sizeof(buf), "%d coins", price);
  int tw = fb.textWidth(buf, 2);
  drawCoinIcon(W / 2 - tw / 2 - 9, y, 5);
  shadowText(buf, W / 2 + 3, y, 2, C_GOLD);
}

void drawShop() {
  drawBackground();
  Profile& p = P();
  shadowText("Shop", 44, 18, 4, C_GOLD);
  char buf[40];
  snprintf(buf, sizeof(buf), "%lu", (unsigned long)p.coins);
  textAt(buf, W - 8, 18, 4, C_GOLD, MR_DATUM);
  drawCoinIcon(W - 20 - fb.textWidth(buf, 4), 18, 8);

  for (int i = 0; i < 4; i++) {   // tabs
    int x = 4 + i * 79;
    uint16_t c = i == shopTab ? C_ORANGE_BTN : C_GREY_BTN;
    fb.fillRoundRect(x, 36, 75, 26, 8, c);
    fb.drawRoundRect(x, 36, 75, 26, 8, C_WHITE);
    fb.setTextDatum(MC_DATUM);
    fb.setTextColor(C_WHITE);
    fb.drawString(TAB_NAMES[i], x + 37, 50, 2);
  }

  int idx = shopIdx[shopTab];
  drawArrowButton(6, 80, 44, 56, true, C_BLUE_BTN);
  drawArrowButton(270, 80, 44, 56, false, C_BLUE_BTN);
  snprintf(buf, sizeof(buf), "%d/%d", idx + 1, shopCount(shopTab));
  textAt(buf, 312, 148, 2, C_WHITE, MR_DATUM);

  const char* name = "";
  const char* label1 = "";
  const char* label2 = nullptr;
  uint16_t col1 = C_GREEN_BTN, col2 = C_GREEN_BTN;
  int price = -1;
  const char* info = nullptr;
  priceHintLevel = 0;

  switch (shopTab) {
    case 0: {
      const Hat& h = HATS[idx];
      drawAxolotl(160, 116, 2, gameT, SKINS[p.skin], false, idx);
      name = h.name;
      if (!hatOwned(idx)) { price = h.price; label1 = buyArmed ? "Tap again to buy!" : "Buy"; col1 = buyArmed ? C_RED_BTN : (p.coins >= h.price ? C_GREEN_BTN : C_GREY_BTN); }
      else if (p.hat == idx + 1) { info = "You're wearing it!"; label1 = "Take off"; col1 = C_GREY_BTN; }
      else { info = "You own this hat"; label1 = "Wear it"; col1 = C_PURPLE_BTN; }
      break;
    }
    case 1: {
      int si = SHOP_SKIN_LIST[idx];
      drawAxolotl(160, 116, 2, gameT, SKINS[si], false, profHat(p));
      name = SKINS[si].name;
      if (!skinUnlocked(si)) { price = SKINS[si].price; if (!isShopOnly(si)) priceHintLevel = SKINS[si].lvl; label1 = buyArmed ? "Tap again to buy!" : "Buy"; col1 = buyArmed ? C_RED_BTN : (p.coins >= SKINS[si].price ? C_GREEN_BTN : C_GREY_BTN); }
      else if (p.skin == si && p.character == CH_AXOLOTL) { info = "You're wearing it!"; label1 = "Wearing"; col1 = C_GREY_BTN; }
      else { info = "You own this skin"; label1 = "Wear it"; col1 = C_PURPLE_BTN; }
      break;
    }
    case 2: {
      drawAxolotl(160, 112, 2, gameT, SKINS[p.skin], false, profHat(p));
      fb.drawCircle(160, 108, 44, RGB(170, 230, 255));
      fb.drawCircle(160, 108, 43, C_WHITE);
      fb.fillCircle(160 + (int)(cosf(gameT * 3) * 36), 108 + (int)(sinf(gameT * 3) * 36), 3, C_WHITE);
      name = "Shield";
      static char sbuf[40];
      snprintf(sbuf, sizeof(sbuf), "Safe for %d seconds. You have %d.", (int)shieldTime(), p.shields);
      info = shopMsgT > 0 ? nullptr : sbuf;
      if (p.shields >= MAX_SHIELDS) { label1 = "Pockets full"; col1 = C_GREY_BTN; }
      else {
        static char lbuf[24];
        snprintf(lbuf, sizeof(lbuf), "Buy for %d", SHIELD_PRICE);
        label1 = buyArmed ? "Tap again to buy!" : lbuf;
        col1 = buyArmed ? C_RED_BTN : (p.coins >= (uint32_t)SHIELD_PRICE ? C_GREEN_BTN : C_GREY_BTN);
      }
      break;
    }
    default: {
      if (idx == 0) {
        drawAxolotl(160, 116, 2, gameT, SKINS[p.skin], false, profHat(p));
        name = "Axolotl";
        info = "Our hero!";
        bool playing = p.character == CH_AXOLOTL;
        label1 = playing ? "Playing" : "Play as Axolotl";
        col1 = playing ? C_GREY_BTN : C_PURPLE_BTN;
      } else {
        int f = idx - 1;
        drawFriend(f, 160, 106, 3, gameT);
        name = FRIENDS[f].name;
        info = FRIENDS[f].perk;
        static char hbuf[20], ubuf[20];
        snprintf(hbuf, sizeof(hbuf), "Helper %d", HELPER_PRICE);
        snprintf(ubuf, sizeof(ubuf), "Unlock %d", FRIENDS[f].price);
        if (p.helper == f + 1) { label1 = "Coming!"; col1 = C_GREY_BTN; }
        else { label1 = buyArmed == 1 ? "Sure?" : hbuf; col1 = buyArmed == 1 ? C_RED_BTN : (p.coins >= (uint32_t)HELPER_PRICE && !p.helper ? C_TEAL_BTN : C_GREY_BTN); }
        if (!friendOwned(f)) { label2 = buyArmed == 2 ? "Sure?" : ubuf; col2 = buyArmed == 2 ? C_RED_BTN : (p.coins >= FRIENDS[f].price ? C_GREEN_BTN : C_GREY_BTN); }
        else if (p.character == f + 1) { label2 = "Playing"; col2 = C_GREY_BTN; }
        else { label2 = "Play as"; col2 = C_PURPLE_BTN; }
      }
      break;
    }
  }

  shadowText(name, W / 2, 152, fitFont(name, 260), C_WHITE);
  if (shopMsgT > 0 && shopMsg) shadowText(shopMsg, W / 2, 172, 2, C_GOLD);
  else if (price >= 0)         drawPriceLine(price, 172);
  else if (info)               shadowText(info, W / 2, 172, 2, C_WHITE);

  drawButton(6, 190, 80, 42, "Back", C_BLUE_BTN);
  if (label2) {
    drawButton(96, 190, 106, 42, label1, col1);
    drawButton(208, 190, 106, 42, label2, col2);
  } else {
    drawButton(96, 190, 218, 42, label1, col1);
  }
}

// ============================================================
//   SCREEN: out of hearts
// ============================================================
void updateOver(float dt) {
  overT += dt;
  if (newRecord && overT < 3.0f && random(100) < 30)
    burst(frand(50, 270), frand(40, 110), pastelHue(frand(0, 1)), 5);
  if (!tPressed) return;
  if (inRect(tx, ty, 55, 165, 100, 40)) {
    playSound(SND(SND_CLICK));
    startRun();            // try this level again
  } else if (inRect(tx, ty, 165, 165, 100, 40)) {
    playSound(SND(SND_CLICK));
    state = ST_TITLE;
    lockInput(250);
  }
}

void drawOver() {
  drawGame();
  drawPanel(40, 30, 240, 185);
  shadowText(overMsg, W / 2, 50, 4, newRecord ? C_GOLD : C_WHITE);

  char buf[32];
  snprintf(buf, sizeof(buf), "Reached level %d", curLevel);
  shadowText(buf, W / 2, 74, 2, C_WHITE);

  snprintf(buf, sizeof(buf), "%d", runScore);
  fb.setTextDatum(MC_DATUM);
  fb.setTextColor(C_WHITE);
  fb.drawString(buf, W / 2, 106, 7);

  if (overRank >= 0) snprintf(buf, sizeof(buf), "#%d on the leaderboard!", overRank + 1);
  else               snprintf(buf, sizeof(buf), "Best: %lu", (unsigned long)P().best);
  shadowText(buf, W / 2, 144, 2, overRank >= 0 ? C_GOLD : C_WHITE);

  drawButton(55, 165, 100, 40, "Again", C_GREEN_BTN);
  drawButton(165, 165, 100, 40, "Menu", C_BLUE_BTN);
}

// ============================================================
//   SETUP & LOOP
// ============================================================
void setup() {
  Serial.begin(115200);
  pinMode(LED_R_PIN, OUTPUT);
  pinMode(LED_G_PIN, OUTPUT);
  pinMode(LED_B_PIN, OUTPUT);
  ledSet(0, 0, 0);
  randomSeed(esp_random());

  tft.init();
  tft.setRotation(1);            // landscape; try 3 if the picture is upside down
  tft.fillScreen(TFT_BLACK);
  backlightInit();   // (brightness setting is applied once it's loaded below)

  touchSPI.begin(XPT2046_CLK, XPT2046_MISO, XPT2046_MOSI, XPT2046_CS);
  touch.begin(touchSPI);
  touch.setRotation(1);

  // Frame buffer: full colour in two halves, or 256 colours in one go
  fb.spr.setColorDepth(FULL_COLOUR ? 16 : 8);
  if (!fb.spr.createSprite(W, BAND_H)) {
    tft.setTextColor(TFT_RED);
    tft.drawString("Not enough memory!", 10, 10, 2);
    while (true) delay(1000);
  }

  prefs.begin("axolotl2", false);
  loadSaved();
  volume = prefs.getUChar("vol", 2);
  mode   = prefs.getUChar("mode", 0);
  musicOn = prefs.getUChar("mus", 1) != 0;
  brightness = prefs.getUChar("bri", 2);
  if (brightness > 2) brightness = 2;
  if (volume > 2) volume = 2;
  if (mode > 2) mode = 0;
  int last = prefs.getUChar("last", 0);
  if (last < MAX_PROFILES && profiles[last].used) cur = last;
  else for (int i = 0; i < MAX_PROFILES; i++) if (profiles[i].used) { cur = i; break; }
  wormsUntilTrophy = random(TROPHY_MIN_WORMS, TROPHY_MAX_WORMS + 1);

  int n = profileCount();
  if (n == 0)      { nameLen = 0; nameBuf[0] = 0; state = ST_NAME; }
  else if (n == 1) state = ST_TITLE;
  else             state = ST_PROFILES;

  ledcWrite(BL_CH, BRIGHT_LEVELS[brightness]);
  speakerInit();
  xTaskCreatePinnedToCore(soundTask, "sound", 3072, nullptr, 1, nullptr, 0);

  buildSkinOrder();
  initBg();
  playSound(SND(SND_START), 2);
  ledFlash(1, 0, 1, 500);
  lastMs = millis();
  lastActivityMs = lastMs;
}

// Draws one whole frame. Must not change the game, because it runs once per half.
void drawFrame() {
  switch (state) {
    case ST_PROFILES: drawProfiles(); break;
    case ST_NAME:     drawName();     break;
    case ST_CONFIRM:  drawConfirm();  break;
    case ST_TITLE:    drawTitle();    break;
    case ST_SKINS:    drawSkins();    break;
    case ST_SCORES:   drawScores();   break;
    case ST_SHOP:     drawShop();     break;
    case ST_STICKERS: drawStickers(); break;
    case ST_SETTINGS: drawSettings(); break;
    case ST_REVIVE:   drawRevive();   break;
    case ST_READY:    drawReady();    break;
    case ST_PLAY:     drawGame(); drawHUD(true); break;
    case ST_PAUSE:    drawPause();    break;
    case ST_CLEAR:    drawClear();    break;
    case ST_OVER:     drawOver();     break;
  }
  drawSparksAndText();
  drawStickerBanner();

  if (TOUCH_DEBUG && tDown) {
    fb.fillCircle(tx, ty, 4, TFT_RED);
    char b[32];
    snprintf(b, sizeof(b), "raw %d,%d", rawX, rawY);
    fb.setTextDatum(BL_DATUM);
    fb.setTextColor(TFT_RED);
    fb.drawString(b, 4, H - 2, 2);
  }
}

void loop() {
  uint32_t now = millis();
  float dt = (now - lastMs) / 1000.0f;
  lastMs = now;
  if (dt > 0.05f) dt = 0.05f;

  readTouch(now);
  ledUpdate(now);

  // dim the screen on menus after a while; the first touch just wakes it
  if (tDown || state == ST_PLAY || state == ST_READY) lastActivityMs = now;
  if (dimmed && tDown) {
    dimmed = false;
    ledcWrite(BL_CH, BRIGHT_LEVELS[brightness]);
    tPressed = false;
    lockInput(300);
  } else if (!dimmed && now - lastActivityMs > DIM_AFTER_MS) {
    dimmed = true;
    ledcWrite(BL_CH, BRIGHT_DIM);
  }
  gameT += dt;
  if (wiggleT > 0) wiggleT -= dt;

  switch (state) {
    case ST_PROFILES: updateProfiles(dt); break;
    case ST_NAME:     updateName(dt);     break;
    case ST_CONFIRM:  updateConfirm();    break;
    case ST_TITLE:    updateTitle(dt);    break;
    case ST_SKINS:    updateSkins(dt);    break;
    case ST_SCORES:   updateScores(dt);   break;
    case ST_SHOP:     updateShop(dt);     break;
    case ST_STICKERS: updateStickers(dt); break;
    case ST_SETTINGS: updateSettings(dt); break;
    case ST_REVIVE:   updateRevive();     break;
    case ST_READY:    updateReady(dt);    break;
    case ST_PLAY:     updatePlay(dt);     break;
    case ST_PAUSE:    updatePause();      break;
    case ST_CLEAR:    updateClear(dt);    break;
    case ST_OVER:     updateOver(dt);     break;
  }

  updateStickerBanner(dt);
  musicWanted = (state == ST_PLAY);
  float worldSpd = 0;
  if (state == ST_PLAY) worldSpd = speed;
  else if (state == ST_TITLE || state == ST_PROFILES) worldSpd = 35;
  updateCommon(dt, worldSpd);

  // draw the screen (in two halves when FULL_COLOUR is on)
  for (int band = 0; band < BANDS; band++) {
    fb.yo = band * BAND_H;
    drawFrame();
    fb.spr.pushSprite(0, fb.yo);
  }
}
