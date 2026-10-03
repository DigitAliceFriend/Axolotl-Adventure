/*
 * ============================================================
 *              AXOLOTL ADVENTURE  by AliceFriend
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
#include "GameTypes.h"   // all game types live here

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
#define BRIGHT_FULL   255
#define BRIGHT_DIM    20

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
TFT_eSprite         fb(&tft);        // full-screen frame buffer = no flicker
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
struct Theme { const char* name; uint16_t water[6]; uint16_t hill, sand, sandDark; };
const Theme THEMES[4] = {
  {"Sunny Lagoon", {RGB(120, 215, 240), RGB(90, 195, 230), RGB(60, 170, 220), RGB(40, 145, 205), RGB(30, 120, 185), RGB(20, 95, 165)},
   RGB(20, 90, 125), RGB(240, 210, 140), RGB(200, 165, 100)},
  {"Coral Reef",   {RGB(110, 225, 215), RGB(80, 205, 200), RGB(55, 180, 190), RGB(40, 155, 175), RGB(30, 130, 160), RGB(20, 105, 145)},
   RGB(230, 110, 130), RGB(250, 225, 170), RGB(215, 180, 120)},
  {"Sunset Bay",   {RGB(255, 190, 140), RGB(240, 160, 150), RGB(200, 130, 170), RGB(150, 105, 180), RGB(100, 85, 170), RGB(60, 60, 150)},
   RGB(70, 50, 120), RGB(235, 195, 150), RGB(195, 150, 110)},
  {"Deep Sea",     {RGB(40, 90, 160), RGB(30, 75, 140), RGB(25, 60, 120), RGB(20, 48, 100), RGB(15, 38, 85), RGB(10, 28, 70)},
   RGB(20, 40, 75), RGB(150, 140, 120), RGB(115, 105, 90)},
};
const uint16_t FISH_COLORS[3] = { RGB(255, 140, 40), RGB(255, 220, 50), RGB(190, 110, 235) };

// ---------------- Skins ----------------
// The first skin is free. Finishing level N unlocks skin number N+1.
const Skin SKINS[] = {
  {"Pinky",    RGB(255, 160, 190), RGB(255, 215, 230), RGB(235, 70, 130),  C_BLACK,           0},  // free
  {"Goldie",   RGB(255, 205, 70),  RGB(255, 240, 170), RGB(255, 120, 30),  C_BLACK,           0},  // level 1
  {"Wild",     RGB(105, 120, 70),  RGB(160, 175, 110), RGB(75, 60, 35),    RGB(230, 190, 40), 0},  // level 2
  {"Sky",      RGB(90, 150, 255),  RGB(175, 205, 255), RGB(40, 70, 200),   C_BLACK,           0},  // level 3
  {"Minty",    RGB(110, 225, 160), RGB(200, 255, 220), RGB(30, 150, 90),   C_BLACK,           0},  // level 4
  {"Magenta",  RGB(235, 60, 190),  RGB(255, 160, 225), RGB(160, 20, 130),  C_BLACK,           0},  // level 5
  {"Cyan",     RGB(60, 220, 235),  RGB(180, 245, 250), RGB(0, 140, 170),   C_BLACK,           0},  // level 6
  {"Lavender", RGB(190, 160, 240), RGB(225, 210, 255), RGB(140, 90, 210),  C_BLACK,           0},  // level 7
  {"White",    RGB(245, 245, 250), RGB(220, 225, 240), RGB(255, 140, 170), C_BLACK,           0},  // level 8
  {"Midnight", RGB(90, 60, 165),   RGB(150, 120, 220), RGB(255, 120, 220), C_WHITE,           0},  // level 9
  {"Rainbow",  0, 0, 0,                                                     C_BLACK,           1},  // level 10
  {"Ruby",     RGB(230, 50, 50),   RGB(255, 140, 130), RGB(150, 15, 30),   C_BLACK,           0},  // level 11 (red)
  {"Lemon",    RGB(255, 240, 60),  RGB(255, 250, 180), RGB(240, 175, 0),   C_BLACK,           0},  // level 12 (yellow)
  {"Cloud",    RGB(200, 200, 210), RGB(235, 235, 242), RGB(140, 145, 170), C_BLACK,           0},  // level 13 (light gray)
  {"Shadow",   RGB(25, 25, 32),    RGB(55, 55, 68),    RGB(95, 65, 120),   RGB(235, 195, 40), 0},  // level 14 (deep black)
  {"Forest",   RGB(30, 100, 50),   RGB(70, 145, 85),   RGB(15, 60, 30),    RGB(235, 195, 40), 0},  // level 15 (dark green)
  {"Twilight", 0, 0, 0,                                                     C_WHITE,           2},  // level 16 (dark rainbow)
};
const int N_SKINS = sizeof(SKINS) / sizeof(SKINS[0]);

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
  ledcWrite(BL_CH, BRIGHT_FULL);
}

void speakerTone(uint16_t f) {
  if (f == 0 || volume == 0) { ledcWrite(SPK_CH, 0); return; }
  ledcChangeFrequency(SPK_CH, f, 8);
  ledcWrite(SPK_CH, volume == 1 ? 10 : 128);   // smaller duty = quieter
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
  for (;;) {
    const Note* req = nullptr;
    uint8_t reqLen = 0;
    portENTER_CRITICAL(&sndMux);
    if (sndReqSeq) { req = sndReqSeq; reqLen = sndReqLen; sndReqSeq = nullptr; }
    portEXIT_CRITICAL(&sndMux);

    uint32_t now = millis();
    if (req) {
      seq = req; len = reqLen; pos = 0;
      speakerTone(seq[0].f);
      noteEnd = now + seq[0].ms;
    } else if (seq && (int32_t)(now - noteEnd) >= 0) {
      pos++;
      if (pos >= len) {
        seq = nullptr;
        speakerTone(0);
        sndBusy = false;
        sndBusyPrio = 0;
      } else {
        speakerTone(seq[pos].f);
        noteEnd = now + seq[pos].ms;
      }
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

const int MAX_OBS = 8, MAX_PK = 6, MAX_SPARK = 32, MAX_TXT = 6, MAX_BG = 12;
Obstacle  obs[MAX_OBS];
Pickup    pks[MAX_PK];
Spark     sparks[MAX_SPARK];
FloatText ftxt[MAX_TXT];
BgBubble  bgb[MAX_BG];

Profile    profiles[MAX_PROFILES];
ScoreEntry scores[MAX_SCORES];
int cur = -1;                  // current player

float py = 110, vy = 0, invuln = 0, gameT = 0, scrollX = 0, spawnDist = 0, speed = 0;
float readyT = 0, overT = 0, wiggleT = 0;
int   levelScore = 0, runScore = 0, curLevel = 1, lives = 3;
int   mode = 0, browse = 0, readyCount = -1, wormsUntilTrophy = 100;
int   overRank = -1, confirmIdx = -1;
bool  runActive = false, newRecord = false, deleteMode = false, quitArmed = false;
const char* overMsg = "";
const char* newSkinName = nullptr;
char  nameBuf[NAME_LEN + 1];
int   nameLen = 0;
uint32_t lastMs = 0, lastActivityMs = 0;
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

void loadSaved() {
  for (int i = 0; i < MAX_PROFILES; i++) {
    char key[6];
    profileKey(i, key);
    Profile& p = profiles[i];
    memset(&p, 0, sizeof(Profile));
    if (prefs.isKey(key)) prefs.getBytes(key, &p, sizeof(Profile));
    p.name[NAME_LEN] = 0;
    if (p.used != 1) { memset(&p, 0, sizeof(Profile)); continue; }
    if (p.unlocked < 1) p.unlocked = 1;
    if (p.unlocked > N_SKINS) p.unlocked = N_SKINS;
    if (p.skin >= p.unlocked) p.skin = 0;
    if (p.seen > p.unlocked) p.seen = p.unlocked;
    if (p.level < 1) p.level = 1;
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

bool skinUnlocked(int i) { return cur >= 0 && i < P().unlocked; }

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

void drawNewBadge(int x, int y) {
  fb.fillRoundRect(x, y, 38, 18, 8, C_RED_BTN);
  fb.drawRoundRect(x, y, 38, 18, 8, C_WHITE);
  fb.setTextDatum(MC_DATUM);
  fb.setTextColor(C_WHITE);
  fb.drawString("NEW!", x + 19, y + 9, 2);
}

bool hasNewSkins() { return cur >= 0 && P().seen < P().unlocked; }

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

// The star of the show! Facing right. s = scale (1 in game, 2-3 in menus)
void drawAxolotl(int cx, int cy, int s, float t, const Skin& sk, bool locked = false) {
  uint16_t body = sk.body, belly = sk.belly, gill = sk.gill, eye = sk.eye;
  if (sk.rainbow == 1) {
    float h = fmodf(t * 0.3f, 1.0f);
    body  = pastelHue(h);
    belly = pastelHue(fmodf(h + 0.12f, 1.0f));
    gill  = pastelHue(fmodf(h + 0.5f, 1.0f));
  } else if (sk.rainbow == 2) {
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
}

int currentTheme() {
  bool inGame = state == ST_READY || state == ST_PLAY || state == ST_PAUSE || state == ST_CLEAR || state == ST_OVER;
  return inGame ? (curLevel - 1) % 4 : 0;
}

void drawBackground() {
  const Theme& T = THEMES[currentTheme()];
  for (int i = 0; i < 6; i++) {
    int y0 = i * SAND_Y / 6, y1 = (i + 1) * SAND_Y / 6;
    fb.fillRect(0, y0, W, y1 - y0, T.water[i]);
  }
  // far-away hills (slow parallax)
  float s = scrollX * 0.25f;
  for (int x = 0; x < W; x += 4) {
    int h = 16 + (int)(9 * sinf((x + s) * 0.025f) + 5 * sinf((x + s) * 0.071f));
    fb.fillRect(x, SAND_Y - h, 4, h, T.hill);
  }
  // rising background bubbles
  for (int i = 0; i < MAX_BG; i++) fb.drawCircle((int)bgb[i].x, (int)bgb[i].y, bgb[i].r, C_BUBBLE);
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

void drawPlayer() {
  if (invuln > 0 && ((int)(invuln * 12)) % 2 == 0) return;   // blink after a bump
  int bob = (state == ST_READY) ? (int)(sinf(gameT * 3) * 3) : 0;
  drawAxolotl(PLAYER_X, (int)py + bob, 1, gameT, SKINS[currentSkin()]);
}

void drawSparksAndText() {
  for (int i = 0; i < MAX_SPARK; i++)
    if (sparks[i].on) fb.fillCircle((int)sparks[i].x, (int)sparks[i].y, sparks[i].life > 0.3f ? 2 : 1, sparks[i].c);
  for (int i = 0; i < MAX_TXT; i++)
    if (ftxt[i].on) shadowText(ftxt[i].s, (int)ftxt[i].x, (int)ftxt[i].y, 4, ftxt[i].c);
}

void drawHUD(bool showPause) {
  for (int i = 0; i < lives; i++) drawHeart(14 + i * 18, 12, 4, C_HEART);

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
  textAt(buf, W - 42, 12, 2, C_GOLD, MR_DATUM);

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
    if (b.y < -5 || b.x < -5) { b.y = SAND_Y + frand(0, 20); b.x = frand(0, W + 40); }
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

void startLevel() {
  clearWorld();
  py = SAND_Y / 2; vy = 0; invuln = 0;
  levelScore = 0;
  lives = MODES[mode].lives;       // hearts refill every level
  speed = MODES[mode].speed;
  spawnDist = 120;
  goReady();
}

void startRun() {
  runScore = 0;
  runActive = true;
  curLevel = P().level;
  startLevel();
}

// Ends the current run: saves best score and adds it to the leaderboard.
void endRun() {
  if (!runActive) return;
  runActive = false;
  if ((uint32_t)runScore > P().best) { P().best = runScore; saveProfile(cur); }
  overRank = insertScore(P().name, runScore, curLevel);
}

void levelComplete() {
  state = ST_CLEAR;
  overT = 0;
  newSkinName = nullptr;
  Profile& p = P();
  int unlockTo = curLevel + 1;
  if (unlockTo > N_SKINS) unlockTo = N_SKINS;
  if (p.unlocked < unlockTo) { p.unlocked = unlockTo; newSkinName = SKINS[unlockTo - 1].name; }
  if (curLevel >= p.level) p.level = curLevel + 1;
  if ((uint32_t)runScore > p.best) p.best = runScore;
  saveProfile(cur);
  playSound(SND(SND_CLEAR), 4);
  ledFlash(0, 1, 0, 800);
  lockInput(900);
}

void gameOver() {
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

void spawnObstacle(int gap) {
  int slot = -1;
  for (int i = 0; i < MAX_OBS; i++) if (!obs[i].on) { slot = i; break; }
  if (slot < 0) return;
  Obstacle& o = obs[slot];
  o.on = true; o.passed = false; o.phase = frand(0, 6.28f); o.spd = 0; o.y = 0; o.col = 0;

  int r = random(100);
  if (r < 30) {
    o.type = OB_ROCK; o.w = random(34, 53); o.h = random(28, 71); o.x = W + 10;
  } else if (r < 55) {
    o.type = OB_WEED; o.w = 16; o.h = random(60, 116); o.x = W + 20;
  } else if (r < 82 || levelScore < 4) {
    o.type = OB_FISH; o.w = 24; o.h = 14; o.x = W + 24;
    o.y = constrain(py + frand(-35, 35), 28.0f, (float)(SAND_Y - 24));  // fish aim near you
    o.spd = frand(12, 40) * MODES[mode].fishSpeed;
    o.col = FISH_COLORS[random(3)];
  } else {
    o.type = OB_ROCK_TOP; o.w = random(34, 51); o.h = random(30, 66); o.x = W + 10;
  }

  // place a treat in the open water after the obstacle
  if (random(100) < 75) {
    for (int i = 0; i < MAX_PK; i++) {
      if (pks[i].on) continue;
      Pickup& p = pks[i];
      p.on = true;
      p.x = o.x + gap / 2.0f;
      p.y = frand(35, SAND_Y - 30);
      p.phase = frand(0, 6.28f);
      if (lives < MODES[mode].lives && random(100) < 10) {
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
    case OB_ROCK_TOP: x = (int)o.x + 3; y = 0; w = o.w - 6; h = o.h - 3; break;
    case OB_WEED:     x = (int)o.x - 6; y = SAND_Y - o.h + 4; w = 14; h = o.h; break;
    default:          x = (int)o.x - 10; y = (int)o.y - 5; w = 22; h = 10; break;
  }
}

void collect(Pickup& p) {
  p.on = false;
  switch (p.type) {
    case PK_BUBBLE:
      burst(p.x, p.y, C_BUBBLE, 8);
      playSound(SND(SND_BUBBLE), 1);
      ledFlash(0, 1, 1, 120);
      addText("+2", p.x, p.y - 15, C_BUBBLE);
      addScore(BUBBLE_POINTS);
      break;
    case PK_WORM:
      burst(p.x, p.y, C_WORM, 8);
      playSound(SND(SND_WORM), 1);
      ledFlash(1, 1, 0, 150);
      addText("+5", p.x, p.y - 15, C_GOLD);
      addScore(WORM_POINTS);
      break;
    case PK_TROPHY:
      burst(p.x, p.y, C_TROPHY, 24);
      playSound(SND(SND_TROPHY), 3);
      ledFlash(1, 1, 0, 700);
      addText("+20!", p.x, p.y - 18, C_TROPHY);
      addScore(TROPHY_POINTS);
      break;
    case PK_HEART:
      if (lives < MODES[mode].lives) lives++;
      burst(p.x, p.y, C_HEART, 12);
      addText("+1 Heart!", p.x, p.y - 15, C_HEART);
      playSound(SND(SND_HEART), 2);
      ledFlash(1, 0, 1, 400);
      break;
  }
}

void openSkins(State returnTo) {
  skinReturn = returnTo;
  browse = P().skin;
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
    drawAxolotl(x + 27, y + 24, 1, gameT + i, SKINS[p.skin]);
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
      p.unlocked = 1;
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
void updateTitle(float dt) {
  scrollX += 35 * dt;
  if (!tPressed) return;
  if (inRect(tx, ty, 95, 122, 130, 44)) {
    playSound(SND(SND_CLICK));
    startRun();
  } else if (inRect(tx, ty, 5, 122, 85, 44)) {
    playSound(SND(SND_CLICK));
    state = ST_SCORES;
    lockInput(250);
  } else if (inRect(tx, ty, 230, 122, 85, 44)) {
    playSound(SND(SND_CLICK));
    deleteMode = false;
    state = ST_PROFILES;
    lockInput(250);
  } else if (inRect(tx, ty, 5, 178, 100, 48)) {
    playSound(SND(SND_CLICK));
    openSkins(ST_TITLE);
  } else if (inRect(tx, ty, 110, 178, 100, 48)) {
    mode = (mode + 1) % 3;
    prefs.putUChar("mode", mode);
    playSound(SND(SND_CLICK));
  } else if (inRect(tx, ty, 215, 178, 100, 48)) {
    volume = (volume + 1) % 3;
    prefs.putUChar("vol", volume);
    playSound(SND(SND_SELECT));
  } else if (inRect(tx, ty, 60, 60, 100, 56)) {
    wiggleT = 0.6f;
    playSound(SND(SND_SQUEAK));
  }
}

void drawTitle() {
  drawBackground();
  shadowText("Axolotl Adventure", W / 2, 20, 4, C_GOLD);
  shadowText("by AliceFriend", W / 2, 42, 2, C_WHITE);
  int wig = wiggleT > 0 ? (int)(sinf(gameT * 30) * 4) : 0;
  drawAxolotl(110, 90 + (int)(sinf(gameT * 2) * 4) + wig, 2, gameT, SKINS[P().skin]);

  char buf[24];
  textAt(P().name, 240, 70, fitFont(P().name, 140), C_WHITE, MC_DATUM);
  snprintf(buf, sizeof(buf), "Level %d", P().level);
  textAt(buf, 240, 92, 2, C_GOLD, MC_DATUM);
  snprintf(buf, sizeof(buf), "Best: %lu", (unsigned long)P().best);
  textAt(buf, 240, 108, 2, C_WHITE, MC_DATUM);

  drawButton(5, 122, 85, 44, "Scores", C_TEAL_BTN);
  drawButton(95, 122, 130, 44, "PLAY", C_GREEN_BTN);
  drawButton(230, 122, 85, 44, "Player", C_BLUE_BTN);
  drawButton2(5, 178, 100, 48, "SKIN", SKINS[P().skin].name, C_PURPLE_BTN);
  if (hasNewSkins()) drawNewBadge(70, 170);
  drawButton2(110, 178, 100, 48, "SPEED", MODES[mode].name, C_ORANGE_BTN);
  drawButton2(215, 178, 100, 48, "SOUND", VOL_NAMES[volume], C_BLUE_BTN);
}

// ============================================================
//   SCREEN: skins
// ============================================================
void updateSkins(float dt) {
  if (skinReturn == ST_TITLE) scrollX += 20 * dt;
  if (!tPressed) return;
  bool un = skinUnlocked(browse);
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
      P().skin = browse;
      saveProfile(cur);
      playSound(SND(SND_SELECT));
    } else {
      playSound(SND(SND_CLICK));
    }
    if (P().seen < P().unlocked) { P().seen = P().unlocked; saveProfile(cur); }   // badges cleared
    state = skinReturn;
    lockInput(250);
  }
}

void drawSkins() {
  drawBackground();
  shadowText("Pick Your Axolotl", W / 2, 22, 4, C_GOLD);
  bool un = skinUnlocked(browse);
  int wig = wiggleT > 0 ? (int)(sinf(gameT * 30) * 5) : 0;
  drawAxolotl(160, 100 + wig, 3, gameT, SKINS[browse], !un);

  if (!un) {  // padlock
    for (int r = 6; r <= 9; r++) fb.drawCircle(160, 94, r, C_GOLD);
    fb.fillRoundRect(147, 96, 26, 20, 4, C_GOLD);
    fb.fillCircle(160, 104, 3, C_BLACK);
    fb.fillRect(159, 104, 3, 7, C_BLACK);
  }

  drawArrowButton(8, 72, 50, 56, true, C_BLUE_BTN);
  drawArrowButton(262, 72, 50, 56, false, C_BLUE_BTN);
  shadowText(SKINS[browse].name, W / 2, 150, 4, C_WHITE);
  if (un && browse >= P().seen) drawNewBadge(212, 52);

  char buf[32];
  if (!un) {
    snprintf(buf, sizeof(buf), "Finish level %d to unlock!", browse);
    shadowText(buf, W / 2, 174, 2, C_GOLD);
  } else if (browse == P().skin) {
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
  bool onPause = inRect(tx, ty, W - 44, 0, 44, 38);
  if (tPressed && onPause) {
    state = ST_PAUSE;
    quitArmed = false;
    playSound(SND(SND_CLICK));
    lockInput(250);
    return;
  }

  // --- controls: top half = up, bottom half = down ---
  if (tDown && !onPause) {
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
  if (py < 18)          { py = 18;          if (vy < 0) vy = 0; }
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

  // forgiving hitbox (smaller than the drawing)
  int hx = PLAYER_X - 10, hy = (int)py - 6, hw = 26, hh = 13;

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
    if (invuln <= 0 && rectsOverlap(hx, hy, hw, hh, bx, by, bw, bh)) {
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
    if (ddx * ddx + ddy * ddy < 18 * 18) {
      collect(p);
      if (state != ST_PLAY) return;
    }
  }

  if (invuln > 0) invuln -= dt;
}

// ============================================================
//   SCREEN: pause menu
// ============================================================
void updatePause() {
  if (!tPressed) return;
  if (inRect(tx, ty, 70, 52, 180, 42)) {
    playSound(SND(SND_CLICK));
    goReady();
  } else if (inRect(tx, ty, 55, 104, 100, 48)) {
    playSound(SND(SND_CLICK));
    openSkins(ST_PAUSE);
  } else if (inRect(tx, ty, 165, 104, 100, 48)) {
    volume = (volume + 1) % 3;
    prefs.putUChar("vol", volume);
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
  drawButton2(55, 104, 100, 48, "SKIN", SKINS[P().skin].name, C_PURPLE_BTN);
  if (hasNewSkins()) drawNewBadge(120, 96);
  drawButton2(165, 104, 100, 48, "SOUND", VOL_NAMES[volume], C_BLUE_BTN);
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
  if (inRect(tx, ty, 55, 165, 100, 40)) {
    playSound(SND(SND_CLICK));
    curLevel++;
    startLevel();          // run score carries on
  } else if (inRect(tx, ty, 165, 165, 100, 40)) {
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
  int yInfo = 132;
  if (newSkinName) {
    drawStar(88, 112, 10, C_GOLD);
    drawStar(232, 112, 10, C_GOLD);
    drawAxolotl(160, 116, 2, overT * 2, SKINS[P().unlocked - 1]);   // show off the new skin
    yInfo = 140;
    snprintf(buf, sizeof(buf), "New skin: %s!", newSkinName);
    shadowText(buf, W / 2, yInfo, 2, C_GOLD);
  } else {
    for (int k = 0; k < 3; k++) drawStar(120 + k * 40, 104, k == 1 ? 13 : 10, C_GOLD);
    snprintf(buf, sizeof(buf), "Score: %d", runScore);
    shadowText(buf, W / 2, yInfo, 2, C_WHITE);
  }
  snprintf(buf, sizeof(buf), "Next goal: %d points", levelGoal(curLevel + 1));
  shadowText(buf, W / 2, yInfo + 15, 2, C_WHITE);
  drawButton(55, 165, 100, 40, "Next", C_GREEN_BTN);
  drawButton(165, 165, 100, 40, "Menu", C_BLUE_BTN);
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
  backlightInit();

  touchSPI.begin(XPT2046_CLK, XPT2046_MISO, XPT2046_MOSI, XPT2046_CS);
  touch.begin(touchSPI);
  touch.setRotation(1);

  // 8-bit colour frame buffer (a 16-bit one won't fit in the ESP32's RAM)
  fb.setColorDepth(8);
  if (!fb.createSprite(W, H)) {
    tft.setTextColor(TFT_RED);
    tft.drawString("Not enough memory!", 10, 10, 2);
    while (true) delay(1000);
  }

  prefs.begin("axolotl2", false);
  loadSaved();
  volume = prefs.getUChar("vol", 2);
  mode   = prefs.getUChar("mode", 0);
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

  speakerInit();
  xTaskCreatePinnedToCore(soundTask, "sound", 3072, nullptr, 1, nullptr, 0);

  initBg();
  playSound(SND(SND_START), 2);
  ledFlash(1, 0, 1, 500);
  lastMs = millis();
  lastActivityMs = lastMs;
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
    ledcWrite(BL_CH, BRIGHT_FULL);
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
    case ST_READY:    updateReady(dt);    break;
    case ST_PLAY:     updatePlay(dt);     break;
    case ST_PAUSE:    updatePause();      break;
    case ST_CLEAR:    updateClear(dt);    break;
    case ST_OVER:     updateOver(dt);     break;
  }

  float worldSpd = 0;
  if (state == ST_PLAY) worldSpd = speed;
  else if (state == ST_TITLE || state == ST_PROFILES) worldSpd = 35;
  updateCommon(dt, worldSpd);

  switch (state) {
    case ST_PROFILES: drawProfiles(); break;
    case ST_NAME:     drawName();     break;
    case ST_CONFIRM:  drawConfirm();  break;
    case ST_TITLE:    drawTitle();    break;
    case ST_SKINS:    drawSkins();    break;
    case ST_SCORES:   drawScores();   break;
    case ST_READY:    drawReady();    break;
    case ST_PLAY:     drawGame(); drawHUD(true); break;
    case ST_PAUSE:    drawPause();    break;
    case ST_CLEAR:    drawClear();    break;
    case ST_OVER:     drawOver();     break;
  }
  drawSparksAndText();

  if (TOUCH_DEBUG && tDown) {
    fb.fillCircle(tx, ty, 4, TFT_RED);
    char b[32];
    snprintf(b, sizeof(b), "raw %d,%d", rawX, rawY);
    fb.setTextDatum(BL_DATUM);
    fb.setTextColor(TFT_RED);
    fb.drawString(b, 4, H - 2, 2);
  }

  fb.pushSprite(0, 0);
}
